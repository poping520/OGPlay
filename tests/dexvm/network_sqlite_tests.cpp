#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/database/database_runtime.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/network_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::optional<std::vector<std::uint8_t>> ReadLocalAngryBirdsDex() {
  const auto path = std::filesystem::path(OGPLAY_SOURCE_DIR) / ".local" /
                    "games" / "com.rovio.angrybirds_2.3.0.apk";
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return std::nullopt;
  const std::vector<char> raw{std::istreambuf_iterator<char>(stream),
                              std::istreambuf_iterator<char>()};
  std::vector<std::byte> apk(raw.size());
  std::memcpy(apk.data(), raw.data(), raw.size());
  const auto dex = ogplay::loader::ReadApkEntry(
      apk, ogplay::loader::ParseApkArchive(apk), "classes.dex");
  std::vector<std::uint8_t> result(dex.size());
  std::memcpy(result.data(), dex.data(), dex.size());
  return result;
}

class FakeNetwork final : public NetworkTransport {
public:
  std::vector<std::string> Resolve(std::string_view host) override {
    resolved = std::string(host);
    return {"203.0.113.7"};
  }
  std::uint64_t Connect(std::string_view host, std::uint16_t port,
                        bool tls) override {
    connected = std::string(host) + ":" + std::to_string(port);
    used_tls = tls;
    return 7;
  }
  void Send(std::uint64_t channel, std::span<const std::byte> bytes) override {
    CHECK(channel == 7);
    sent.assign(bytes.begin(), bytes.end());
  }
  std::vector<std::byte> Receive(std::uint64_t channel,
                                 std::size_t maximum) override {
    CHECK(channel == 7);
    const std::string value = "pong";
    const auto count = std::min(maximum, value.size());
    return std::vector<std::byte>(
        reinterpret_cast<const std::byte *>(value.data()),
        reinterpret_cast<const std::byte *>(value.data() + count));
  }
  void Close(std::uint64_t channel) noexcept override {
    closed = channel;
    ++close_count;
  }
  void SendDatagram(const NetworkDatagram &datagram) override {
    last_datagram = datagram;
  }
  NetworkDatagram ReceiveDatagram(std::size_t maximum) override {
    NetworkDatagram result{"game.test", 9000, {std::byte{'o'}, std::byte{'k'}}};
    if (result.payload.size() > maximum)
      result.payload.resize(maximum);
    return result;
  }
  std::string resolved;
  std::string connected;
  bool used_tls{};
  std::uint64_t closed{};
  std::vector<std::byte> sent;
  NetworkDatagram last_datagram;
  std::uint32_t close_count{};
};

struct NetworkSqliteVm final {
  JniStringStore strings;
  JniPrimitiveArrayStore arrays;
  JavaObjectModel model{strings, arrays};
  DexClassLinker linker;
  ogplay::core::CapabilityLedger ledger;
  ogplay::core::Logger logger;
  VirtualFileSystem vfs;
  DexVmIoVfsAdapter io_file_system{vfs};
  std::shared_ptr<DexVmAndroidContext> context{
      std::make_shared<DexVmAndroidContext>()};
  Interpreter vm;
  VmThreadRuntime threads;
  std::int32_t helper_create_calls{};
  std::int32_t helper_upgrade_calls{};
  std::int32_t helper_old_version{};
  std::int32_t helper_new_version{};
  bool helper_create_throws{};
  std::shared_ptr<database::Connection> thread_connection;
  std::atomic<bool> thread_query_entered{};
  std::atomic<bool> thread_query_interrupted{};

  explicit NetworkSqliteVm(
      const InterpreterBackend backend = InterpreterBackend::switch_dispatch,
      std::vector<std::uint8_t> app_dex = {},
      std::string package_name = "test.game")
      : vm(
            [this, &app_dex, &package_name]() -> DexClassLinker & {
              context->package_name = package_name;
              context->vfs = &vfs;
              linker.RegisterIntrinsics(CoreIntrinsicCatalog(
                  {.current_time_millis = [] { return std::int64_t{1000}; }}));
              linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
              auto helper = IntrinsicClassBuilder::Class(
                  "Ltest/SqliteOpenHelperFixture;",
                  "Landroid/database/sqlite/SQLiteOpenHelper;");
              helper.OverrideMethod(
                  "onCreate", "(Landroid/database/sqlite/SQLiteDatabase;)V",
                  [this](IntrinsicContext &) {
                    ++helper_create_calls;
                    if (helper_create_throws) {
                      REQUIRE(context->sqlite_connections.size() == 1);
                      const auto database =
                          context->sqlite_connections.begin()->second;
                      database->Execute("CREATE TABLE callback_partial(value INTEGER)");
                      throw VmJavaThrow{"Ljava/lang/RuntimeException;",
                                        "injected onCreate failure"};
                    }
                    return VmValue::Void();
                  });
              helper.OverrideMethod(
                  "onUpgrade", "(Landroid/database/sqlite/SQLiteDatabase;II)V",
                  [this](IntrinsicContext &call) {
                    ++helper_upgrade_calls;
                    helper_old_version = call.arguments[1].AsInt();
                    helper_new_version = call.arguments[2].AsInt();
                    return VmValue::Void();
                  });
              auto sqlite_worker = IntrinsicClassBuilder::Class(
                  "Ltest/SqliteWorker;", "Ljava/lang/Thread;");
              sqlite_worker.OverrideMethod("run", "()V",
                                           [this](IntrinsicContext &) {
                thread_query_entered = true;
                std::this_thread::yield();
                try {
                  static_cast<void>(thread_connection->Query(
                      "WITH RECURSIVE n(x) AS (VALUES(0) UNION ALL SELECT "
                      "x+1 FROM n WHERE x<100000000) SELECT sum(x) FROM n"));
                } catch (const std::runtime_error &) {
                  thread_query_interrupted = true;
                }
                return VmValue::Void();
              });
              std::vector<IntrinsicClassDecl> test_catalog;
              test_catalog.push_back(std::move(helper).Build());
              test_catalog.push_back(std::move(sqlite_worker).Build());
              linker.RegisterIntrinsics(test_catalog);
              ogplay::test::RegisterBootDex(linker);
              if (!app_dex.empty())
                linker.RegisterDex(std::move(app_dex));
              linker.Link();
              return linker;
            }(),
            model, nullptr, ledger, InterpreterConfig{.backend = backend}),
        threads(vm) {
    RegisterAndroidDatabaseStateTables(vm, context);
    vm.IO().SetFileSystem(&io_file_system);
    vm.SetLogger(&logger);
    vm.Monitors().SetTimeSource([] { return std::int64_t{1000}; });
    vfs.CreateDirectory("/data");
    vfs.CreateDirectory("/data/data");
    vfs.CreateDirectory("/data/data/" + context->package_name);
    vfs.CreateDirectory("/data/data/" + context->package_name + "/databases");
  }

  ~NetworkSqliteVm() { ReleaseAndroidDatabaseResources(context); }

  VmObjectRef New(const char *descriptor, const char *constructor = "()V",
                  std::vector<VmValue> arguments = {}) {
    const auto object = vm.NewIntrinsicInstance(descriptor);
    const auto method = linker.FindDirectMethod(
        linker.ResolveDescriptor(descriptor), "<init>", constructor);
    REQUIRE(method.has_value());
    arguments.insert(arguments.begin(), VmValue::Ref(object));
    const auto outcome = vm.Call(*method, arguments);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return object;
  }

  VmValue Static(const char *owner, const char *name, const char *descriptor,
                 std::vector<VmValue> arguments = {}) {
    const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner),
                                                name, descriptor);
    REQUIRE(method.has_value());
    const auto outcome = vm.Call(*method, arguments);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return outcome.value;
  }

  VmCallOutcome StaticOutcome(const char *owner, const char *name,
                              const char *descriptor,
                              std::vector<VmValue> arguments = {}) {
    const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner),
                                                name, descriptor);
    REQUIRE(method.has_value());
    return vm.Call(*method, arguments);
  }

  VmValue On(VmObjectRef receiver, const char *name, const char *descriptor,
             std::vector<VmValue> arguments = {}) {
    const auto outcome =
        OnOutcome(receiver, name, descriptor, std::move(arguments));
    auto detail =
        (outcome.exception_class.IsValid()
             ? linker.Class(outcome.exception_class).descriptor + ": "
             : std::string{}) +
        outcome.exception_message;
    if (outcome.exception.IsValid()) {
      const auto cause_index = linker.FindVtableIndex(
          model.ObjectClass(outcome.exception), "getCause",
          "()Ljava/lang/Throwable;");
      if (cause_index.has_value()) {
        const std::array cause_arguments{VmValue::Ref(outcome.exception)};
        const auto cause = vm.Call(
            linker.Class(model.ObjectClass(outcome.exception))
                .vtable[*cause_index],
            cause_arguments);
        if (!cause.exception.IsValid() && cause.value.ref.IsValid()) {
          const auto cause_class = model.ObjectClass(cause.value.ref);
          detail += " cause=" + linker.Class(cause_class).descriptor;
          const auto message = vm.ThrowableMessage(cause.value.ref);
          if (message.IsValid())
            detail += ": " + vm.StringUtf8(message);
        }
      }
    }
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), detail);
    return outcome.value;
  }

  VmCallOutcome OnOutcome(VmObjectRef receiver, const char *name,
                          const char *descriptor,
                          std::vector<VmValue> arguments = {}) {
    const auto owner = model.ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(owner, name, descriptor);
    REQUIRE(index.has_value());
    arguments.insert(arguments.begin(), VmValue::Ref(receiver));
    return vm.Call(linker.Class(owner).vtable[*index], arguments);
  }

  VmObjectRef Strings(std::initializer_list<std::string_view> values) {
    const auto array =
        model.NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/String;"),
                             linker.ResolveDescriptor("Ljava/lang/String;"),
                             static_cast<JniSize>(values.size()));
    std::int32_t index{};
    for (const auto value : values)
      model.SetObjectElement(array, index++, vm.NewStringUtf8(value));
    return array;
  }

  VmObjectRef NewHelper(const std::string_view name,
                        const std::int32_t version) {
    const auto helper =
        vm.NewIntrinsicInstance("Ltest/SqliteOpenHelperFixture;");
    const auto constructor = linker.FindDirectMethod(
        linker.ResolveDescriptor("Landroid/database/sqlite/SQLiteOpenHelper;"),
        "<init>",
        "(Landroid/content/Context;Ljava/lang/String;Landroid/database/sqlite/"
        "SQLiteDatabase$CursorFactory;I)V");
    REQUIRE(constructor.has_value());
    const auto android_context = vm.NewIntrinsicInstance("Landroid/content/Context;");
    const std::array arguments{
        VmValue::Ref(helper), VmValue::Ref(android_context),
        VmValue::Ref(vm.NewStringUtf8(name)), VmValue::Ref(VmObjectRef{}),
        VmValue::Int(version)};
    const auto outcome = vm.Call(*constructor, arguments);
    REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
    return helper;
  }
};

