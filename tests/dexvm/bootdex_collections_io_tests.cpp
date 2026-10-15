#include "boot_dex.h"
#include <doctest/doctest.h>

#include <cstdint>
#include <algorithm>
#include <memory>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "ogplay/runtime/dexvm/big_int_runtime.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/dexvm_io_vfs.h"
#include "tls_fixtures.h"

#include "bootdex_library_fixture.h"

using namespace ogplay::test::bootdex_library;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-103 BootDex collection implementations own their methods and storage") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        for (const auto* name : {"ArrayList", "LinkedList", "Vector", "Stack", "ArrayDeque",
                                "HashSet", "LinkedHashSet", "TreeSet", "PriorityQueue",
                                "concurrent/CopyOnWriteArrayList", "concurrent/CopyOnWriteArraySet",
                                "concurrent/ConcurrentLinkedQueue", "concurrent/ConcurrentLinkedDeque",
                                "concurrent/LinkedBlockingQueue", "concurrent/LinkedBlockingDeque",
                                "concurrent/PriorityBlockingQueue", "concurrent/ConcurrentSkipListSet"}) {
            CAPTURE(name);
            const auto descriptor = std::string("Ljava/util/") + name + ";";
            const auto type = f.linker.ResolveDescriptor(descriptor);
            CHECK(f.linker.Class(type).is_boot_dex);
            for (const auto method : f.linker.Class(type).own_virtual_methods)
                CHECK(f.linker.Method(method).kind != MethodKind::intrinsic);
            const auto object = f.vm.NewIntrinsicInstance(descriptor);
            f.Construct(object, descriptor, "()V");
            const auto a = f.vm.NewStringUtf8("b");
            const auto b = f.vm.NewStringUtf8("a");
            f.RequireOk(f.Virtual(object, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(a)}));
            f.RequireOk(f.Virtual(object, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(b)}));
            auto size = f.Virtual(object, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 2);
            auto it = f.Virtual(object, "iterator", "()Ljava/util/Iterator;"); f.RequireOk(it);
            auto next = f.Virtual(it.value.ref, "next", "()Ljava/lang/Object;"); f.RequireOk(next);
            CHECK((next.value.ref == a || next.value.ref == b));
            f.RequireOk(f.Virtual(object, "clear", "()V"));
            size = f.Virtual(object, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 0);
        }
        for (const auto* name : {"HashMap", "LinkedHashMap", "Hashtable", "TreeMap", "IdentityHashMap",
                                "WeakHashMap", "Properties", "concurrent/ConcurrentHashMap",
                                "concurrent/ConcurrentSkipListMap"}) {
            CAPTURE(name);
            const auto descriptor = std::string("Ljava/util/") + name + ";";
            const auto object = f.vm.NewIntrinsicInstance(descriptor);
            f.Construct(object, descriptor, "()V");
            const auto key = f.vm.NewStringUtf8("key");
            const auto value = f.vm.NewStringUtf8("value");
            f.RequireOk(f.Virtual(object, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                 {VmValue::Ref(key), VmValue::Ref(value)}));
            auto got = f.Virtual(object, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {VmValue::Ref(key)});
            f.RequireOk(got); CHECK(got.value.ref == value);
            const auto view = f.Virtual(object, "entrySet", "()Ljava/util/Set;"); f.RequireOk(view);
            const auto it = f.Virtual(view.value.ref, "iterator", "()Ljava/util/Iterator;"); f.RequireOk(it);
            const auto entry = f.Virtual(it.value.ref, "next", "()Ljava/lang/Object;"); f.RequireOk(entry);
            got = f.Virtual(entry.value.ref, "getKey", "()Ljava/lang/Object;"); f.RequireOk(got); CHECK(got.value.ref == key);
            f.RequireOk(f.Virtual(object, "clear", "()V"));
            got = f.Virtual(view.value.ref, "size", "()I"); f.RequireOk(got); CHECK(got.value.AsInt() == 0);
        }
    }
}

