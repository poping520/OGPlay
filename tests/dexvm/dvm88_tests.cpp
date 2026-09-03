#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/network_runtime.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace {
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

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
    void Send(std::uint64_t channel,
              std::span<const std::byte> bytes) override {
        CHECK(channel == 7);
        sent.assign(bytes.begin(), bytes.end());
    }
    std::vector<std::byte> Receive(std::uint64_t channel,
                                   std::size_t maximum) override {
        CHECK(channel == 7);
        const std::string value = "pong";
        const auto count = std::min(maximum, value.size());
        return std::vector<std::byte>(
            reinterpret_cast<const std::byte*>(value.data()),
            reinterpret_cast<const std::byte*>(value.data() + count));
    }
    void Close(std::uint64_t channel) noexcept override {
        closed = channel;
        ++close_count;
    }
    void SendDatagram(const NetworkDatagram& datagram) override {
        last_datagram = datagram;
    }
    NetworkDatagram ReceiveDatagram(std::size_t maximum) override {
        NetworkDatagram result{"game.test", 9000,
                               {std::byte{'o'}, std::byte{'k'}}};
        if (result.payload.size() > maximum) result.payload.resize(maximum);
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

struct Dvm88Vm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    VirtualFileSystem vfs;
    std::shared_ptr<DexVmAndroidContext> context{
        std::make_shared<DexVmAndroidContext>()};
    Interpreter vm;
    std::int32_t helper_create_calls{};
    std::int32_t helper_upgrade_calls{};
    std::int32_t helper_old_version{};
    std::int32_t helper_new_version{};

    explicit Dvm88Vm(
        const InterpreterBackend backend = InterpreterBackend::switch_dispatch)
        : vm([this]() -> DexClassLinker& {
              context->package_name = "test.game";
              context->vfs = &vfs;
              linker.RegisterIntrinsics(CoreIntrinsicCatalog());
              linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
              auto helper = IntrinsicClassBuilder::Class(
                  "Ltest/Dvm88OpenHelper;",
                  "Landroid/database/sqlite/SQLiteOpenHelper;");
              helper.OverrideMethod("onCreate",
                  "(Landroid/database/sqlite/SQLiteDatabase;)V",
                  [this](IntrinsicContext&) {
                      ++helper_create_calls;
                      return VmValue::Void();
                  });
              helper.OverrideMethod("onUpgrade",
                  "(Landroid/database/sqlite/SQLiteDatabase;II)V",
                  [this](IntrinsicContext& call) {
                      ++helper_upgrade_calls;
                      helper_old_version = call.arguments[1].AsInt();
                      helper_new_version = call.arguments[2].AsInt();
                      return VmValue::Void();
                  });
              std::vector<IntrinsicClassDecl> test_catalog;
              test_catalog.push_back(std::move(helper).Build());
              linker.RegisterIntrinsics(test_catalog);
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, InterpreterConfig{.backend = backend}) {
        RegisterAndroidDatabaseStateTables(vm, context);
        vfs.CreateDirectory("/data");
        vfs.CreateDirectory("/data/data");
        vfs.CreateDirectory("/data/data/test.game");
    }

    VmObjectRef New(const char* descriptor, const char* constructor = "()V",
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

    VmValue Static(const char* owner, const char* name, const char* descriptor,
                   std::vector<VmValue> arguments = {}) {
        const auto method = linker.FindDirectMethod(
            linker.ResolveDescriptor(owner), name, descriptor);
        REQUIRE(method.has_value());
        const auto outcome = vm.Call(*method, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmCallOutcome StaticOutcome(
        const char* owner, const char* name, const char* descriptor,
        std::vector<VmValue> arguments = {}) {
        const auto method = linker.FindDirectMethod(
            linker.ResolveDescriptor(owner), name, descriptor);
        REQUIRE(method.has_value());
        return vm.Call(*method, arguments);
    }

    VmValue On(VmObjectRef receiver, const char* name, const char* descriptor,
               std::vector<VmValue> arguments = {}) {
        const auto owner = model.ObjectClass(receiver);
        const auto index = linker.FindVtableIndex(owner, name, descriptor);
        REQUIRE(index.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        const auto outcome = vm.Call(linker.Class(owner).vtable[*index], arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return outcome.value;
    }

    VmObjectRef Strings(std::initializer_list<std::string_view> values) {
        const auto array = model.NewObjectArray(
            linker.ResolveDescriptor("[Ljava/lang/String;"),
            linker.ResolveDescriptor("Ljava/lang/String;"),
            static_cast<JniSize>(values.size()));
        std::int32_t index{};
        for (const auto value : values)
            model.SetObjectElement(array, index++, vm.NewStringUtf8(value));
        return array;
    }

    VmObjectRef NewHelper(const std::string_view name, const std::int32_t version) {
        const auto helper = vm.NewIntrinsicInstance("Ltest/Dvm88OpenHelper;");
        const auto constructor = linker.FindDirectMethod(
            linker.ResolveDescriptor("Landroid/database/sqlite/SQLiteOpenHelper;"),
            "<init>",
            "(Landroid/content/Context;Ljava/lang/String;Landroid/database/sqlite/SQLiteDatabase$CursorFactory;I)V");
        REQUIRE(constructor.has_value());
        const std::array arguments{
            VmValue::Ref(helper), VmValue::Ref(VmObjectRef{}),
            VmValue::Ref(vm.NewStringUtf8(name)), VmValue::Ref(VmObjectRef{}),
            VmValue::Int(version)};
        const auto outcome = vm.Call(*constructor, arguments);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
        return helper;
    }
};
}  // namespace

TEST_CASE("DVM-88 network runtime is offline unless explicitly injected") {
    NetworkRuntime runtime;
    CHECK_THROWS_WITH_AS(([&] { static_cast<void>(runtime.Resolve("game.test")); }()),
                         "network policy is offline", NetworkRuntimeError);

    FakeNetwork transport;
    runtime.Configure({true, true, true, {"game.test"}}, &transport);
    CHECK(runtime.Resolve("game.test") == std::vector<std::string>{"203.0.113.7"});
    CHECK_THROWS_AS(([&] { static_cast<void>(runtime.Resolve("tracker.test")); }()),
                    NetworkRuntimeError);

    runtime.CreateSocket(VmObjectRef(1), true);
    runtime.Connect(VmObjectRef(1), {"game.test", "203.0.113.7", 443});
    runtime.BindStream(VmObjectRef(2), VmObjectRef(1), true);
    runtime.BindStream(VmObjectRef(3), VmObjectRef(1), false);
    const std::array request{std::byte{'p'}, std::byte{'i'}, std::byte{'n'},
                             std::byte{'g'}};
    runtime.WriteStream(VmObjectRef(2), request);
    CHECK(runtime.ReadStream(VmObjectRef(3), 4) ==
          std::vector<std::byte>{std::byte{'p'}, std::byte{'o'},
                                 std::byte{'n'}, std::byte{'g'}});
    CHECK(transport.connected == "game.test:443");
    CHECK(transport.used_tls);
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

TEST_CASE("DVM-88 URL form codecs match API 19 UTF-8 behavior") {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        CAPTURE(backend == InterpreterBackend::threaded ? "threaded" :
                                                         "switch");
        Dvm88Vm fixture(backend);
        const auto utf8 = fixture.vm.NewStringUtf8("UTF-8");
        const std::u16string clear{u'a', u' ', u'b', u'+', u'c', u'/',
                                   0x00e9U, 0xd83dU, 0xde00U};
        const auto input = fixture.model.NewString(clear);
        const auto encoded = fixture.Static(
            "Ljava/net/URLEncoder;", "encode",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(input), VmValue::Ref(utf8)}).ref;
        CHECK(fixture.vm.StringUtf8(encoded) ==
              "a+b%2Bc%2F%C3%A9%F0%9F%98%80");

        const auto decoded = fixture.Static(
            "Ljava/net/URLDecoder;", "decode",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(encoded), VmValue::Ref(utf8)}).ref;
        CHECK(fixture.model.StringValue(decoded) == clear);

        const auto default_encoded = fixture.Static(
            "Ljava/net/URLEncoder;", "encode",
            "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("x y"))}).ref;
        CHECK(fixture.vm.StringUtf8(default_encoded) == "x+y");
        const auto default_decoded = fixture.Static(
            "Ljava/net/URLDecoder;", "decode",
            "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(default_encoded)}).ref;
        CHECK(fixture.vm.StringUtf8(default_decoded) == "x y");

        const auto unchanged = fixture.vm.NewStringUtf8("plain");
        CHECK(fixture.Static(
                  "Ljava/net/URLDecoder;", "decode",
                  "(Ljava/lang/String;)Ljava/lang/String;",
                  {VmValue::Ref(unchanged)}).ref == unchanged);

        const auto invalid = fixture.StaticOutcome(
            "Ljava/net/URLDecoder;", "decode",
            "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(fixture.vm.NewStringUtf8("bad%2")),
             VmValue::Ref(utf8)});
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

TEST_CASE("DVM-88 SocketFactory exposes policy-gated common creation") {
    FakeNetwork transport;
    Dvm88Vm fixture;
    fixture.vm.Network().Configure(
        {true, false, false, {"game.test"}}, &transport);
    const auto factory = fixture.Static(
        "Ljavax/net/SocketFactory;", "getDefault",
        "()Ljavax/net/SocketFactory;").ref;
    const auto socket = fixture.On(
        factory, "createSocket", "(Ljava/lang/String;I)Ljava/net/Socket;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("game.test")),
         VmValue::Int(80)}).ref;
    CHECK(fixture.linker.Class(fixture.model.ObjectClass(socket)).descriptor ==
          "Ljava/net/Socket;");
    CHECK(transport.connected == "game.test:80");
    static_cast<void>(fixture.On(socket, "close", "()V"));
}

TEST_CASE("DVM-88 ContentValues SQLite query persists through guest VFS") {
    Dvm88Vm fixture;
    const auto path = fixture.vm.NewStringUtf8(
        "/data/data/test.game/databases/save.db");
    auto database = fixture.Static(
        "Landroid/database/sqlite/SQLiteDatabase;", "openOrCreateDatabase",
        "(Ljava/lang/String;Landroid/database/sqlite/SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/SQLiteDatabase;",
        {VmValue::Ref(path), VmValue::Ref(VmObjectRef(0))}).ref;
    fixture.On(database, "execSQL", "(Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8(
                   "CREATE TABLE saves (name TEXT, score INTEGER)"))});

    const auto values = fixture.New("Landroid/content/ContentValues;");
    fixture.On(values, "put",
               "(Ljava/lang/String;Ljava/lang/String;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8("name")),
                VmValue::Ref(fixture.vm.NewStringUtf8("slot-a"))});
    const auto score = fixture.New("Ljava/lang/Integer;", "(I)V",
                                   {VmValue::Int(42)});
    fixture.On(values, "put", "(Ljava/lang/String;Ljava/lang/Integer;)V",
               {VmValue::Ref(fixture.vm.NewStringUtf8("score")),
                VmValue::Ref(score)});
    CHECK(fixture.On(database, "insert",
        "(Ljava/lang/String;Ljava/lang/String;Landroid/content/ContentValues;)J",
        {VmValue::Ref(fixture.vm.NewStringUtf8("saves")),
         VmValue::Ref(VmObjectRef(0)), VmValue::Ref(values)}).AsLong() == 1);

    fixture.On(database, "close", "()V");
    const auto database_path = "/data/data/test.game/databases/save.db";
    const auto info = fixture.vfs.Stat(database_path);
    CHECK(info.size > 6);
    CHECK(info.writable);

    // Discard the in-memory owner maps so reopening must deserialize guest VFS.
    fixture.context->databases.erase(database.Value());
    fixture.context->database_by_path.erase(database_path);
    database = fixture.Static(
        "Landroid/database/sqlite/SQLiteDatabase;", "openOrCreateDatabase",
        "(Ljava/lang/String;Landroid/database/sqlite/SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/SQLiteDatabase;",
        {VmValue::Ref(path), VmValue::Ref(VmObjectRef(0))}).ref;

    const auto cursor = fixture.On(database, "query",
        "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("saves")),
         VmValue::Ref(fixture.Strings({"name", "score"})),
         VmValue::Ref(fixture.vm.NewStringUtf8("name=?")),
         VmValue::Ref(fixture.Strings({"slot-a"})),
         VmValue::Ref(VmObjectRef(0)), VmValue::Ref(VmObjectRef(0)),
         VmValue::Ref(VmObjectRef(0))}).ref;
    CHECK(fixture.On(cursor, "getCount", "()I").AsInt() == 1);
    CHECK(fixture.On(cursor, "moveToFirst", "()Z").AsInt() == 1);
    CHECK(fixture.vm.StringUtf8(
        fixture.On(cursor, "getString", "(I)Ljava/lang/String;",
                   {VmValue::Int(0)}).ref) == "slot-a");
    CHECK(fixture.On(cursor, "getInt", "(I)I", {VmValue::Int(1)}).AsInt() == 42);

    const auto helper = fixture.New(
        "Landroid/database/sqlite/SQLiteOpenHelper;",
        "(Landroid/content/Context;Ljava/lang/String;Landroid/database/sqlite/SQLiteDatabase$CursorFactory;I)V",
        {VmValue::Ref(VmObjectRef(0)),
         VmValue::Ref(fixture.vm.NewStringUtf8("helper.db")),
         VmValue::Ref(VmObjectRef(0)), VmValue::Int(1)});
    const auto helper_database = fixture.On(
        helper, "getWritableDatabase",
        "()Landroid/database/sqlite/SQLiteDatabase;").ref;
    fixture.vm.SetGcIntegration(
        {{}, {}, [helper](const VmRootVisitor& visit) { visit(helper); }});
    const auto marked = fixture.vm.MarkReachable();
    CHECK(marked.IsMarked(helper));
    CHECK(marked.IsMarked(helper_database));
}