std::vector<std::byte> ReadVfsFile(VirtualFileSystem &vfs,
                                   std::string_view path) {
  std::vector<std::byte> bytes(static_cast<std::size_t>(vfs.Stat(path).size));
  const auto descriptor = vfs.Open(path, {.read = true});
  std::size_t offset{};
  while (offset < bytes.size())
    offset += vfs.Read(descriptor, std::span(bytes).subspan(offset));
  vfs.Close(descriptor);
  return bytes;
}

void WriteVfsFile(VirtualFileSystem &vfs, std::string_view path,
                  std::span<const std::byte> bytes) {
  const auto descriptor =
      vfs.Open(path, {.write = true, .create = true, .truncate = true});
  std::size_t offset{};
  while (offset < bytes.size())
    offset += vfs.Write(descriptor, bytes.subspan(offset));
  vfs.Flush(descriptor);
  vfs.Close(descriptor);
}

std::vector<std::byte> ReadHostFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  REQUIRE(input.good());
  const auto size = input.tellg();
  REQUIRE(size >= 0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char *>(bytes.data()), size);
  REQUIRE(input.good());
  return bytes;
}

void WriteHostFile(const std::filesystem::path &path,
                   const std::span<const std::byte> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  REQUIRE(output.good());
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  REQUIRE(output.good());
}
} // namespace

TEST_CASE("DVM-186 SQLite VFS opens and closes an empty database") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto connection = database::Connection::Open(vfs, "/data/empty.db");
  CHECK(connection->IsOpen());
  CHECK(connection->UserVersion() == 0);
  connection->Execute(
      "CREATE TABLE probe (id INTEGER PRIMARY KEY, value TEXT)");
  connection->Flush();
  connection->Execute("INSERT INTO probe VALUES (1, 'ok')");
  connection->Flush();
  connection->Execute("BEGIN IMMEDIATE");
  connection->Execute("INSERT INTO probe VALUES (2, 'rollback')");
  connection->Execute("ROLLBACK");
  connection->Execute("BEGIN IMMEDIATE");
  connection->Execute("INSERT INTO probe VALUES (3, 'commit')");
  connection->Execute("COMMIT");
  CHECK(std::get<std::int64_t>(
            connection->Query("SELECT count(*) FROM probe").rows.at(0).at(0)) ==
        2);
}

TEST_CASE("DVM-186 memory databases stay memory only and file escapes fail") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto memory = database::Connection::Open(vfs, ":memory:");
  memory->Execute("CREATE TABLE value_table(value INTEGER)");
  memory->Execute("INSERT INTO value_table VALUES(19)");
  CHECK(std::get<std::int64_t>(
            memory->Query("SELECT value FROM value_table").rows[0][0]) == 19);
  CHECK_THROWS_AS(static_cast<void>(vfs.Stat(":memory:")), VfsError);

  auto file = database::Connection::Open(vfs, "/data/scope.db");
  CHECK_THROWS_WITH_AS(file->Execute("ATTACH '/data/other.db' AS other"),
                       doctest::Contains("not authorized"),
                       std::runtime_error);
  CHECK_THROWS_AS(static_cast<void>(vfs.Stat("/data/other.db")), VfsError);
  CHECK_THROWS_WITH_AS(file->Execute("PRAGMA journal_mode=WAL"),
                       doctest::Contains("not authorized"),
                       std::runtime_error);
}

TEST_CASE("DVM-186 SQLite time comes from the injected process clock") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto connection = database::Connection::Open(
      vfs, "/data/time.db", {}, [] { return std::int64_t{0}; });
  CHECK(std::get<std::string>(connection->Query("SELECT datetime('now')")
                                  .rows[0][0]) == "1970-01-01 00:00:00");
}

TEST_CASE("DVM-186 SQLite preserves values indexes and trigger effects") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto connection = database::Connection::Open(vfs, "/data/types.db");
  connection->Execute(
      "CREATE TABLE typed(id INTEGER PRIMARY KEY, nullable, real_value REAL, "
      "text_value TEXT, blob_value BLOB)");
  connection->Execute("CREATE INDEX typed_text ON typed(text_value)");
  connection->Execute("CREATE TABLE audit(inserted INTEGER)");
  connection->Execute(
      "CREATE TRIGGER typed_insert AFTER INSERT ON typed BEGIN INSERT INTO "
      "audit VALUES(new.id); END");
  const std::array<database::Value, 5> values{
      std::int64_t{7}, std::monostate{}, 3.25, std::string{"你好"},
      std::vector<std::byte>{std::byte{0}, std::byte{0xff}}};
  connection->Execute("INSERT INTO typed VALUES(?,?,?,?,?)", values);
  const auto result = connection->Query(
      "SELECT nullable,real_value,text_value,blob_value FROM typed "
      "INDEXED BY typed_text WHERE text_value='你好'");
  REQUIRE(result.rows.size() == 1);
  CHECK(std::holds_alternative<std::monostate>(result.rows[0][0]));
  CHECK(std::get<double>(result.rows[0][1]) == doctest::Approx(3.25));
  CHECK(std::get<std::string>(result.rows[0][2]) == "你好");
  CHECK(std::get<std::vector<std::byte>>(result.rows[0][3]) ==
        std::vector<std::byte>{std::byte{0}, std::byte{0xff}});
  CHECK(std::get<std::int64_t>(
            connection->Query("SELECT inserted FROM audit").rows[0][0]) == 7);
}

TEST_CASE("DVM-186 SQLite VFS journal and writer locks are real") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto first = database::Connection::Open(vfs, "/data/locks.db");
  first->Execute("CREATE TABLE values_table (value INTEGER)");
  auto second = database::Connection::Open(vfs, "/data/locks.db");
  first->Execute("BEGIN IMMEDIATE");
  first->Execute("INSERT INTO values_table VALUES (1)");
  CHECK(vfs.Stat("/data/locks.db-journal").size > 0);
  CHECK_THROWS(second->Execute("INSERT INTO values_table VALUES (2)"));
  first->Execute("ROLLBACK");
  CHECK_THROWS_AS(static_cast<void>(vfs.Stat("/data/locks.db-journal")),
                  VfsError);
  second->Execute("INSERT INTO values_table VALUES (3)");
  CHECK(std::get<std::int64_t>(
            second->Query("SELECT value FROM values_table").rows.at(0).at(0)) ==
        3);
}

TEST_CASE("DVM-186 SQLite locks and journals share canonical VFS identity") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto first = database::Connection::Open(vfs, "/DATA/Alias.DB");
  first->Execute("CREATE TABLE values_table (value INTEGER)");
  auto second = database::Connection::Open(vfs, "/data/alias.db");
  first->Execute("BEGIN IMMEDIATE");
  first->Execute("INSERT INTO values_table VALUES (1)");
  CHECK(vfs.Stat("/data/alias.db-journal").size > 0);
  CHECK_THROWS_AS(second->Execute("BEGIN IMMEDIATE"), database::Error);
  first->Execute("ROLLBACK");
  second->Execute("BEGIN IMMEDIATE");
  second->Execute("ROLLBACK");
}

TEST_CASE("DVM-186 SQLite preserves empty blobs through both bind paths") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto connection = database::Connection::Open(vfs, "/data/blobs.db");
  connection->Execute("CREATE TABLE values_table (value BLOB NOT NULL)");
  const std::array<database::Value, 1> empty{std::vector<std::byte>{}};
  connection->Execute("INSERT INTO values_table VALUES (?)", empty);
  const auto statement = connection->Prepare("INSERT INTO values_table VALUES (?)");
  connection->Bind(statement, 1, std::vector<std::byte>{});
  connection->Execute(statement);
  connection->Finalize(statement);
  const auto result = connection->Query(
      "SELECT typeof(value), length(value) FROM values_table");
  REQUIRE(result.rows.size() == 2);
  for (const auto &row : result.rows) {
    CHECK(std::get<std::string>(row[0]) == "blob");
    CHECK(std::get<std::int64_t>(row[1]) == 0);
  }
}

TEST_CASE("DVM-186 zero-byte databases reopen and initialize") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  const auto empty = vfs.Open("/data/empty.db",
                              {.read = true, .write = true, .create = true});
  vfs.Close(empty);
  auto connection = database::Connection::Open(vfs, "/data/empty.db");
  connection->Execute("CREATE TABLE values_table (value INTEGER)");
  connection->Close();
  CHECK(vfs.Stat("/data/empty.db").size > 0);
}