TEST_CASE("DVM-103 BootDex weak keys clear and enqueue while live list elements survive GC") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto map = f.vm.NewIntrinsicInstance("Ljava/util/WeakHashMap;");
        f.Construct(map, "Ljava/util/WeakHashMap;", "()V");
        const auto list = f.vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
        f.Construct(list, "Ljava/util/ArrayList;", "()V");
        const auto key = f.vm.NewIntrinsicInstance("Ljava/lang/Object;");
        const auto value = f.vm.NewIntrinsicInstance("Ljava/lang/Object;");
        const auto queue = f.vm.NewIntrinsicInstance("Ljava/lang/ref/ReferenceQueue;");
        f.Construct(queue, "Ljava/lang/ref/ReferenceQueue;", "()V");
        const auto weak = f.vm.NewIntrinsicInstance("Ljava/lang/ref/WeakReference;");
        f.Construct(weak, "Ljava/lang/ref/WeakReference;",
            "(Ljava/lang/Object;Ljava/lang/ref/ReferenceQueue;)V", {VmValue::Ref(key), VmValue::Ref(queue)});
        f.RequireOk(f.Virtual(map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                             {VmValue::Ref(key), VmValue::Ref(value)}));
        f.RequireOk(f.Virtual(list, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(value)}));
        const std::array roots{map, list, weak, queue};
        const auto protect = f.vm.ProtectReferences(roots);
        static_cast<void>(f.vm.CollectGarbage());
        CHECK_FALSE(f.model.IsValidRef(key));
        CHECK(f.model.IsValidRef(value));
        const auto cleared = f.Virtual(weak, "get", "()Ljava/lang/Object;");
        f.RequireOk(cleared); CHECK_FALSE(cleared.value.ref.IsValid());
        const auto polled = f.Virtual(queue, "poll", "()Ljava/lang/ref/Reference;");
        f.RequireOk(polled); CHECK(polled.value.ref == weak);
        const auto reenqueue = f.Virtual(weak, "enqueue", "()Z");
        f.RequireOk(reenqueue); CHECK(reenqueue.value.AsInt() == 0);
        auto size = f.Virtual(map, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 0);
        auto clone = f.Virtual(list, "clone", "()Ljava/lang/Object;"); f.RequireOk(clone);
        f.RequireOk(f.Virtual(list, "clear", "()V"));
        size = f.Virtual(clone.value.ref, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 1);
        const auto got = f.Virtual(clone.value.ref, "get", "(I)Ljava/lang/Object;", {VmValue::Int(0)});
        f.RequireOk(got); CHECK(got.value.ref == value);
    }
}

