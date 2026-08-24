#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
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
    void Close(std::uint64_t channel) noexcept override { closed = channel; }
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

    Dvm88Vm()
        : vm([this]() -> DexClassLinker& {
              context->package_name = "test.game";
              context->vfs = &vfs;
              linker.RegisterIntrinsics(CoreIntrinsicCatalog());
              linker.RegisterIntrinsics(AndroidIntrinsicCatalog(context));
              linker.Link();
              return linker;
          }(), model, nullptr, ledger, {}) {
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

TEST_CASE("DVM-88 stage catalog keeps NIO GLES AudioTrack and data paths linkable") {
    Dvm88Vm fixture;
    for (const auto descriptor : {
             "Ljava/nio/ByteBuffer;", "Landroid/opengl/GLES20;",
             "Landroid/media/AudioTrack;", "Ljava/net/Socket;",
             "Landroid/database/sqlite/SQLiteDatabase;"}) {
        CHECK(fixture.linker.FindClass(descriptor).has_value());
    }
}