TEST_CASE("DVM-186 SQLite open flags control creation and writes") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  CHECK_THROWS_AS(database::Connection::Open(
                      vfs, "/data/missing.db", {}, {}, {}, {.create = false}),
                  database::Error);
  CHECK_THROWS_AS(static_cast<void>(vfs.Stat("/data/missing.db")), VfsError);
  auto writer = database::Connection::Open(vfs, "/data/existing.db");
  writer->Execute("CREATE TABLE values_table (value INTEGER)");
  writer->Close();
  auto reader = database::Connection::Open(
      vfs, "/data/existing.db", {}, {}, {},
      {.read_only = true, .create = false});
  CHECK_THROWS_AS(reader->Execute("INSERT INTO values_table VALUES (1)"),
                  database::Error);
  CHECK(std::get<std::int64_t>(reader->Query(
                                   "SELECT count(*) FROM values_table")
                                   .rows[0][0]) == 0);
}

TEST_CASE("DVM-186 API19 nativeOpen honors flags and maps open errors") {
  NetworkSqliteVm fixture;
  const auto open = [&](const std::string_view path, const int flags) {
    return fixture.StaticOutcome(
        "Landroid/database/sqlite/SQLiteConnection;", "nativeOpen",
        "(Ljava/lang/String;ILjava/lang/String;ZZ)I",
        {VmValue::Ref(fixture.vm.NewStringUtf8(path)), VmValue::Int(flags),
         VmValue::Ref(fixture.vm.NewStringUtf8("test")), VmValue::Int(0),
         VmValue::Int(0)});
  };
  const auto missing = open("/data/data/test.game/databases/missing.db", 0);
  REQUIRE(missing.exception.IsValid());
  CHECK(fixture.linker.Class(missing.exception_class).descriptor ==
        "Landroid/database/sqlite/SQLiteCantOpenDatabaseException;");
  CHECK_THROWS_AS(static_cast<void>(fixture.vfs.Stat(
                      "/data/data/test.game/databases/missing.db")),
                  VfsError);
  const auto created =
      open("/data/data/test.game/databases/flags.db", 0x10000001);
  REQUIRE_FALSE(created.exception.IsValid());
  fixture.Static("Landroid/database/sqlite/SQLiteConnection;", "nativeClose",
                 "(I)V", {VmValue::Int(created.value.AsInt())});
  const auto readonly = open("/data/data/test.game/databases/flags.db", 1);
  REQUIRE_FALSE(readonly.exception.IsValid());
  fixture.Static("Landroid/database/sqlite/SQLiteConnection;", "nativeClose",
                 "(I)V", {VmValue::Int(readonly.value.AsInt())});
}

TEST_CASE("DVM-186 prepared statement tokens bind reset cancel and close") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto connection = database::Connection::Open(vfs, "/data/statements.db");
  connection->Execute("CREATE TABLE values_table (value INTEGER, text TEXT)");
  const auto insert =
      connection->Prepare("INSERT INTO values_table VALUES (?, ?)");
  CHECK(connection->ParameterCount(insert) == 2);
  CHECK(connection->IsReadOnly(insert) == false);
  connection->Bind(insert, 1, std::int64_t{7});
  connection->Bind(insert, 2, std::string{"first"});
  connection->Execute(insert);
  connection->Reset(insert);
  connection->Bind(insert, 1, std::int64_t{9});
  connection->Bind(insert, 2, std::string{"second"});
  connection->Execute(insert);
  connection->Finalize(insert);
  CHECK_THROWS(connection->Finalize(insert));

  const auto query = connection->Prepare(
      "SELECT value, text FROM values_table ORDER BY value");
  CHECK(connection->IsReadOnly(query));
  CHECK(connection->ColumnCount(query) == 2);
  CHECK(connection->ColumnName(query, 1) == "text");
  const auto result = connection->Query(query);
  CHECK(result.rows.size() == 2);
  connection->Finalize(query);

  std::atomic<bool> cancelled{};
  std::thread worker([&] {
    try {
      static_cast<void>(connection->Query(
          "WITH RECURSIVE n(x) AS (VALUES(0) UNION ALL SELECT x+1 FROM n "
          "WHERE x<100000000) SELECT sum(x) FROM n"));
    } catch (const std::runtime_error &) {
      cancelled = true;
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  connection->Cancel();
  worker.join();
  CHECK(cancelled);
  connection->Close();
  CHECK_FALSE(connection->IsOpen());
  CHECK_THROWS(static_cast<void>(connection->Prepare("SELECT 1")));
}

TEST_CASE("DVM-186 CursorWindow enforces payload capacity and stale tokens") {
  NetworkSqliteVm fixture;
  const auto token = fixture
                         .Static("Landroid/database/CursorWindow;", "nativeCreate",
                                 "(Ljava/lang/String;I)I",
                                 {VmValue::Ref(fixture.vm.NewStringUtf8("budget")),
                                  VmValue::Int(64)})
                         .AsInt();
  CHECK(fixture
            .Static("Landroid/database/CursorWindow;", "nativeSetNumColumns",
                    "(II)Z", {VmValue::Int(token), VmValue::Int(1)})
            .AsInt() == 1);
  CHECK(fixture.Static("Landroid/database/CursorWindow;", "nativeAllocRow",
                       "(I)Z", {VmValue::Int(token)})
            .AsInt() == 1);
  CHECK(fixture
            .Static("Landroid/database/CursorWindow;", "nativePutString",
                    "(ILjava/lang/String;II)Z",
                    {VmValue::Int(token),
                     VmValue::Ref(fixture.vm.NewStringUtf8(std::string(40, 'a'))),
                     VmValue::Int(0), VmValue::Int(0)})
            .AsInt() == 1);
  const auto buffer = fixture.New("Landroid/database/CharArrayBuffer;", "(I)V",
                                  {VmValue::Int(1)});
  fixture.Static(
      "Landroid/database/CursorWindow;", "nativeCopyStringToBuffer",
      "(IIILandroid/database/CharArrayBuffer;)V",
      {VmValue::Int(token), VmValue::Int(0), VmValue::Int(0),
       VmValue::Ref(buffer)});
  const auto buffer_class = fixture.model.ObjectClass(buffer);
  const auto data_field = fixture.linker.FindFieldRecursive(
      buffer_class, "data", "[C");
  const auto size_field = fixture.linker.FindFieldRecursive(
      buffer_class, "sizeCopied", "I");
  REQUIRE(data_field.has_value());
  REQUIRE(size_field.has_value());
  const auto buffer_slots = fixture.model.InstanceSlots(buffer);
  const auto chars = VmObjectRef(
      buffer_slots[fixture.linker.Field(*data_field).slot].bits);
  CHECK(fixture.model.ArrayLength(chars) == 40);
  CHECK(fixture.model.GetPrimitiveElement(chars, 0) == 'a');
  CHECK(buffer_slots[fixture.linker.Field(*size_field).slot].bits == 40);
  CHECK(fixture
            .Static("Landroid/database/CursorWindow;", "nativePutString",
                    "(ILjava/lang/String;II)Z",
                    {VmValue::Int(token),
                     VmValue::Ref(fixture.vm.NewStringUtf8(std::string(60, 'b'))),
                     VmValue::Int(0), VmValue::Int(0)})
            .AsInt() == 0);
  CHECK(fixture.Static("Landroid/database/CursorWindow;", "nativeAllocRow",
                       "(I)Z", {VmValue::Int(token)})
            .AsInt() == 0);
  fixture.Static("Landroid/database/CursorWindow;", "nativeFreeLastRow", "(I)V",
                 {VmValue::Int(token)});
  CHECK(fixture.Static("Landroid/database/CursorWindow;", "nativeAllocRow",
                       "(I)Z", {VmValue::Int(token)})
            .AsInt() == 1);
  fixture.Static("Landroid/database/CursorWindow;", "nativeDispose", "(I)V",
                 {VmValue::Int(token)});
  CHECK(fixture
            .StaticOutcome("Landroid/database/CursorWindow;", "nativeGetNumRows",
                           "(I)I", {VmValue::Int(token)})
            .exception.IsValid());
}

TEST_CASE("DVM-186 CursorWindow streams required rows within its budget") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto connection = database::Connection::Open(vfs, "/data/window.db");
  connection->Execute("CREATE TABLE values_table (value TEXT)");
  for (int index = 0; index < 8; ++index)
    connection->Execute("INSERT INTO values_table VALUES (?)",
                        std::array<database::Value, 1>{
                            std::string(20, static_cast<char>('a' + index))});
  const auto query =
      connection->Prepare("SELECT value FROM values_table ORDER BY rowid");
  const auto partial = connection->QueryWindow(query, 0, 5, false, 80);
  CHECK(partial.start_position == 4);
  CHECK(partial.total_rows == 7);
  REQUIRE(partial.rows.size() == 2);
  CHECK(std::get<std::string>(partial.rows[1][0]) == std::string(20, 'f'));
  const auto counted = connection->QueryWindow(query, 0, 5, true, 80);
  CHECK(counted.start_position == 4);
  CHECK(counted.total_rows == 8);
  REQUIRE(counted.rows.size() == 2);
  const auto oversized = connection->QueryWindow(query, 0, 0, true, 16);
  CHECK(oversized.total_rows == 8);
  CHECK(oversized.rows.empty());
  connection->Finalize(query);
}

TEST_CASE("DVM-186 hot rollback journal recovers a crash snapshot") {
  std::vector<std::byte> database_image;
  std::vector<std::byte> journal_image;
  {
    VirtualFileSystem crashed;
    crashed.CreateDirectory("/data");
    auto connection = database::Connection::Open(crashed, "/data/recover.db");
    connection->Execute("CREATE TABLE values_table (value INTEGER)");
    connection->Execute("INSERT INTO values_table VALUES (1)");
    connection->Execute("BEGIN IMMEDIATE");
    connection->Execute("UPDATE values_table SET value=2");
    database_image = ReadVfsFile(crashed, "/data/recover.db");
    journal_image = ReadVfsFile(crashed, "/data/recover.db-journal");
    CHECK_FALSE(journal_image.empty());
  }

  VirtualFileSystem recovered;
  recovered.CreateDirectory("/data");
  WriteVfsFile(recovered, "/data/recover.db", database_image);
  WriteVfsFile(recovered, "/data/recover.db-journal", journal_image);
  auto connection = database::Connection::Open(recovered, "/data/recover.db");
  CHECK(std::get<std::int64_t>(
            connection->Query("SELECT value FROM values_table").rows[0][0]) ==
        1);
  CHECK(recovered.Stat("/data/recover.db-journal").size ==
        journal_image.size());
}

TEST_CASE("DVM-186 ENOSPC aborts a write without committing partial state") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  std::atomic<bool> full{};
  auto connection = database::Connection::Open(
      vfs, "/data/full.db",
      [&full](const database::IoOperation operation,
              const std::string_view) -> std::optional<int> {
        if (full && operation == database::IoOperation::write)
          return 28;
        return std::nullopt;
      });
  connection->Execute("CREATE TABLE values_table (value TEXT)");
  full = true;
  CHECK_THROWS_WITH_AS(connection->Execute(
                           "INSERT INTO values_table VALUES ('not-committed')"),
                       doctest::Contains("database or disk is full"),
                       std::runtime_error);
  full = false;
  CHECK(std::get<std::int64_t>(connection->Query(
                                   "SELECT count(*) FROM values_table")
                                   .rows[0][0]) == 0);
  CHECK(std::get<std::string>(connection->Query("PRAGMA integrity_check")
                                  .rows[0][0]) == "ok");
}