TEST_CASE("DVM-103 Externalizable invokes public constructor and callbacks with shared handles") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        bool public_constructor = true;
        bool fail_write = false;
        bool fail_read = false;
        int protocol = 2;
        SUBCASE("round trip") {}
        SUBCASE("protocol 1 round trip") { protocol = 1; }
        SUBCASE("private constructor is rejected") { public_constructor = false; }
        SUBCASE("write callback preserves throwable identity") { fail_write = true; }
        SUBCASE("read callback preserves throwable identity") { fail_read = true; }
        auto expected_failure = std::make_shared<VmObjectRef>();
        auto builder = IntrinsicClassBuilder::Class("Ltest/External;", "Ljava/lang/Object;", {"Ljava/io/Externalizable;"});
        builder.ConstantInt("serialVersionUID", "J", 123, kAccPrivate);
        const auto value = builder.BoundInstanceField("value", "I");
        const auto self = builder.BoundInstanceField("self", "Ljava/lang/Object;");
        const auto constructed = builder.BoundInstanceField("constructed", "Z");
        builder.Constructor("()V", [constructed](IntrinsicContext& c) {
            IntrinsicCall(c).SetInt(constructed, 1); return VmValue::Void();
        }, public_constructor ? kAccPublic : kAccPrivate);
        const auto invoke = [](IntrinsicContext& c, VmObjectRef receiver, const char* name,
                               const char* signature, std::vector<VmValue> args = {}) {
            const auto type = c.vm.Model().ObjectClass(receiver);
            const auto slot = c.vm.Linker().FindVtableIndex(type, name, signature);
            REQUIRE(slot.has_value()); args.insert(args.begin(), VmValue::Ref(receiver));
            const auto result = c.vm.Call(c.vm.Linker().Class(type).vtable[*slot], args);
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            return result.value;
        };
        builder.VirtualMethod("writeExternal", "(Ljava/io/ObjectOutput;)V", [value, invoke, fail_write, expected_failure](IntrinsicContext& c) {
            if (fail_write) {
                *expected_failure = c.vm.NewIntrinsicInstance("Ljava/io/IOException;");
                c.vm.SetPendingException(*expected_failure);
                return VmValue::Void();
            }
            const auto out = c.arguments[0].ref;
            invoke(c, out, "writeInt", "(I)V", {VmValue::Int(IntrinsicCall(c).GetInt(value))});
            invoke(c, out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(c.receiver)});
            // The reader may leave trailing primitive and object data unconsumed.
            invoke(c, out, "writeInt", "(I)V", {VmValue::Int(99)});
            invoke(c, out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(c.receiver)});
            return VmValue::Void();
        });
        builder.VirtualMethod("readExternal", "(Ljava/io/ObjectInput;)V", [value, self, invoke, fail_read, expected_failure, protocol](IntrinsicContext& c) {
            if (fail_read) {
                *expected_failure = c.vm.NewIntrinsicInstance("Ljava/io/IOException;");
                c.vm.SetPendingException(*expected_failure);
                return VmValue::Void();
            }
            const auto in = c.arguments[0].ref;
            IntrinsicCall(c).SetInt(value, invoke(c, in, "readInt", "()I").AsInt());
            IntrinsicCall(c).SetRef(self, invoke(c, in, "readObject", "()Ljava/lang/Object;").ref);
            if (protocol == 1) { // Protocol 1 has no end-block marker: consume the whole custom payload.
                CHECK(invoke(c, in, "readInt", "()I").AsInt() == 99);
                CHECK(invoke(c, in, "readObject", "()Ljava/lang/Object;").ref == c.receiver);
            }
            return VmValue::Void();
        });
        std::vector<IntrinsicClassDecl> extras{std::move(builder).Build()};
        Dvm87Vm f(backend, "en", "eng", "USA", "GMT", extras);
        const auto object = f.vm.NewIntrinsicInstance("Ltest/External;");
        const auto field = f.linker.FindFieldRecursive(f.model.ObjectClass(object), "value", "I"); REQUIRE(field);
        f.model.InstanceSlots(object)[f.linker.Field(*field).slot] = {42, SlotTag::cat1};
        const auto bytes = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
        f.Construct(bytes, "Ljava/io/ByteArrayOutputStream;", "()V");
        const auto out = f.vm.NewIntrinsicInstance("Ljava/io/ObjectOutputStream;");
        f.Construct(out, "Ljava/io/ObjectOutputStream;", "(Ljava/io/OutputStream;)V", {VmValue::Ref(bytes)});
        f.RequireOk(f.Virtual(out, "useProtocolVersion", "(I)V", {VmValue::Int(protocol)}));
        const auto written = f.Virtual(out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(object)});
        if (fail_write) { CHECK(written.exception == *expected_failure); continue; }
        f.RequireOk(written);
        f.RequireOk(f.Virtual(out, "writeObject", "(Ljava/lang/Object;)V", {VmValue::Ref(object)}));
        f.RequireOk(f.Virtual(out, "flush", "()V"));
        CHECK(f.vm.IO().FindOutput(out) == nullptr);
        const auto encoded = f.Virtual(bytes, "toByteArray", "()[B"); f.RequireOk(encoded);
        const auto raw = encoded.value.ref;
        const auto input = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
        f.Construct(input, "Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(raw)});
        const auto in = f.vm.NewIntrinsicInstance("Ljava/io/ObjectInputStream;");
        f.Construct(in, "Ljava/io/ObjectInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(input)});
        const auto first = f.Virtual(in, "readObject", "()Ljava/lang/Object;");
        if (!public_constructor) {
            REQUIRE(first.exception.IsValid());
            CHECK(f.linker.Class(first.exception_class).descriptor == "Ljava/io/InvalidClassException;");
            continue;
        }
        if (fail_read) { CHECK(first.exception == *expected_failure); continue; }
        f.RequireOk(first);
        const auto second = f.Virtual(in, "readObject", "()Ljava/lang/Object;"); f.RequireOk(second);
        CHECK(first.value.ref == second.value.ref);
        CHECK(first.value.ref != object);
        const auto type = f.model.ObjectClass(first.value.ref);
        const auto slots = f.model.InstanceSlots(first.value.ref);
        CHECK(slots[f.linker.Field(*field).slot].bits == 42);
        const auto self_field = f.linker.FindFieldRecursive(type, "self", "Ljava/lang/Object;"); REQUIRE(self_field);
        CHECK(slots[f.linker.Field(*self_field).slot].bits == first.value.ref.Value());
        const auto ctor_field = f.linker.FindFieldRecursive(type, "constructed", "Z"); REQUIRE(ctor_field);
        CHECK(slots[f.linker.Field(*ctor_field).slot].bits == 1);
    }
}

