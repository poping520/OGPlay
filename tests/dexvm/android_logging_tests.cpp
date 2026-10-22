#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/runtime/integration/host_image_decode.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("Log debug throwable overload renders without changing control flow") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto message = fixture.vm.NewStringUtf8("failure detail");
        const auto throwable = fixture.New(
            "Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(message)});
        const auto tag = fixture.vm.NewStringUtf8("Kiwi");
        const auto text = fixture.vm.NewStringUtf8("task failed");
        const auto roots = fixture.vm.ProtectReferences(
            std::array{tag, text, throwable});
        CHECK(fixture.Static(
                  "Landroid/util/Log;", "d",
                  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I",
                  {VmValue::Ref(tag), VmValue::Ref(text), VmValue::Ref(throwable)})
                  .AsInt() == 0);
    }
}

TEST_CASE("EventLog write overloads preserve API 19 payload semantics") {
    for (const auto backend :
         {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm fixture(backend);
        const auto text = fixture.vm.NewStringUtf8("cookie.db\ncorrupt");
        const auto integer = fixture.New("Ljava/lang/Integer;", "(I)V",
                                         {VmValue::Int(42)});
        const auto wide = fixture.New("Ljava/lang/Long;", "(J)V",
                                      {VmValue::Long(INT64_C(9000000000))});
        const auto values = fixture.model.NewObjectArray(
            fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
            fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), 4);
        fixture.model.SetObjectElement(values, 0, text);
        fixture.model.SetObjectElement(values, 1, integer);
        fixture.model.SetObjectElement(values, 2, wide);
        fixture.model.SetObjectElement(values, 3, VmObjectRef{});
        const auto roots = fixture.vm.ProtectReferences(
            std::array{text, integer, wide, values});

        CHECK(fixture.Static("Landroid/util/EventLog;", "writeEvent", "(II)I",
                             {VmValue::Int(75004), VmValue::Int(7)}).AsInt() == 5);
        CHECK(fixture.Static("Landroid/util/EventLog;", "writeEvent", "(IJ)I",
                             {VmValue::Int(75004), VmValue::Long(8)}).AsInt() == 9);
        CHECK(fixture.Static("Landroid/util/EventLog;", "writeEvent",
                             "(ILjava/lang/String;)I",
                             {VmValue::Int(75004), VmValue::Ref(text)}).AsInt() == 23);
        CHECK(fixture.Static("Landroid/util/EventLog;", "writeEvent",
                             "(I[Ljava/lang/Object;)I",
                             {VmValue::Int(75004), VmValue::Ref(values)}).AsInt() == 48);

        const auto records = fixture.logger.Snapshot(
            ogplay::core::LogLevel::info, "runtime.dexvm.guest");
        REQUIRE(records.size() == 4);
        CHECK(records[0].message == "EventLog tag=75004 payload=int(7)");
        CHECK(records[1].message == "EventLog tag=75004 payload=long(8)");
        CHECK(records[2].message ==
              "EventLog tag=75004 payload=string(\"cookie.db\\ncorrupt\")");
        CHECK(records[3].message ==
              "EventLog tag=75004 payload=list[string(\"cookie.db\\ncorrupt\"), int(42), long(9000000000), string(\"NULL\")]");

        const auto bad = fixture.model.NewObjectArray(
            fixture.linker.ResolveDescriptor("[Ljava/lang/Object;"),
            fixture.linker.ResolveDescriptor("Ljava/lang/Object;"), 1);
        fixture.model.SetObjectElement(bad, 0,
                                       fixture.New("Ljava/lang/Object;"));
        const auto outcome = fixture.StaticOutcome(
            "Landroid/util/EventLog;", "writeEvent", "(I[Ljava/lang/Object;)I",
            {VmValue::Int(1), VmValue::Ref(bad)});
        REQUIRE(outcome.exception.IsValid());
        CHECK(fixture.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");

        const auto reads = fixture.StaticOutcome(
            "Landroid/util/EventLog;", "readEvents",
            "([ILjava/util/Collection;)V",
            {VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(reads.exception.IsValid());
        CHECK(fixture.linker.Class(reads.exception_class).descriptor ==
              "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST_CASE("DVM-176 Log.getStackTraceString follows API 19 throwable rules") {
    const auto super_construct = [](IntrinsicContext& call, const char* owner,
                                    const char* signature) {
        const auto type = call.vm.Linker().ResolveDescriptor(owner);
        const auto constructor =
            call.vm.Linker().FindDirectMethod(type, "<init>", signature);
        std::vector<VmValue> arguments{VmValue::Ref(call.receiver)};
        arguments.insert(arguments.end(), call.arguments.begin(),
                         call.arguments.end());
        const auto outcome = call.vm.Call(*constructor, arguments);
        if (outcome.exception.IsValid()) {
            throw VmJavaThrow{
                call.vm.Linker().Class(outcome.exception_class).descriptor,
                outcome.exception_message, outcome.exception};
        }
        return VmValue::Void();
    };
    for (const auto backend :
         {InterpreterBackend::switch_dispatch,
          InterpreterBackend::threaded}) {
        std::int32_t printed{};
        auto custom_host = IntrinsicClassBuilder::Class(
            "Ltest/CustomUnknownHost;", "Ljava/net/UnknownHostException;");
        custom_host.Constructor(
            "(Ljava/lang/String;)V",
            [&super_construct](IntrinsicContext& call) {
                return super_construct(call, "Ljava/net/UnknownHostException;",
                                       "(Ljava/lang/String;)V");
            });
        auto overridden = IntrinsicClassBuilder::Class(
            "Ltest/OverriddenTrace;", "Ljava/lang/RuntimeException;");
        overridden.Constructor("()V", [&super_construct](IntrinsicContext& call) {
            return super_construct(call, "Ljava/lang/RuntimeException;", "()V");
        });
        overridden.OverrideMethod(
            "printStackTrace", "(Ljava/io/PrintWriter;)V",
            [&printed](IntrinsicContext& call) {
                ++printed;
                const auto marker = call.vm.NewStringUtf8("override-marker");
                const auto writer = call.arguments[0].ref;
                const auto roots =
                    call.vm.ProtectReferences(std::array{marker, writer});
                const auto index = call.vm.Linker().FindVtableIndex(
                    call.vm.Model().ObjectClass(writer), "print",
                    "(Ljava/lang/String;)V");
                if (!index) {
                    throw DexVmError(DexVmErrorReason::unresolved_reference,
                                     "PrintWriter.print(String)");
                }
                const std::array arguments{
                    VmValue::Ref(writer), VmValue::Ref(marker)};
                const auto outcome = call.vm.Call(
                    call.vm.Linker().Class(
                        call.vm.Model().ObjectClass(writer)).vtable[*index],
                    arguments);
                if (outcome.exception.IsValid()) {
                    throw VmJavaThrow{
                        call.vm.Linker().Class(outcome.exception_class)
                            .descriptor,
                        outcome.exception_message, outcome.exception};
                }
                return VmValue::Void();
            });
        auto throwing = IntrinsicClassBuilder::Class(
            "Ltest/ThrowingTrace;", "Ljava/lang/RuntimeException;");
        throwing.Constructor("()V", [&super_construct](IntrinsicContext& call) {
            return super_construct(call, "Ljava/lang/RuntimeException;", "()V");
        });
        throwing.OverrideMethod(
            "printStackTrace", "(Ljava/io/PrintWriter;)V",
            [](IntrinsicContext&) -> VmValue {
                throw VmJavaThrow{"Ljava/lang/IllegalStateException;",
                                  "print failed"};
            });
        AndroidValueVm fixture(
            backend,
            {std::move(custom_host).Build(), std::move(overridden).Build(),
             std::move(throwing).Build()});
        const auto stack = [&](const VmObjectRef throwable) {
            return fixture.vm.StringUtf8(
                fixture.Static(
                    "Landroid/util/Log;", "getStackTraceString",
                    "(Ljava/lang/Throwable;)Ljava/lang/String;",
                    {VmValue::Ref(throwable)}).ref);
        };

        CHECK(stack(VmObjectRef{}) == "");

        const auto host_message = fixture.vm.NewStringUtf8("an.appads.com");
        const auto unknown_host = fixture.New(
            "Ljava/net/UnknownHostException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(host_message)});
        CHECK(stack(unknown_host) == "");

        const auto download = fixture.vm.NewStringUtf8("download failed");
        const auto nested = fixture.New(
            "Ljava/io/IOException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(download)});
        fixture.On(nested, "initCause",
                   "(Ljava/lang/Throwable;)Ljava/lang/Throwable;",
                   {VmValue::Ref(unknown_host)});
        CHECK(stack(nested) == "");

        const auto custom_message = fixture.vm.NewStringUtf8("nested.example");
        const auto custom = fixture.New(
            "Ltest/CustomUnknownHost;", "(Ljava/lang/String;)V",
            {VmValue::Ref(custom_message)});
        CHECK(stack(custom) == "");
        const auto wrapped_custom = fixture.New(
            "Ljava/io/IOException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(download)});
        fixture.On(wrapped_custom, "initCause",
                   "(Ljava/lang/Throwable;)Ljava/lang/Throwable;",
                   {VmValue::Ref(custom)});
        CHECK(stack(wrapped_custom) == "");

        const auto attach_frame = [&](const VmObjectRef throwable,
                                      const char* class_name,
                                      const char* method) {
            const auto declaring = fixture.vm.NewStringUtf8(class_name);
            const auto method_name = fixture.vm.NewStringUtf8(method);
            const auto file = fixture.vm.NewStringUtf8("Fixture.java");
            const auto element = fixture.New(
                "Ljava/lang/StackTraceElement;",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V",
                {VmValue::Ref(declaring), VmValue::Ref(method_name),
                 VmValue::Ref(file), VmValue::Int(42)});
            const auto array = fixture.model.NewObjectArray(
                fixture.linker.ResolveDescriptor(
                    "[Ljava/lang/StackTraceElement;"),
                fixture.linker.ResolveDescriptor(
                    "Ljava/lang/StackTraceElement;"),
                1);
            fixture.model.SetObjectElement(array, 0, element);
            const auto roots = fixture.vm.ProtectReferences(
                std::array{throwable, element, array});
            fixture.On(throwable, "setStackTrace",
                       "([Ljava/lang/StackTraceElement;)V",
                       {VmValue::Ref(array)});
        };

        const auto spoofed_message =
            fixture.vm.NewStringUtf8("UnknownHostException an.appads.com");
        const auto spoofed = fixture.New(
            "Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(spoofed_message)});
        attach_frame(spoofed, "test.Spoofed", "run");
        const auto spoofed_text = stack(spoofed);
        CHECK(spoofed_text.find("UnknownHostException an.appads.com") !=
              std::string::npos);
        CHECK(spoofed_text.find("java.lang.RuntimeException") !=
              std::string::npos);
        CHECK(spoofed_text.find("\tat test.Spoofed.run(Fixture.java:42)") !=
              std::string::npos);

        const auto cause_message = fixture.vm.NewStringUtf8("inner-detail");
        const auto cause = fixture.New(
            "Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(cause_message)});
        attach_frame(cause, "test.Cause", "fail");
        const auto outer_message = fixture.vm.NewStringUtf8("outer-detail");
        const auto outer = fixture.New(
            "Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(outer_message)});
        attach_frame(outer, "test.Outer", "act");
        fixture.On(outer, "initCause",
                   "(Ljava/lang/Throwable;)Ljava/lang/Throwable;",
                   {VmValue::Ref(cause)});
        const auto outer_text = stack(outer);
        CHECK(outer_text.find("java.lang.RuntimeException: outer-detail") !=
              std::string::npos);
        CHECK(outer_text.find("\tat test.Outer.act(Fixture.java:42)") !=
              std::string::npos);
        CHECK(outer_text.find("Caused by: java.lang.RuntimeException: inner-detail") !=
              std::string::npos);
        CHECK(outer_text.find("\tat test.Cause.fail(Fixture.java:42)") !=
              std::string::npos);

        printed = 0;
        const auto overridden_object = fixture.New("Ltest/OverriddenTrace;");
        CHECK(stack(overridden_object) == "override-marker");
        CHECK(printed == 1);

        const auto throwing_object = fixture.New("Ltest/ThrowingTrace;");
        const auto thrown = fixture.StaticOutcome(
            "Landroid/util/Log;", "getStackTraceString",
            "(Ljava/lang/Throwable;)Ljava/lang/String;",
            {VmValue::Ref(throwing_object)});
        CHECK(thrown.exception.IsValid());
        CHECK(fixture.linker.Class(thrown.exception_class).descriptor ==
              "Ljava/lang/IllegalStateException;");
        CHECK(thrown.exception_message == "print failed");
    }
}

TEST_CASE("DVM226 BootDex PrintStream captures bytes stack traces encoding and GC without logging") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto buffer = f.New("Ljava/io/ByteArrayOutputStream;");
        const auto stream = f.New("Ljava/io/PrintStream;", "(Ljava/io/OutputStream;)V", {VmValue::Ref(buffer)});
        const auto roots = f.vm.ProtectReferences(std::array{stream});
        const auto klass = f.model.ObjectClass(stream);
        CHECK(f.linker.Class(klass).is_boot_dex);
        CHECK(f.linker.Class(*f.linker.Class(klass).super).descriptor == "Ljava/io/FilterOutputStream;");
        f.On(stream, "print", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("中文"))});
        CHECK(f.On(stream, "append", "(C)Ljava/io/PrintStream;", {VmValue::Int('!')}).ref == stream);
        f.On(stream, "println", "(I)V", {VmValue::Int(42)});
        f.On(stream, "flush", "()V");
        CHECK(f.vm.StringUtf8(f.On(buffer, "toString", "(Ljava/lang/String;)Ljava/lang/String;",
            {VmValue::Ref(f.vm.NewStringUtf8("UTF-8"))}).ref) == "中文!42\n");
        CHECK(f.logger.Snapshot(std::nullopt, "runtime.dexvm.guest").empty());
        CHECK(f.vm.MarkReachable().IsMarked(buffer));
        static_cast<void>(f.vm.CollectGarbage());
        CHECK(f.model.IsValidRef(buffer));
        const auto error = f.New("Ljava/lang/RuntimeException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("original"))});
        const auto error_root = f.vm.ProtectReferences(std::array{error});
        const auto cause = f.New("Ljava/lang/IllegalArgumentException;", "(Ljava/lang/String;)V",
            {VmValue::Ref(f.vm.NewStringUtf8("cause"))});
        f.On(error, "initCause", "(Ljava/lang/Throwable;)Ljava/lang/Throwable;", {VmValue::Ref(cause)});
        f.On(error, "printStackTrace", "(Ljava/io/PrintStream;)V", {VmValue::Ref(stream)});
        f.On(stream, "flush", "()V");
        const auto trace = f.vm.StringUtf8(f.On(buffer, "toString", "()Ljava/lang/String;").ref);
        CHECK(trace.find("java.lang.RuntimeException: original") != std::string::npos);
        CHECK(trace.find("Caused by: java.lang.IllegalArgumentException: cause") != std::string::npos);
        CHECK(f.logger.Snapshot(std::nullopt, "runtime.dexvm.guest").empty());
        f.On(stream, "close", "()V");
        f.On(stream, "close", "()V");
        f.On(stream, "print", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("after-close"))});
        CHECK(f.On(stream, "checkError", "()Z").AsInt() == 1);
        CHECK(f.vm.StringUtf8(f.On(buffer, "toString", "()Ljava/lang/String;").ref) == trace);
        const auto encoded = f.New("Ljava/io/ByteArrayOutputStream;");
        const auto latin = f.New("Ljava/io/PrintStream;", "(Ljava/io/OutputStream;ZLjava/lang/String;)V",
            {VmValue::Ref(encoded), VmValue::Int(1), VmValue::Ref(f.vm.NewStringUtf8("ISO-8859-1"))});
        f.On(latin, "print", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("é"))});
        const auto bytes = f.On(encoded, "toByteArray", "()[B").ref;
        CHECK(f.model.ArrayLength(bytes) == 1);
        CHECK((f.model.GetPrimitiveElement(bytes, 0) & 255U) == 0xe9U);
        const auto construct = [&](const char* signature, std::vector<VmValue> args) {
            const auto object = f.vm.NewIntrinsicInstance("Ljava/io/PrintStream;");
            const auto method = f.linker.FindDirectMethod(klass, "<init>", signature);
            REQUIRE(method.has_value());
            args.insert(args.begin(), VmValue::Ref(object));
            return f.vm.Call(*method, args);
        };
        const auto null_stream = construct("(Ljava/io/OutputStream;)V", {VmValue::Ref(VmObjectRef{})});
        REQUIRE(null_stream.exception.IsValid());
        CHECK(f.linker.Class(null_stream.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto bad_charset = construct("(Ljava/io/OutputStream;ZLjava/lang/String;)V",
            {VmValue::Ref(buffer), VmValue::Int(0), VmValue::Ref(f.vm.NewStringUtf8("not-an-encoding"))});
        REQUIRE(bad_charset.exception.IsValid());
        CHECK(f.linker.Class(bad_charset.exception_class).descriptor == "Ljava/io/UnsupportedEncodingException;");
        const auto input = f.Bytes("!abc?");
        CHECK(f.vm.StringUtf8(f.New("Ljava/lang/String;", "([BIILjava/lang/String;)V",
            {VmValue::Ref(input), VmValue::Int(1), VmValue::Int(3), VmValue::Ref(f.vm.NewStringUtf8("UTF-8"))})) == "abc");
        const auto string_class = f.linker.ResolveDescriptor("Ljava/lang/String;");
        const auto string_ctor = f.linker.FindDirectMethod(string_class, "<init>", "([BIILjava/lang/String;)V");
        REQUIRE(string_ctor.has_value());
        const auto range_error = f.vm.Call(*string_ctor, std::array{
            VmValue::Ref(f.vm.NewIntrinsicInstance("Ljava/lang/String;")), VmValue::Ref(input),
            VmValue::Int(3), VmValue::Int(3), VmValue::Ref(f.vm.NewStringUtf8("UTF-8"))});
        REQUIRE(range_error.exception.IsValid());
        CHECK(f.linker.Class(range_error.exception_class).descriptor == "Ljava/lang/StringIndexOutOfBoundsException;");
    }
}

TEST_CASE("DVM226 PrintStream owns error and autoFlush semantics and System streams use only log sinks") {
    struct Target { std::vector<char> bytes; int flushes{}; int closes{}; bool fail_write{}; };
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        auto target = std::make_shared<Target>();
        auto out = IntrinsicClassBuilder::Class("Ltest/PrintTarget;", "Ljava/io/OutputStream;");
        out.Constructor("()V", [](IntrinsicContext&) { return VmValue::Void(); });
        out.OverrideMethod("write", "(I)V", [target](IntrinsicContext& c) {
            if (target->fail_write) throw VmJavaThrow{"Ljava/io/IOException;", "write failed"};
            target->bytes.push_back(static_cast<char>(c.arguments[0].AsInt()));
            return VmValue::Void();
        });
        out.OverrideMethod("flush", "()V", [target](IntrinsicContext&) { ++target->flushes; return VmValue::Void(); });
        out.OverrideMethod("close", "()V", [target](IntrinsicContext&) { ++target->closes; return VmValue::Void(); });
        AndroidValueVm f(backend, {std::move(out).Build()});
        const auto sink = f.New("Ltest/PrintTarget;");
        const auto stream = f.New("Ljava/io/PrintStream;", "(Ljava/io/OutputStream;Z)V", {VmValue::Ref(sink), VmValue::Int(1)});
        f.On(stream, "write", "(I)V", {VmValue::Int('x')});
        CHECK(target->flushes == 0);
        f.On(stream, "write", "(I)V", {VmValue::Int('\n')});
        CHECK(target->flushes == 1);
        CHECK(std::string(target->bytes.begin(), target->bytes.end()) == "x\n");
        target->fail_write = true;
        CHECK_FALSE(f.OnOutcome(stream, "write", "(I)V", {VmValue::Int('x')}).exception.IsValid());
        CHECK(f.On(stream, "checkError", "()Z").AsInt() == 1);
        f.On(stream, "close", "()V");
        f.On(stream, "close", "()V");
        CHECK(target->closes == 1);
        const auto initialized = f.vm.EnsureClassInitialized(f.linker.ResolveDescriptor("Ljava/lang/System;"));
        REQUIRE_FALSE(initialized.exception.IsValid());
        const auto system = f.linker.ResolveDescriptor("Ljava/lang/System;");
        const auto get = [&](const char* name) {
            const auto field = f.linker.FindFieldRecursive(system, name, "Ljava/io/PrintStream;");
            REQUIRE(field.has_value());
            return VmObjectRef{f.linker.Class(system).static_storage[f.linker.Field(*field).slot]};
        };
        const auto stdout_stream = get("out");
        const auto stderr_stream = get("err");
        CHECK(stdout_stream != stderr_stream);
        f.On(stdout_stream, "println", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("stdout 中文"))});
        f.On(stderr_stream, "println", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("stderr"))});
        f.On(stdout_stream, "append", "(Ljava/lang/CharSequence;)Ljava/io/PrintStream;", {VmValue::Ref(f.vm.NewStringUtf8(""))});
        auto logs = f.logger.Snapshot();
        REQUIRE(logs.size() == 2);
        CHECK(logs[0].message == "stdout 中文");
        CHECK(logs[1].message == "stderr");
        std::string stdout_bytes, stderr_bytes;
        f.vm.SetConsoleSink([&](std::string_view bytes) { stdout_bytes += bytes; },
                            [&](std::string_view bytes) { stderr_bytes += bytes; });
        f.On(stdout_stream, "println", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("out"))});
        f.On(stderr_stream, "println", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("err"))});
        CHECK(stdout_bytes == "out\n");
        CHECK(stderr_bytes == "err\n");
        f.vm.SetConsoleSink({});
        f.On(stdout_stream, "close", "()V");
        f.On(stderr_stream, "println", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("still-open"))});
        logs = f.logger.Snapshot();
        CHECK(logs.back().message == "still-open");
        CHECK(f.On(stdout_stream, "checkError", "()Z").AsInt() == 0);
        f.On(stdout_stream, "print", "(Ljava/lang/String;)V", {VmValue::Ref(f.vm.NewStringUtf8("closed"))});
        CHECK(f.On(stdout_stream, "checkError", "()Z").AsInt() == 1);
    }
}