TEST_CASE("DVM-186 interrupted commit leaves a hot journal that rolls back") {
  VirtualFileSystem live;
  live.CreateDirectory("/data");
  std::atomic<bool> armed{};
  std::vector<std::byte> database_image;
  std::vector<std::byte> journal_image;
  auto connection = database::Connection::Open(
      live, "/data/interrupted.db",
      [&](const database::IoOperation operation,
          const std::string_view path) -> std::optional<int> {
        if (armed && operation == database::IoOperation::sync &&
            path == "/data/interrupted.db") {
          database_image = ReadVfsFile(live, "/data/interrupted.db");
          journal_image =
              ReadVfsFile(live, "/data/interrupted.db-journal");
          armed = false;
          return 5;
        }
        return std::nullopt;
      });
  connection->Execute("CREATE TABLE values_table (value INTEGER)");
  connection->Execute("INSERT INTO values_table VALUES (1)");
  connection->Execute("BEGIN IMMEDIATE");
  connection->Execute("UPDATE values_table SET value=2");
  armed = true;
  CHECK_THROWS(connection->Execute("COMMIT"));
  REQUIRE_FALSE(database_image.empty());
  REQUIRE_FALSE(journal_image.empty());

  VirtualFileSystem recovered;
  recovered.CreateDirectory("/data");
  WriteVfsFile(recovered, "/data/interrupted.db", database_image);
  WriteVfsFile(recovered, "/data/interrupted.db-journal", journal_image);
  auto reopened =
      database::Connection::Open(recovered, "/data/interrupted.db");
  CHECK(std::get<std::int64_t>(
            reopened->Query("SELECT value FROM values_table").rows[0][0]) == 1);
  CHECK(std::get<std::string>(reopened->Query("PRAGMA integrity_check")
                                  .rows[0][0]) == "ok");
  CHECK_THROWS_AS(
      static_cast<void>(recovered.Stat("/data/interrupted.db-journal")),
      VfsError);
}

TEST_CASE("DVM-186 independent sqlite tool interoperates in both directions") {
  const std::filesystem::path sqlite =
      R"(D:\01_software\android-sdk\platform-tools\sqlite3.exe)";
  if (!std::filesystem::exists(sqlite)) {
    MESSAGE("independent Android SDK sqlite3.exe is unavailable");
    return;
  }
  const auto root = std::filesystem::temp_directory_path() /
                    ("ogplay-sqlite-interop-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::filesystem::create_directories(root);
  const auto cleanup = std::unique_ptr<void, std::function<void(void *)>>(
      reinterpret_cast<void *>(1), [&root](void *) {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
      });
  const auto host = root / "host.db";
  const auto output = root / "result.txt";
  const auto command = [&](const std::filesystem::path &database,
                           const std::string_view sql,
                           const bool capture = false) {
    auto text = sqlite.string() + " \"" + database.string() + "\" \"" +
                std::string(sql) + "\"";
    if (capture)
      text += " > \"" + output.string() + "\"";
    return std::system(text.c_str());
  };

  REQUIRE(command(host,
                  "CREATE TABLE interop(id INTEGER PRIMARY KEY, text TEXT, "
                  "payload BLOB, number INTEGER); INSERT INTO interop VALUES"
                  "(1,CAST(X'E4BDA0E5A5BD' AS TEXT),X'0001FF',"
                  "9223372036854775807);") == 0);
  VirtualFileSystem imported;
  imported.CreateDirectory("/data");
  WriteVfsFile(imported, "/data/interop.db", ReadHostFile(host));
  auto from_tool =
      database::Connection::Open(imported, "/data/interop.db");
  const auto first = from_tool->Query(
      "SELECT text,payload,number FROM interop WHERE id=1");
  CHECK(std::get<std::string>(first.rows[0][0]) == "你好");
  CHECK(std::get<std::vector<std::byte>>(first.rows[0][1]) ==
        std::vector<std::byte>{std::byte{0}, std::byte{1}, std::byte{255}});
  CHECK(std::get<std::int64_t>(first.rows[0][2]) ==
        9223372036854775807LL);
  const std::array<database::Value, 3> inserted{
      std::string{"ogplay"},
      std::vector<std::byte>{std::byte{0xaa}, std::byte{0x55}},
      std::int64_t{-7}};
  from_tool->Execute(
      "INSERT INTO interop(text,payload,number) VALUES(?,?,?)", inserted);
  from_tool->Close();
  WriteHostFile(host, ReadVfsFile(imported, "/data/interop.db"));
  REQUIRE(command(host,
                  "SELECT integrity_check FROM pragma_integrity_check; SELECT "
                  "text,hex(payload),number FROM interop WHERE id=2;",
                  true) == 0);
  const auto result_bytes = ReadHostFile(output);
  const std::string result(reinterpret_cast<const char *>(result_bytes.data()),
                           result_bytes.size());
  CHECK(result.find("ok") != std::string::npos);
  CHECK(result.find("ogplay|AA55|-7") != std::string::npos);

  VirtualFileSystem created;
  created.CreateDirectory("/data");
  auto by_ogplay = database::Connection::Open(created, "/data/reverse.db");
  by_ogplay->Execute("CREATE TABLE reverse_path(value TEXT)");
  by_ogplay->Execute("INSERT INTO reverse_path VALUES('before')");
  by_ogplay->Close();
  const auto reverse_host = root / "reverse.db";
  WriteHostFile(reverse_host, ReadVfsFile(created, "/data/reverse.db"));
  REQUIRE(command(reverse_host,
                  "UPDATE reverse_path SET value='after'; PRAGMA "
                  "integrity_check;") == 0);
  VirtualFileSystem round_trip;
  round_trip.CreateDirectory("/data");
  WriteVfsFile(round_trip, "/data/reverse.db", ReadHostFile(reverse_host));
  auto reopened =
      database::Connection::Open(round_trip, "/data/reverse.db");
  CHECK(std::get<std::string>(
            reopened->Query("SELECT value FROM reverse_path").rows[0][0]) ==
        "after");
  CHECK(std::get<std::string>(reopened->Query("PRAGMA integrity_check")
                                  .rows[0][0]) == "ok");

  VirtualFileSystem crashed;
  crashed.CreateDirectory("/data");
  std::atomic<bool> capture_hot{};
  std::vector<std::byte> crash_database;
  std::vector<std::byte> crash_journal;
  auto crashing = database::Connection::Open(
      crashed, "/data/crash.db",
      [&](const database::IoOperation operation,
          const std::string_view path) -> std::optional<int> {
        if (capture_hot && operation == database::IoOperation::sync &&
            path == "/data/crash.db") {
          crash_database = ReadVfsFile(crashed, "/data/crash.db");
          crash_journal = ReadVfsFile(crashed, "/data/crash.db-journal");
          capture_hot = false;
          return 5;
        }
        return std::nullopt;
      });
  crashing->Execute("CREATE TABLE crash_value(value INTEGER)");
  crashing->Execute("INSERT INTO crash_value VALUES(1)");
  crashing->Execute("BEGIN IMMEDIATE");
  crashing->Execute("UPDATE crash_value SET value=2");
  capture_hot = true;
  CHECK_THROWS(crashing->Execute("COMMIT"));
  REQUIRE_FALSE(crash_database.empty());
  REQUIRE_FALSE(crash_journal.empty());
  const auto crash_host = root / "crash.db";
  const auto crash_host_journal = root / "crash.db-journal";
  WriteHostFile(crash_host, crash_database);
  WriteHostFile(crash_host_journal, crash_journal);
  REQUIRE(command(crash_host,
                  "SELECT value FROM crash_value; PRAGMA integrity_check;",
                  true) == 0);
  const auto recovered_bytes = ReadHostFile(output);
  const std::string recovered_text(
      reinterpret_cast<const char *>(recovered_bytes.data()),
      recovered_bytes.size());
  CHECK(recovered_text.find("1") != std::string::npos);
  CHECK(recovered_text.find("ok") != std::string::npos);
  CHECK_FALSE(std::filesystem::exists(crash_host_journal));
}

TEST_CASE("DVM-186 close cancels active work and teardown is idempotent") {
  VirtualFileSystem vfs;
  vfs.CreateDirectory("/data");
  auto connection = database::Connection::Open(vfs, "/data/close-race.db");
  auto context = std::make_shared<DexVmAndroidContext>();
  context->sqlite_connections.emplace(1U, connection);
  std::atomic<bool> entered{};
  std::atomic<bool> interrupted{};
  std::thread worker([&] {
    entered = true;
    try {
      static_cast<void>(connection->Query(
          "WITH RECURSIVE n(x) AS (VALUES(0) UNION ALL SELECT x+1 FROM n "
          "WHERE x<100000000) SELECT sum(x) FROM n"));
    } catch (const std::runtime_error &) {
      interrupted = true;
    }
  });
  while (!entered)
    std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  ReleaseAndroidDatabaseResources(context);
  worker.join();
  CHECK(interrupted);
  CHECK_FALSE(connection->IsOpen());
  CHECK(context->sqlite_connections.empty());
  CHECK_NOTHROW(ReleaseAndroidDatabaseResources(context));
  CHECK_NOTHROW(connection->Close());
}

TEST_CASE("DVM-186 original database path runs on both interpreters") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    NetworkSqliteVm fixture(backend);
    const auto database =
        fixture
            .Static("Landroid/database/sqlite/SQLiteDatabase;",
                    "openOrCreateDatabase",
                    "(Ljava/lang/String;Landroid/database/sqlite/"
                    "SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/"
                    "SQLiteDatabase;",
                    {VmValue::Ref(fixture.vm.NewStringUtf8(
                         "/data/data/test.game/databases/backends.db")),
                     VmValue::Ref(VmObjectRef{})})
            .ref;
    fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8(
                   "CREATE TABLE backend_value(value INTEGER)"))});
    fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8(
                   "INSERT INTO backend_value VALUES(19)"))});
    const auto cursor =
        fixture
            .On(database, "rawQuery",
                "(Ljava/lang/String;[Ljava/lang/String;)Landroid/database/"
                "Cursor;",
                {VmValue::Ref(fixture.vm.NewStringUtf8(
                     "SELECT value FROM backend_value")),
                 VmValue::Ref(VmObjectRef{})})
            .ref;
    CHECK(fixture.On(cursor, "moveToFirst", "()Z").AsInt() == 1);
    CHECK(fixture.On(cursor, "getLong", "(I)J", {VmValue::Int(0)}).AsLong() ==
          19);
    fixture.On(cursor, "close", "()V");
    fixture.On(database, "close", "()V");
  }
}