TEST_CASE("DVM-103 bounded queues and Collections wrappers use API19 semantics") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto value = f.vm.NewStringUtf8("item");
        for (const auto* name : {"ArrayBlockingQueue", "LinkedBlockingQueue", "LinkedBlockingDeque"}) {
            CAPTURE(name);
            const auto descriptor = std::string("Ljava/util/concurrent/") + name + ";";
            const auto q = f.vm.NewIntrinsicInstance(descriptor);
            f.Construct(q, descriptor, "(I)V", {VmValue::Int(1)});
            const auto offer = [&] { return f.Virtual(q, "offer", "(Ljava/lang/Object;)Z", {VmValue::Ref(value)}); };
            auto result = offer(); f.RequireOk(result); CHECK(result.value.AsInt() == 1);
            result = offer(); f.RequireOk(result); CHECK(result.value.AsInt() == 0);
            result = f.Virtual(q, "poll", "()Ljava/lang/Object;"); f.RequireOk(result); CHECK(result.value.ref == value);
            result = f.Virtual(q, "poll", "()Ljava/lang/Object;"); f.RequireOk(result); CHECK_FALSE(result.value.ref.IsValid());
        }
        for (const auto* name : {"SynchronousQueue", "LinkedTransferQueue"}) {
            CAPTURE(name);
            const auto descriptor = std::string("Ljava/util/concurrent/") + name + ";";
            const auto q = f.vm.NewIntrinsicInstance(descriptor);
            f.Construct(q, descriptor, "()V");
            const auto result = f.Virtual(q, "poll", "()Ljava/lang/Object;");
            f.RequireOk(result); CHECK_FALSE(result.value.ref.IsValid());
        }
        const auto list = f.vm.NewIntrinsicInstance("Ljava/util/ArrayList;");
        f.Construct(list, "Ljava/util/ArrayList;", "()V");
        f.RequireOk(f.Virtual(list, "add", "(Ljava/lang/Object;)Z", {VmValue::Ref(value)}));
        const auto view = f.Static("Ljava/util/Collections;", "unmodifiableList",
            "(Ljava/util/List;)Ljava/util/List;", {VmValue::Ref(list)}); f.RequireOk(view);
        const auto rejected = f.Virtual(view.value.ref, "clear", "()V");
        REQUIRE(rejected.exception.IsValid());
        CHECK(f.linker.Class(rejected.exception_class).descriptor == "Ljava/lang/UnsupportedOperationException;");
        f.RequireOk(f.Virtual(list, "clear", "()V"));
        const auto size = f.Virtual(view.value.ref, "size", "()I"); f.RequireOk(size); CHECK(size.value.AsInt() == 0);
    }
}