TEST_CASE("DVM-88 SQLiteOpenHelper dispatches create and upgrade by version") {
    Dvm88Vm fixture;
    const auto first = fixture.NewHelper("lifecycle.db", 1);
    const auto database = fixture.On(
        first, "getWritableDatabase",
        "()Landroid/database/sqlite/SQLiteDatabase;").ref;
    CHECK(fixture.helper_create_calls == 1);
    CHECK(fixture.context->databases.at(database.Value()).version == 1);
    static_cast<void>(fixture.On(first, "close", "()V"));

    const auto second = fixture.NewHelper("lifecycle.db", 2);
    CHECK(fixture.On(second, "getWritableDatabase",
                     "()Landroid/database/sqlite/SQLiteDatabase;").ref == database);
    CHECK(fixture.helper_create_calls == 1);
    CHECK(fixture.helper_upgrade_calls == 1);
    CHECK(fixture.helper_old_version == 1);
    CHECK(fixture.helper_new_version == 2);
    CHECK(fixture.context->databases.at(database.Value()).version == 2);
}

TEST_CASE("DVM-88 database open reports non-missing VFS failures") {
    Dvm88Vm fixture;
    const auto outcome = fixture.StaticOutcome(
        "Landroid/database/sqlite/SQLiteDatabase;", "openOrCreateDatabase",
        "(Ljava/lang/String;Landroid/database/sqlite/SQLiteDatabase$CursorFactory;)Landroid/database/sqlite/SQLiteDatabase;",
        {VmValue::Ref(fixture.vm.NewStringUtf8("/data")),
         VmValue::Ref(VmObjectRef{})});
    CHECK(outcome.exception.IsValid());
    CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
          "Landroid/database/SQLException;");
}

TEST_CASE("DVM-88 stage catalog keeps NIO GLES AudioTrack and data paths linkable") {
    Dvm88Vm fixture;
    for (const auto descriptor : {
             "Ljava/nio/ByteBuffer;", "Landroid/opengl/GLES20;",
             "Landroid/media/AudioTrack;", "Ljava/net/Socket;",
             "Landroid/database/sqlite/SQLiteDatabase;"}) {
        CHECK(fixture.linker.FindClass(descriptor).has_value());
    }
}