TEST_CASE("DVM-186 real guest thread query is cancelled by teardown") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    NetworkSqliteVm fixture(backend);
    fixture.thread_connection = database::Connection::Open(
        fixture.vfs, "/data/data/test.game/databases/thread-close.db");
    fixture.context->sqlite_connections.emplace(77U,
                                                fixture.thread_connection);
    const auto worker =
        fixture.vm.NewIntrinsicInstance("Ltest/SqliteWorker;");
    fixture.threads.Start(worker, "sqlite-worker",
                          fixture.threads.AllocateThreadId());
    while (!fixture.thread_query_entered)
      std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ReleaseAndroidDatabaseResources(fixture.context);
    fixture.threads.Join(worker);
    CHECK(fixture.thread_query_interrupted);
    CHECK_FALSE(fixture.thread_connection->IsOpen());
    CHECK(fixture.context->sqlite_connections.empty());
  }
}

TEST_CASE("DVM-186 GC marking competes safely with connection close") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    NetworkSqliteVm fixture(backend);
    const auto database =
        fixture
            .Static("Landroid/database/sqlite/SQLiteDatabase;",
                    "openOrCreateDatabase",
                    "(Ljava/lang/String;Landroid/database/sqlite/"
                    "SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/"
                    "SQLiteDatabase;",
                    {VmValue::Ref(fixture.vm.NewStringUtf8(
                         "/data/data/test.game/databases/gc-close.db")),
                     VmValue::Ref(VmObjectRef{})})
            .ref;
    REQUIRE(fixture.context->sqlite_connections.size() == 1);
    const auto connection = fixture.context->sqlite_connections.begin()->second;
    fixture.vm.SetGcIntegration(
        {{}, {}, [database](const VmRootVisitor &visit) { visit(database); }});
    std::atomic<bool> entered{};
    std::atomic<bool> interrupted{};
    std::thread query([&] {
      entered = true;
      try {
        static_cast<void>(connection->Query(
            "WITH RECURSIVE n(x) AS (VALUES(0) UNION ALL SELECT x+1 FROM n "
            "WHERE x<100000000) SELECT sum(x) FROM n"));
      } catch (const std::runtime_error &) {
        interrupted = true;
      }
    });
    while (!entered)
      std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::thread closer([&] { connection->Close(); });
    const auto marked = fixture.vm.MarkReachable();
    closer.join();
    query.join();
    CHECK(marked.IsMarked(database));
    CHECK(interrupted);
    CHECK_FALSE(connection->IsOpen());
    CHECK_NOTHROW(ReleaseAndroidDatabaseResources(fixture.context));
  }
}

TEST_CASE("DVM-88 network runtime is offline unless explicitly injected") {
  NetworkRuntime runtime;
  CHECK_THROWS_WITH_AS(
      ([&] { static_cast<void>(runtime.Resolve("game.test")); }()),
      "network policy is offline", NetworkRuntimeError);

  FakeNetwork transport;
  runtime.Configure({true, true, true, {"game.test"}}, &transport);
  CHECK(runtime.Resolve("game.test") ==
        std::vector<std::string>{"203.0.113.7"});
  CHECK_THROWS_AS(
      ([&] { static_cast<void>(runtime.Resolve("tracker.test")); }()),
      NetworkRuntimeError);

  runtime.CreateSocket(VmObjectRef(1), true);
  runtime.Connect(VmObjectRef(1), {"game.test", "203.0.113.7", 443});
  runtime.BindStream(VmObjectRef(2), VmObjectRef(1), true);
  runtime.BindStream(VmObjectRef(3), VmObjectRef(1), false);
  const std::array request{std::byte{'p'}, std::byte{'i'}, std::byte{'n'},
                           std::byte{'g'}};
  runtime.WriteStream(VmObjectRef(2), request);
  CHECK(runtime.ReadStream(VmObjectRef(3), 4) ==
        std::vector<std::byte>{std::byte{'p'}, std::byte{'o'}, std::byte{'n'},
                               std::byte{'g'}});
  CHECK(transport.connected == "game.test:443");
  CHECK_FALSE(transport.used_tls);
  runtime.CloseSocket(VmObjectRef(1));
  CHECK(transport.closed == 7);
}

TEST_CASE("DVM-88 network runtime closes live channels during teardown") {
  FakeNetwork transport;
  {
    NetworkRuntime runtime;
    runtime.Configure({true, false, false, {"game.test"}}, &transport);
    runtime.CreateSocket(VmObjectRef(9));
    runtime.Connect(VmObjectRef(9), {"game.test", "203.0.113.7", 80});
  }
  CHECK(transport.closed == 7);
  CHECK(transport.close_count == 1);
}