TEST_CASE("DVM-104 memory wrappers preserve underlying guest stream identity") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        const auto bytes = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayOutputStream;");
        f.Construct(bytes, "Ljava/io/ByteArrayOutputStream;", "()V");
        const auto buffered = f.vm.NewIntrinsicInstance("Ljava/io/BufferedOutputStream;");
        f.Construct(buffered, "Ljava/io/BufferedOutputStream;", "(Ljava/io/OutputStream;I)V", {VmValue::Ref(bytes), VmValue::Int(2)});
        const auto data = f.vm.NewIntrinsicInstance("Ljava/io/DataOutputStream;");
        f.Construct(data, "Ljava/io/DataOutputStream;", "(Ljava/io/OutputStream;)V", {VmValue::Ref(buffered)});
        const auto text = f.vm.NewStringUtf8("Hello 世界");
        f.RequireOk(f.Virtual(data, "writeUTF", "(Ljava/lang/String;)V", {VmValue::Ref(text)}));
        f.RequireOk(f.Virtual(data, "flush", "()V"));
        const auto encoded = f.Virtual(bytes, "toByteArray", "()[B"); f.RequireOk(encoded);
        CHECK(f.model.ArrayLength(encoded.value.ref) == 14);
        const auto source = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
        f.Construct(source, "Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(encoded.value.ref)});
        const auto input = f.vm.NewIntrinsicInstance("Ljava/io/DataInputStream;");
        f.Construct(input, "Ljava/io/DataInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(source)});
        const auto decoded = f.Virtual(input, "readUTF", "()Ljava/lang/String;"); f.RequireOk(decoded);
        CHECK(f.vm.StringUtf8(decoded.value.ref) == "Hello 世界");
        const auto remaining = f.Virtual(source, "available", "()I"); f.RequireOk(remaining); CHECK(remaining.value.AsInt() == 0);
        const auto sr = f.vm.NewIntrinsicInstance("Ljava/io/StringReader;");
        f.Construct(sr, "Ljava/io/StringReader;", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("first\r\nsecond"))});
        const auto br = f.vm.NewIntrinsicInstance("Ljava/io/BufferedReader;");
        f.Construct(br, "Ljava/io/BufferedReader;", "(Ljava/io/Reader;I)V", {VmValue::Ref(sr), VmValue::Int(2)});
        auto line=f.Virtual(br,"readLine","()Ljava/lang/String;");f.RequireOk(line);CHECK(f.vm.StringUtf8(line.value.ref)=="first");
        line=f.Virtual(br,"readLine","()Ljava/lang/String;");f.RequireOk(line);CHECK(f.vm.StringUtf8(line.value.ref)=="second");
        f.RequireOk(f.Virtual(br,"close","()V"));
        const auto closed=f.Virtual(sr,"read","()I");REQUIRE(closed.exception.IsValid());CHECK(f.linker.Class(closed.exception_class).descriptor=="Ljava/io/IOException;");
    }
}