TEST_CASE(
    "DVM-152 API 19 InetAddress owns address state and uses bounded DNS") {
  for (const auto backend :
       {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
    NetworkSqliteVm fixture(backend);

    const auto ipv4 =
        fixture
            .Static("Ljava/net/InetAddress;", "getByName",
                    "(Ljava/lang/String;)Ljava/net/InetAddress;",
                    {VmValue::Ref(fixture.vm.NewStringUtf8("192.0.2.9"))})
            .ref;
    CHECK(fixture.linker.Class(fixture.model.ObjectClass(ipv4)).descriptor ==
          "Ljava/net/Inet4Address;");
    CHECK(fixture.vm.StringUtf8(
              fixture.On(ipv4, "getHostAddress", "()Ljava/lang/String;").ref) ==
          "192.0.2.9");
    const auto endpoint = fixture.New("Ljava/net/InetSocketAddress;",
                                      "(Ljava/net/InetAddress;I)V",
                                      {VmValue::Ref(ipv4), VmValue::Int(8080)});
    const auto endpoint_roots =
        fixture.vm.ProtectReferences(std::array{ipv4, endpoint});
    static_cast<void>(fixture.vm.CollectGarbage("dvm152_endpoint_fields"));
    CHECK(fixture.On(endpoint, "getAddress", "()Ljava/net/InetAddress;").ref ==
          ipv4);
    CHECK(fixture.On(endpoint, "getPort", "()I").AsInt() == 8080);

    const auto ipv6 =
        fixture
            .Static("Ljava/net/InetAddress;", "getByName",
                    "(Ljava/lang/String;)Ljava/net/InetAddress;",
                    {VmValue::Ref(fixture.vm.NewStringUtf8("2001:db8::1"))})
            .ref;
    CHECK(fixture.linker.Class(fixture.model.ObjectClass(ipv6)).descriptor ==
          "Ljava/net/Inet6Address;");
    CHECK(fixture.vm.StringUtf8(
              fixture.On(ipv6, "getHostAddress", "()Ljava/lang/String;").ref) ==
          "2001:db8::1");

    FakeNetwork transport;
    fixture.vm.Network().Configure({true, false, false, {"game.test"}},
                                   &transport);
    const auto posix = fixture.New("Llibcore/io/Posix;");
    const auto hints = fixture.New("Llibcore/io/StructAddrinfo;");
    const auto addresses =
        fixture
            .On(posix, "getaddrinfo",
                "(Ljava/lang/String;Llibcore/io/StructAddrinfo;)[Ljava/net/"
                "InetAddress;",
                {VmValue::Ref(fixture.vm.NewStringUtf8("game.test")),
                 VmValue::Ref(hints)})
            .ref;
    REQUIRE(fixture.model.ArrayLength(addresses) == 1);
    const auto resolved = fixture.model.GetObjectElement(addresses, 0);
    CHECK(transport.resolved == "game.test");
    CHECK(fixture.vm.StringUtf8(
              fixture.On(resolved, "getHostAddress", "()Ljava/lang/String;")
                  .ref) == "203.0.113.7");
  }
}

TEST_CASE(
    "DVM-153 API 19 URI parses creates normalizes and resolves in BootDex") {
  for (const auto backend :
       {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
    NetworkSqliteVm fixture(backend);
    const auto input = fixture.vm.NewStringUtf8(
        "http://chillingo-terms.chillingocloud.com/a/../getLatest?q=x%20y#f");
    const auto uri =
        fixture
            .Static("Ljava/net/URI;", "create",
                    "(Ljava/lang/String;)Ljava/net/URI;", {VmValue::Ref(input)})
            .ref;
    CHECK(fixture.vm.StringUtf8(
              fixture.On(uri, "getHost", "()Ljava/lang/String;").ref) ==
          "chillingo-terms.chillingocloud.com");
    CHECK(fixture.vm.StringUtf8(
              fixture.On(uri, "getPath", "()Ljava/lang/String;").ref) ==
          "/a/../getLatest");
    CHECK(fixture.vm.StringUtf8(
              fixture.On(uri, "getQuery", "()Ljava/lang/String;").ref) ==
          "q=x y");
    const auto normalized =
        fixture.On(uri, "normalize", "()Ljava/net/URI;").ref;
    CHECK(fixture.vm.StringUtf8(
              fixture.On(normalized, "toString", "()Ljava/lang/String;").ref) ==
          "http://chillingo-terms.chillingocloud.com/getLatest?q=x%20y#f");
    const auto resolved =
        fixture
            .On(normalized, "resolve", "(Ljava/lang/String;)Ljava/net/URI;",
                {VmValue::Ref(fixture.vm.NewStringUtf8("terms.json"))})
            .ref;
    CHECK(fixture.vm.StringUtf8(
              fixture.On(resolved, "toString", "()Ljava/lang/String;").ref) ==
          "http://chillingo-terms.chillingocloud.com/terms.json");
    CHECK(
        fixture.On(uri, "equals", "(Ljava/lang/Object;)Z", {VmValue::Ref(uri)})
            .AsInt() == 1);

    const auto invalid = fixture.StaticOutcome(
        "Ljava/net/URI;", "create", "(Ljava/lang/String;)Ljava/net/URI;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("http://bad host/"))});
    REQUIRE(invalid.exception.IsValid());
    CHECK(fixture.linker.Class(invalid.exception_class).descriptor ==
          "Ljava/lang/IllegalArgumentException;");
  }
}

TEST_CASE("ProxySelector reports no process-wide proxy service") {
  for (const auto backend :
       {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
    NetworkSqliteVm fixture(backend);
    const auto selector = fixture.Static(
        "Ljava/net/ProxySelector;", "getDefault", "()Ljava/net/ProxySelector;");
    CHECK_FALSE(selector.ref.IsValid());
  }
}

TEST_CASE("DVM-88 URL form codecs match API 19 UTF-8 behavior") {
  for (const auto backend :
       {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
    NetworkSqliteVm fixture(backend);
    const auto utf8 = fixture.vm.NewStringUtf8("UTF-8");
    const std::u16string clear{u'a', u' ',    u'b',    u'+',   u'c',
                               u'/', 0x00e9U, 0xd83dU, 0xde00U};
    const auto input = fixture.model.NewString(clear);
    const auto encoded =
        fixture
            .Static("Ljava/net/URLEncoder;", "encode",
                    "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                    {VmValue::Ref(input), VmValue::Ref(utf8)})
            .ref;
    CHECK(fixture.vm.StringUtf8(encoded) == "a+b%2Bc%2F%C3%A9%F0%9F%98%80");

    const auto decoded =
        fixture
            .Static("Ljava/net/URLDecoder;", "decode",
                    "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                    {VmValue::Ref(encoded), VmValue::Ref(utf8)})
            .ref;
    CHECK(fixture.model.StringValue(decoded) == clear);

    const auto default_encoded =
        fixture
            .Static("Ljava/net/URLEncoder;", "encode",
                    "(Ljava/lang/String;)Ljava/lang/String;",
                    {VmValue::Ref(fixture.vm.NewStringUtf8("x y"))})
            .ref;
    CHECK(fixture.vm.StringUtf8(default_encoded) == "x+y");
    const auto default_decoded =
        fixture
            .Static("Ljava/net/URLDecoder;", "decode",
                    "(Ljava/lang/String;)Ljava/lang/String;",
                    {VmValue::Ref(default_encoded)})
            .ref;
    CHECK(fixture.vm.StringUtf8(default_decoded) == "x y");

    const auto unchanged = fixture.vm.NewStringUtf8("plain");
    CHECK(fixture
              .Static("Ljava/net/URLDecoder;", "decode",
                      "(Ljava/lang/String;)Ljava/lang/String;",
                      {VmValue::Ref(unchanged)})
              .ref == unchanged);

    const auto invalid = fixture.StaticOutcome(
        "Ljava/net/URLDecoder;", "decode",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("bad%2")), VmValue::Ref(utf8)});
    REQUIRE(invalid.exception.IsValid());
    CHECK(fixture.linker.Class(invalid.exception_class).descriptor ==
          "Ljava/lang/IllegalArgumentException;");

    const auto unsupported = fixture.StaticOutcome(
        "Ljava/net/URLEncoder;", "encode",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        {VmValue::Ref(input),
         VmValue::Ref(fixture.vm.NewStringUtf8("UTF-16"))});
    REQUIRE(unsupported.exception.IsValid());
    CHECK(fixture.linker.Class(unsupported.exception_class).descriptor ==
          "Ljava/io/UnsupportedEncodingException;");
  }
}

TEST_CASE(
    "DVM-88 URL parsing is value-only and offline failure starts at I/O") {
  for (const auto backend :
       {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
    CAPTURE(backend == InterpreterBackend::threaded ? "threaded" : "switch");
    FakeNetwork transport;
    NetworkSqliteVm fixture(backend);
    const auto url = fixture.New(
        "Ljava/net/URL;", "(Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8(
            "  HTTPS://user:pass@example.com:8443/a%20b?q=1#frag  "))});
    const auto text = [&](const char *name) {
      return fixture.vm.StringUtf8(
          fixture.On(url, name, "()Ljava/lang/String;").ref);
    };
    CHECK(text("getProtocol") == "https");
    CHECK(text("getAuthority") == "user:pass@example.com:8443");
    CHECK(text("getUserInfo") == "user:pass");
    CHECK(text("getHost") == "example.com");
    CHECK(fixture.On(url, "getPort", "()I").AsInt() == 8443);
    CHECK(fixture.On(url, "getDefaultPort", "()I").AsInt() == 443);
    CHECK(text("getPath") == "/a%20b");
    CHECK(text("getQuery") == "q=1");
    CHECK(text("getFile") == "/a%20b?q=1");
    CHECK(text("getRef") == "frag");
    CHECK(text("toExternalForm") ==
          "https://user:pass@example.com:8443/a%20b?q=1#frag");
    CHECK(text("toString") ==
          "https://user:pass@example.com:8443/a%20b?q=1#frag");

    const auto file_url = fixture.New(
        "Ljava/net/URL;", "(Ljava/lang/String;)V",
        {VmValue::Ref(fixture.vm.NewStringUtf8("file:///data/save.dat"))});
    CHECK(fixture.vm
              .StringUtf8(
                  fixture.On(file_url, "getAuthority", "()Ljava/lang/String;")
                      .ref)
              .empty());
    CHECK(fixture.vm.StringUtf8(
              fixture.On(file_url, "toString", "()Ljava/lang/String;").ref) ==
          "file:///data/save.dat");
    CHECK(fixture.On(file_url, "getDefaultPort", "()I").AsInt() == -1);
    const auto file_io =
        fixture.OnOutcome(file_url, "openStream", "()Ljava/io/InputStream;");
    REQUIRE(file_io.exception.IsValid());
    CHECK(fixture.linker.Class(file_io.exception_class).descriptor ==
          "Ljava/io/IOException;");

    const auto offline =
        fixture.OnOutcome(url, "openConnection", "()Ljava/net/URLConnection;");
    REQUIRE(offline.exception.IsValid());
    CHECK(fixture.linker.Class(offline.exception_class).descriptor ==
          "Ljava/net/UnknownHostException;");
    CHECK(offline.exception_message ==
          "network policy is offline for example.com");

    fixture.vm.Network().Configure({true, true, false, {"example.com"}},
                                   &transport);
    const auto http_unimplemented =
        fixture.OnOutcome(url, "openConnection", "()Ljava/net/URLConnection;");
    REQUIRE(http_unimplemented.exception.IsValid());
    CHECK(fixture.linker.Class(http_unimplemented.exception_class).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");

    const auto url_class = fixture.linker.ResolveDescriptor("Ljava/net/URL;");
    const auto constructor = fixture.linker.FindDirectMethod(
        url_class, "<init>", "(Ljava/lang/String;)V");
    REQUIRE(constructor.has_value());
    const auto malformed_url =
        fixture.vm.NewIntrinsicInstance("Ljava/net/URL;");
    const auto malformed = fixture.vm.Call(
        *constructor,
        std::array{VmValue::Ref(malformed_url),
                   VmValue::Ref(fixture.vm.NewStringUtf8("not a URL"))});
    REQUIRE(malformed.exception.IsValid());
    CHECK(fixture.linker.Class(malformed.exception_class).descriptor ==
          "Ljava/net/MalformedURLException;");
  }
}

TEST_CASE("DVM-88 SocketFactory exposes policy-gated common creation") {
  FakeNetwork transport;
  NetworkSqliteVm fixture;
  fixture.vm.Network().Configure({true, false, false, {"game.test"}},
                                 &transport);
  const auto factory = fixture
                           .Static("Ljavax/net/SocketFactory;", "getDefault",
                                   "()Ljavax/net/SocketFactory;")
                           .ref;
  const auto socket =
      fixture
          .On(factory, "createSocket", "(Ljava/lang/String;I)Ljava/net/Socket;",
              {VmValue::Ref(fixture.vm.NewStringUtf8("game.test")),
               VmValue::Int(80)})
          .ref;
  CHECK(fixture.linker.Class(fixture.model.ObjectClass(socket)).descriptor ==
        "Ljava/net/Socket;");
  CHECK(transport.connected == "game.test:80");
  static_cast<void>(fixture.On(socket, "close", "()V"));
}

TEST_CASE("DVM-88 ContentValues SQLite query persists through guest VFS") {
  NetworkSqliteVm fixture;
  const auto path =
      fixture.vm.NewStringUtf8("/data/data/test.game/databases/save.db");
  auto database =
      fixture
          .Static("Landroid/database/sqlite/SQLiteDatabase;",
                  "openOrCreateDatabase",
                  "(Ljava/lang/String;Landroid/database/sqlite/"
                  "SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/"
                  "SQLiteDatabase;",
                  {VmValue::Ref(path), VmValue::Ref(VmObjectRef(0))})
          .ref;
  fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
             {VmValue::Ref(fixture.vm.NewStringUtf8(
                 "CREATE TABLE saves (name TEXT, score INTEGER)"))});

  const auto values = fixture.New("Landroid/content/ContentValues;");
  fixture.On(values, "put", "(Ljava/lang/String;Ljava/lang/String;)V",
             {VmValue::Ref(fixture.vm.NewStringUtf8("name")),
              VmValue::Ref(fixture.vm.NewStringUtf8("slot-a"))});
  const auto score =
      fixture.New("Ljava/lang/Integer;", "(I)V", {VmValue::Int(42)});
  fixture.On(
      values, "put", "(Ljava/lang/String;Ljava/lang/Integer;)V",
      {VmValue::Ref(fixture.vm.NewStringUtf8("score")), VmValue::Ref(score)});
  CHECK(fixture
            .On(database, "insert",
                "(Ljava/lang/String;Ljava/lang/String;Landroid/content/"
                "ContentValues;)J",
                {VmValue::Ref(fixture.vm.NewStringUtf8("saves")),
                 VmValue::Ref(VmObjectRef(0)), VmValue::Ref(values)})
            .AsLong() == 1);

  fixture.On(database, "close", "()V");
  const auto database_path = "/data/data/test.game/databases/save.db";
  const auto info = fixture.vfs.Stat(database_path);
  CHECK(info.size > 6);
  CHECK(info.writable);
  const auto image = fixture.vfs.Open(database_path, {.read = true});
  std::array<std::byte, 16> header{};
  CHECK(fixture.vfs.Read(image, header) == header.size());
  fixture.vfs.Close(image);
  CHECK(std::string_view(reinterpret_cast<const char *>(header.data()),
                         header.size()) ==
        std::string_view("SQLite format 3\0", 16));

  database = fixture
                 .Static("Landroid/database/sqlite/SQLiteDatabase;",
                         "openOrCreateDatabase",
                         "(Ljava/lang/String;Landroid/database/sqlite/"
                         "SQLiteDatabase$CursorFactory;)Landroid/database/"
                         "sqlite/SQLiteDatabase;",
                         {VmValue::Ref(path), VmValue::Ref(VmObjectRef(0))})
                 .ref;

  const auto cursor =
      fixture
          .On(database, "query",
              "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;[Ljava/"
              "lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
              "String;)Landroid/database/Cursor;",
              {VmValue::Ref(fixture.vm.NewStringUtf8("saves")),
               VmValue::Ref(fixture.Strings({"name", "score"})),
               VmValue::Ref(fixture.vm.NewStringUtf8("name=?")),
               VmValue::Ref(fixture.Strings({"slot-a"})),
               VmValue::Ref(VmObjectRef(0)), VmValue::Ref(VmObjectRef(0)),
               VmValue::Ref(VmObjectRef(0))})
          .ref;
  CHECK(fixture.On(cursor, "getCount", "()I").AsInt() == 1);
  CHECK(fixture.On(cursor, "moveToFirst", "()Z").AsInt() == 1);
  CHECK(
      fixture.vm.StringUtf8(fixture
                                .On(cursor, "getString",
                                    "(I)Ljava/lang/String;", {VmValue::Int(0)})
                                .ref) == "slot-a");
  CHECK(fixture.On(cursor, "getInt", "(I)I", {VmValue::Int(1)}).AsInt() == 42);

  const auto helper = fixture.NewHelper("helper.db", 1);
  const auto helper_database =
      fixture
          .On(helper, "getWritableDatabase",
              "()Landroid/database/sqlite/SQLiteDatabase;")
          .ref;
  fixture.vm.SetGcIntegration(
      {{}, {}, [helper](const VmRootVisitor &visit) { visit(helper); }});
  const auto marked = fixture.vm.MarkReachable();
  CHECK(marked.IsMarked(helper));
  CHECK(marked.IsMarked(helper_database));
}

TEST_CASE("DVM-186 real SQLite rawQuery constraints and transactions") {
  NetworkSqliteVm fixture;
  const auto database =
      fixture
          .Static("Landroid/database/sqlite/SQLiteDatabase;",
                  "openOrCreateDatabase",
                  "(Ljava/lang/String;Landroid/database/sqlite/"
                  "SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/"
                  "SQLiteDatabase;",
                  {VmValue::Ref(fixture.vm.NewStringUtf8(
                       "/data/data/test.game/databases/real.db")),
                   VmValue::Ref(VmObjectRef{})})
          .ref;
  fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
             {VmValue::Ref(fixture.vm.NewStringUtf8(
                 "CREATE TABLE values_table (id INTEGER PRIMARY KEY, "
                 "text_value TEXT UNIQUE, wide INTEGER)"))});
  fixture.On(database, "beginTransaction", "()V");
  fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
             {VmValue::Ref(fixture.vm.NewStringUtf8(
                 "INSERT INTO values_table VALUES (1, 'rollback', "
                 "9223372036854775807)"))});
  fixture.On(database, "endTransaction", "()V");
  auto cursor = fixture
                    .On(database, "rawQuery",
                        "(Ljava/lang/String;[Ljava/lang/String;)Landroid/"
                        "database/Cursor;",
                        {VmValue::Ref(fixture.vm.NewStringUtf8(
                             "SELECT count(*) AS count FROM values_table")),
                         VmValue::Ref(VmObjectRef{})})
                    .ref;
  CHECK(fixture.On(cursor, "moveToFirst", "()Z").AsInt() == 1);
  CHECK(fixture.On(cursor, "getLong", "(I)J", {VmValue::Int(0)}).AsLong() == 0);
  fixture.On(database, "beginTransaction", "()V");
  fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
             {VmValue::Ref(
                 fixture.vm.NewStringUtf8("INSERT INTO values_table VALUES (2, "
                                          "'commit', 9223372036854775807)"))});
  fixture.On(database, "setTransactionSuccessful", "()V");
  fixture.On(database, "endTransaction", "()V");
  cursor =
      fixture
          .On(database, "rawQuery",
              "(Ljava/lang/String;[Ljava/lang/String;)Landroid/database/"
              "Cursor;",
              {VmValue::Ref(fixture.vm.NewStringUtf8(
                   "SELECT text_value, wide FROM values_table WHERE id=?")),
               VmValue::Ref(fixture.Strings({"2"}))})
          .ref;
  CHECK(fixture.On(cursor, "moveToFirst", "()Z").AsInt() == 1);
  CHECK(
      fixture.vm.StringUtf8(fixture
                                .On(cursor, "getString",
                                    "(I)Ljava/lang/String;", {VmValue::Int(0)})
                                .ref) == "commit");
  CHECK(fixture.On(cursor, "getLong", "(I)J", {VmValue::Int(1)}).AsLong() ==
        9223372036854775807LL);
  fixture.On(database, "beginTransaction", "()V");
  fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
             {VmValue::Ref(fixture.vm.NewStringUtf8(
                 "INSERT INTO values_table VALUES (4, 'outer-rollback', 4)"))});
  fixture.On(database, "beginTransaction", "()V");
  fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
             {VmValue::Ref(fixture.vm.NewStringUtf8(
                 "INSERT INTO values_table VALUES (5, 'inner-success', 5)"))});
  fixture.On(database, "setTransactionSuccessful", "()V");
  fixture.On(database, "endTransaction", "()V");
  fixture.On(database, "endTransaction", "()V");
  cursor = fixture
               .On(database, "rawQuery",
                   "(Ljava/lang/String;[Ljava/lang/String;)Landroid/database/"
                   "Cursor;",
                   {VmValue::Ref(fixture.vm.NewStringUtf8(
                        "SELECT count(*) FROM values_table")),
                    VmValue::Ref(VmObjectRef{})})
               .ref;
  CHECK(fixture.On(cursor, "moveToFirst", "()Z").AsInt() == 1);
  CHECK(fixture.On(cursor, "getLong", "(I)J", {VmValue::Int(0)}).AsLong() == 1);
  const auto constraint = fixture.OnOutcome(
      database, "execSQL", "(Ljava/lang/String;)V",
      {VmValue::Ref(fixture.vm.NewStringUtf8(
          "INSERT INTO values_table VALUES (3, 'commit', 1)"))});
  CHECK(constraint.exception.IsValid());
  CHECK(fixture.linker.Class(constraint.exception_class).descriptor ==
        "Landroid/database/sqlite/SQLiteConstraintException;");
}