TEST_CASE("DVM-104 standard charset names encode decode and incremental reader agree") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        Dvm87Vm f(backend);
        for (const auto* name : {"UTF-8", "UTF-16", "UTF-16BE", "UTF-16LE", "ISO-8859-1", "US-ASCII"}) {
            CAPTURE(name);
            const auto charset=f.Static("Ljava/nio/charset/Charset;","forName","(Ljava/lang/String;)Ljava/nio/charset/Charset;",{VmValue::Ref(f.vm.NewStringUtf8(name))});f.RequireOk(charset);
            const auto text=f.model.NewString(std::string(name).starts_with("UTF")?u"A世界😀":u"Abc");
            const auto bytes=f.Virtual(text,"getBytes","(Ljava/nio/charset/Charset;)[B",{VmValue::Ref(charset.value.ref)});f.RequireOk(bytes);
            const auto decoded=f.vm.NewIntrinsicInstance("Ljava/lang/String;");
            f.Construct(decoded,"Ljava/lang/String;","([BLjava/nio/charset/Charset;)V",{VmValue::Ref(bytes.value.ref),VmValue::Ref(charset.value.ref)});
            CHECK(f.vm.StringUtf8(decoded)==f.vm.StringUtf8(text));
            const auto source=f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");f.Construct(source,"Ljava/io/ByteArrayInputStream;","([B)V",{VmValue::Ref(bytes.value.ref)});
            const auto reader=f.vm.NewIntrinsicInstance("Ljava/io/InputStreamReader;");f.Construct(reader,"Ljava/io/InputStreamReader;","(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V",{VmValue::Ref(source),VmValue::Ref(charset.value.ref)});
            std::u16string result;
            for (int i=0;i<32;++i) {const auto unit=f.Virtual(reader,"read","()I");f.RequireOk(unit);if(unit.value.AsInt()<0)break;result.push_back(static_cast<char16_t>(unit.value.AsInt()));}
            CHECK(result==f.model.StringValue(text));
            const auto encoding=f.Virtual(reader,"getEncoding","()Ljava/lang/String;"); f.RequireOk(encoding); CHECK(f.vm.StringUtf8(encoding.value.ref)==name);
            const auto named=f.vm.NewIntrinsicInstance("Ljava/lang/String;"); f.Construct(named,"Ljava/lang/String;","([BLjava/lang/String;)V",{VmValue::Ref(bytes.value.ref),VmValue::Ref(f.vm.NewStringUtf8(name))}); CHECK(f.model.StringValue(named)==f.model.StringValue(text));
            const auto ordering=f.Virtual(charset.value.ref,"compareTo","(Ljava/nio/charset/Charset;)I",{VmValue::Ref(charset.value.ref)}); f.RequireOk(ordering); CHECK(ordering.value.AsInt()==0);

            f.RequireOk(f.Virtual(reader,"close","()V"));
        }
        const auto malformed=f.model.NewString(std::u16string(1,static_cast<char16_t>(0xd800)));
        for (const auto* name : {"UTF-8", "UTF-16BE", "UTF-16LE", "US-ASCII"}) {
            const auto bytes=f.Virtual(malformed,"getBytes","(Ljava/lang/String;)[B",{VmValue::Ref(f.vm.NewStringUtf8(name))}); f.RequireOk(bytes);
            const auto decoded=f.vm.NewIntrinsicInstance("Ljava/lang/String;");
            f.Construct(decoded,"Ljava/lang/String;","([BLjava/lang/String;)V",{VmValue::Ref(bytes.value.ref),VmValue::Ref(f.vm.NewStringUtf8(name))});
            CHECK(f.model.StringValue(decoded)==(std::string_view(name).starts_with("UTF-16")?u"\ufffd":u"?"));
        }
        const auto invalid=f.Static("Ljava/nio/charset/Charset;","forName","(Ljava/lang/String;)Ljava/nio/charset/Charset;",{VmValue::Ref(f.vm.NewStringUtf8("not a charset"))});
        REQUIRE(invalid.exception.IsValid());CHECK(f.linker.Class(invalid.exception_class).descriptor=="Ljava/nio/charset/IllegalCharsetNameException;");
        const auto unsupported=f.Static("Ljava/nio/charset/Charset;","forName","(Ljava/lang/String;)Ljava/nio/charset/Charset;",{VmValue::Ref(f.vm.NewStringUtf8("made-up"))});
        REQUIRE(unsupported.exception.IsValid());CHECK(f.linker.Class(unsupported.exception_class).descriptor=="Ljava/nio/charset/UnsupportedCharsetException;");
    }
}

TEST_CASE("DVM-104 guest stream callbacks retain monitor exception identity and nonblocking available") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto failure = std::make_shared<VmObjectRef>();
        auto reads = std::make_shared<int>();
        auto locked = std::make_shared<bool>(false);
        auto input = IntrinsicClassBuilder::Class("Ltest/CallbackInput;", "Ljava/io/InputStream;");
        input.OverrideMethod("read", "()I", [failure, reads, locked](IntrinsicContext& c) {
            *locked = c.vm.Monitors().IsOwner(c.receiver, c.vm.CurrentContextToken());
            ++*reads;
            static_cast<void>(c.vm.CollectGarbage("stream-callback"));
            c.vm.SetPendingException(*failure); return VmValue::Int(-1);
        });
        const std::vector<IntrinsicClassDecl> extras{std::move(input).Build()};
        Dvm87Vm f(backend,"en","eng","USA","GMT",extras);
        *failure=f.vm.NewIntrinsicInstance("Ljava/io/IOException;");
        const std::array roots{*failure}; const auto protect=f.vm.ProtectReferences(roots);
        const auto source=f.vm.NewIntrinsicInstance("Ltest/CallbackInput;");
        const auto reader=f.vm.NewIntrinsicInstance("Ljava/io/InputStreamReader;");
        f.Construct(reader,"Ljava/io/InputStreamReader;","(Ljava/io/InputStream;)V",{VmValue::Ref(source)});
        const auto result=f.Virtual(reader,"read","()I"); CHECK(result.exception==*failure); CHECK(*locked); CHECK(*reads==1);
        CHECK_FALSE(f.vm.Monitors().IsOwner(source,f.vm.CurrentContextToken()));
        // A stream whose header is available but whose next byte must not be read by available().
        const auto header = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[B"), JniPrimitiveKind::byte, 4);
        f.model.WriteByteRegion(header, 0, std::array{std::byte{0xac}, std::byte{0xed}, std::byte{0}, std::byte{5}});
        const auto wire = f.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
        f.Construct(wire, "Ljava/io/ByteArrayInputStream;", "([B)V", {VmValue::Ref(header)});
        const auto object = f.vm.NewIntrinsicInstance("Ljava/io/ObjectInputStream;");
        f.Construct(object, "Ljava/io/ObjectInputStream;", "(Ljava/io/InputStream;)V", {VmValue::Ref(wire)});
        const auto available=f.Virtual(object,"available","()I"); f.RequireOk(available); CHECK(available.value.AsInt()==0); CHECK(*reads==1);
    }
}

TEST_CASE("DVM-151 StringBuffer monitor covers callbacks and releases on exception") {
    for (auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto receiver = std::make_shared<VmObjectRef>();
        auto observed = std::make_shared<bool>(false);
        auto probe = IntrinsicClassBuilder::Class("Ltest/BuilderCallback;");
        probe.OverrideMethod("toString", "()Ljava/lang/String;",
            [receiver, observed](IntrinsicContext& c) -> VmValue {
                *observed = c.vm.Monitors().IsOwner(*receiver, c.vm.CurrentContextToken());
                throw VmJavaThrow{"Ljava/lang/IllegalStateException;", "callback failure"};
            });
        const std::vector<IntrinsicClassDecl> extras{std::move(probe).Build()};
        Dvm87Vm f(backend, "en", "eng", "USA", "GMT", extras);
        *receiver = f.vm.NewIntrinsicInstance("Ljava/lang/StringBuffer;");
        f.Construct(*receiver, "Ljava/lang/StringBuffer;", "()V");
        const auto object = f.vm.NewIntrinsicInstance("Ltest/BuilderCallback;");
        const auto roots = f.vm.ProtectReferences(std::array{*receiver, object});
        const auto token = f.vm.CurrentContextToken();
        f.vm.Monitors().Enter(*receiver, token);
        const auto outcome = f.Virtual(*receiver, "append", "(Ljava/lang/Object;)Ljava/lang/StringBuffer;", {VmValue::Ref(object)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(f.linker.Class(outcome.exception_class).descriptor == "Ljava/lang/IllegalStateException;");
        CHECK(*observed);
        CHECK(f.vm.Monitors().Snapshot(*receiver).recursion == 1);
        f.vm.Monitors().Exit(*receiver, token);
        CHECK(f.vm.Monitors().Snapshot(*receiver).recursion == 0);
        CHECK(f.Virtual(*receiver, "length", "()I").value.AsInt() == 0);
        f.RequireOk(f.Virtual(*receiver, "append", "(C)Ljava/lang/StringBuffer;", {VmValue::Int('x')}));
        CHECK(f.vm.StringUtf8(f.Virtual(*receiver, "toString", "()Ljava/lang/String;").value.ref) == "x");
    }
}