TEST_CASE("DVM-186 real APK cookie storage handles empty expired and valid databases") {
  const auto app_dex = ReadLocalAngryBirdsDex();
  if (!app_dex.has_value()) {
    MESSAGE("local Angry Birds 2.3.0 APK is unavailable; real APK matrix skipped");
    return;
  }
  const auto run = [&](const std::optional<std::int64_t> expiration) {
    NetworkSqliteVm fixture(InterpreterBackend::switch_dispatch, *app_dex,
                            "com.rovio.angrybirds");
    constexpr std::string_view path =
        "/data/data/com.rovio.angrybirds/databases/cookiedb";
    if (expiration.has_value()) {
      auto database = database::Connection::Open(fixture.vfs, std::string(path));
      database->Execute("CREATE TABLE cookie (name TEXT PRIMARY KEY, "
                        "cookie_content TEXT, expires INTEGER)");
      database->SetUserVersion(2);
      const std::array<database::Value, 3> values{
          std::string(*expiration == 0 ? "expired" : "valid"),
          std::string("{\"name\":\"valid\",\"value\":\"v\","
                      "\"maxage\":3600}"),
          *expiration};
      database->Execute("INSERT INTO cookie VALUES (?, ?, ?)", values);
      database->Close();
    }
    const auto android_context =
        fixture.vm.NewIntrinsicInstance("Landroid/content/Context;");
    const auto logger =
        fixture
            .Static("Lcom/burstly/lib/util/LoggerExt;", "getInstance",
                    "()Lcom/burstly/lib/util/LoggerExt;")
            .ref;
    fixture.On(logger, "setLogLevel", "(I)V", {VmValue::Int(7)});
    const auto storage = fixture.New(
        "Lcom/burstly/lib/network/beans/cookie/SQLiteCookieStorage;",
        "(Landroid/content/Context;)V", {VmValue::Ref(android_context)});
    const auto manager_field = fixture.linker.FindFieldRecursive(
        fixture.model.ObjectClass(storage), "mCookieDatabase",
        "Lcom/burstly/lib/network/beans/cookie/"
        "SQLiteCookieStorage$CookiePersistanceManager;");
    REQUIRE(manager_field.has_value());
    const auto manager = VmObjectRef{static_cast<std::uint32_t>(
        fixture.model.InstanceSlots(storage)
            [fixture.linker.Field(*manager_field).slot]
                .bits)};
    const auto saved =
        fixture.On(manager, "getSavedCookies", "()Ljava/util/List;").ref;
    const auto saved_count = fixture.On(saved, "size", "()I").AsInt();
    const auto valid_path = expiration.value_or(0) > 0;
    std::int32_t count{-1};
    if (!valid_path) {
      const auto outcome = fixture.OnOutcome(
          storage, "getValidCookies", "()Ljava/util/Collection;");
      REQUIRE_FALSE(outcome.exception.IsValid());
      count = fixture.On(outcome.value.ref, "size", "()I").AsInt();
    }
    auto rows = static_cast<std::size_t>(saved_count);
    if (!valid_path) {
      auto database =
          database::Connection::Open(fixture.vfs, std::string(path));
      rows = database->Query("SELECT name FROM cookie").rows.size();
      CHECK(database->UserVersion() == 2);
      database->Close();
    }
    return std::tuple{saved_count, count, rows};
  };

  CHECK(run(std::nullopt) == std::tuple{0, 0, std::size_t{0}});
  CHECK(run(std::int64_t{0}) == std::tuple{1, 0, std::size_t{0}});
  CHECK(run(std::int64_t{4'102'444'800'000LL}) ==
        std::tuple{1, -1, std::size_t{1}});
}

TEST_CASE("DVM-186 helper callback failure rolls back schema and version") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    NetworkSqliteVm fixture(backend);
    fixture.helper_create_throws = true;
    const auto helper = fixture.NewHelper("callback-failure.db", 1);
    const auto outcome = fixture.OnOutcome(
        helper, "getWritableDatabase",
        "()Landroid/database/sqlite/SQLiteDatabase;");
    REQUIRE(outcome.exception.IsValid());
    CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
          "Ljava/lang/RuntimeException;");
    auto database = database::Connection::Open(
        fixture.vfs,
        "/data/data/test.game/databases/callback-failure.db");
    CHECK(database->UserVersion() == 0);
    CHECK(std::get<std::int64_t>(database->Query(
                                     "SELECT count(*) FROM sqlite_master "
                                     "WHERE name='callback_partial'")
                                     .rows[0][0]) == 0);
  }
}

TEST_CASE("DVM-88 SQLiteOpenHelper dispatches create and upgrade by version") {
  NetworkSqliteVm fixture;
  const auto user_version = [&fixture](const VmObjectRef database) {
    const auto cursor =
        fixture
            .On(database, "rawQuery",
                "(Ljava/lang/String;[Ljava/lang/String;)Landroid/database/"
                "Cursor;",
                {VmValue::Ref(fixture.vm.NewStringUtf8("PRAGMA user_version")),
                 VmValue::Ref(VmObjectRef{})})
            .ref;
    CHECK(fixture.On(cursor, "moveToFirst", "()Z").AsInt() == 1);
    const auto version =
        fixture.On(cursor, "getInt", "(I)I", {VmValue::Int(0)}).AsInt();
    fixture.On(cursor, "close", "()V");
    return version;
  };
  const auto first = fixture.NewHelper("lifecycle.db", 1);
  const auto database = fixture
                            .On(first, "getWritableDatabase",
                                "()Landroid/database/sqlite/SQLiteDatabase;")
                            .ref;
  CHECK(fixture.helper_create_calls == 1);
  CHECK(user_version(database) == 1);
  static_cast<void>(fixture.On(first, "close", "()V"));

  const auto second = fixture.NewHelper("lifecycle.db", 2);
  const auto upgraded = fixture
                            .On(second, "getWritableDatabase",
                                "()Landroid/database/sqlite/SQLiteDatabase;")
                            .ref;
  CHECK(upgraded != database);
  CHECK(fixture.helper_create_calls == 1);
  CHECK(fixture.helper_upgrade_calls == 1);
  CHECK(fixture.helper_old_version == 1);
  CHECK(fixture.helper_new_version == 2);
  CHECK(user_version(upgraded) == 2);
}

TEST_CASE("DVM-186 database open maps non-missing VFS failures") {
  NetworkSqliteVm fixture;
  const auto outcome = fixture.StaticOutcome(
      "Landroid/database/sqlite/SQLiteDatabase;", "openOrCreateDatabase",
      "(Ljava/lang/String;Landroid/database/sqlite/"
      "SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/SQLiteDatabase;",
      {VmValue::Ref(fixture.vm.NewStringUtf8("/data")),
       VmValue::Ref(VmObjectRef{})});
  CHECK(outcome.exception.IsValid());
  CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
        "Landroid/database/sqlite/SQLiteDiskIOException;");
}

TEST_CASE("DVM-186 corrupt database logs deletes and rebuilds") {
  NetworkSqliteVm fixture;
  constexpr std::string_view path =
      "/data/data/test.game/databases/corrupt.db";
  const std::array garbage{std::byte{'n'}, std::byte{'o'}, std::byte{'t'},
                           std::byte{'-'}, std::byte{'s'}, std::byte{'q'},
                           std::byte{'l'}, std::byte{'i'}, std::byte{'t'},
                           std::byte{'e'}};
  WriteVfsFile(fixture.vfs, path, garbage);
  const auto path_string = fixture.vm.NewStringUtf8(path);
  const auto first = fixture.StaticOutcome(
      "Landroid/database/sqlite/SQLiteDatabase;", "openOrCreateDatabase",
      "(Ljava/lang/String;Landroid/database/sqlite/"
      "SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/SQLiteDatabase;",
      {VmValue::Ref(path_string), VmValue::Ref(VmObjectRef{})});
  REQUIRE_FALSE(first.exception.IsValid());

  const auto records = fixture.logger.Snapshot(
      ogplay::core::LogLevel::info, "runtime.dexvm.guest");
  const auto event = std::ranges::find_if(records, [](const auto &record) {
    return record.message.starts_with(
        "EventLog tag=75004 payload=string(");
  });
  REQUIRE(event != records.end());
  CHECK(event->message.find("corrupt.db") != std::string::npos);

  const auto database = first.value.ref;
  fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
             {VmValue::Ref(fixture.vm.NewStringUtf8(
                 "CREATE TABLE rebuilt(value INTEGER)"))});
  fixture.On(database, "close", "()V");
  const auto image = ReadVfsFile(fixture.vfs, path);
  REQUIRE(image.size() >= 16);
  CHECK(std::string_view(reinterpret_cast<const char *>(image.data()), 16) ==
        std::string_view("SQLite format 3\0", 16));
}

TEST_CASE(
    "DVM-88 stage catalog keeps NIO GLES AudioTrack and data paths linkable") {
  NetworkSqliteVm fixture;
  for (const auto descriptor :
       {"Ljava/nio/ByteBuffer;", "Landroid/opengl/GLES20;",
        "Landroid/media/AudioTrack;", "Ljava/net/Socket;",
        "Landroid/database/sqlite/SQLiteDatabase;"}) {
    CHECK(fixture.linker.FindClass(descriptor).has_value());
  }
}
