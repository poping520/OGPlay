#include "ogplay/runtime/dexvm/reflection.h"
// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from dalvik_system_PathClassLoader.cpp ----
#include "catalog.h"

#include <array>
#include <limits>
#include <boost/url/encode.hpp>
#include <boost/url/grammar/lut_chars.hpp>
#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_dalvik_system_BaseDexClassLoader_Shell() {
    auto builder = IntrinsicClassBuilder::Class("Ldalvik/system/BaseDexClassLoader;", "Ljava/lang/ClassLoader;");
    builder.MergesBootClass();
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_dalvik_system_PathClassLoader_Shell() {
    auto builder =
        IntrinsicClassBuilder::Class("Ldalvik/system/PathClassLoader;", "Ldalvik/system/BaseDexClassLoader;");
    builder.MergesBootClass();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics

#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "shared.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace detail {
namespace {

[[noreturn]] void MissingResourceBackend(IntrinsicContext& context) {
    if (auto* ledger = context.vm.Ledger()) {
        ledger->RecordUnimplemented("dexvm.classloader.resource_url", 0);
    }
    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "sealed classpath resource provider is unavailable"};
}

}  // namespace

VmObjectRef OpenClasspathResource(IntrinsicContext& context, const CoreIntrinsicServices& services,
                                  const CoreIntrinsicServices::ClasspathResource& resource) {
    if (!services.read_classpath_resource) MissingResourceBackend(context);
    std::optional<std::vector<std::byte>> bytes;
    try {
        bytes = services.read_classpath_resource(resource);
    } catch (const std::invalid_argument& error) {
        if (auto* ledger = context.vm.Ledger()) {
            ledger->RecordUnimplemented("dexvm.classloader.resource_url", 0);
        }
        throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
    } catch (const std::runtime_error& error) {
        throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
    }
    if (!bytes) {
        throw VmJavaThrow{"Ljava/io/FileNotFoundException;", resource.entry_name};
    }
    if (bytes->size() > static_cast<std::size_t>(std::numeric_limits<JniSize>::max())) {
        throw VmJavaThrow{"Ljava/io/IOException;", "classpath resource exceeds Java array limit"};
    }
    const auto array = context.vm.Model().NewPrimitiveArray(
        context.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte, static_cast<JniSize>(bytes->size()));
    const auto roots = context.vm.ProtectReferences(std::array{array});
    if (!bytes->empty()) context.vm.Model().WriteByteRegion(array, 0, *bytes);
    const auto stream = context.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
    const auto stream_roots = context.vm.ProtectReferences(std::array{stream});
    const auto constructor = context.vm.Linker().FindDirectMethod(
        context.vm.Linker().ResolveDescriptor("Ljava/io/ByteArrayInputStream;"), "<init>", "([B)V");
    if (!constructor) {
        throw DexVmError(DexVmErrorReason::internal_invariant, "ByteArrayInputStream constructor is not linked");
    }
    const auto outcome = context.vm.Call(*constructor, std::array{VmValue::Ref(stream), VmValue::Ref(array)});
    if (outcome.exception.IsValid()) {
        throw VmJavaThrow{context.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                          outcome.exception};
    }
    return stream;
}

}  // namespace detail

IntrinsicClassDecl Declare_java_lang_VMClassLoader_Natives(const CoreIntrinsicServices& services) {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/VMClassLoader;");
    b.MergesBootClass();
    b.StaticMethod(
        "findLoadedClass", "(Ljava/lang/ClassLoader;Ljava/lang/String;)Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto name = c.vm.StringUtf8(IntrinsicCall(c).NonNullRef(1, "name"));
            auto& loaders = c.vm.ClassLoaders();
            const auto loader = c.arguments[0].ref;
            const auto role = !loader.IsValid() || loader == loaders.BootstrapLoader()
                                  ? std::optional<VmClassLoaderId>(kBootstrapLoader)
                              : loaders.IsRomApplicationLoader(loader)
                                  ? std::optional<VmClassLoaderId>(kApplicationLoader)
                                  : std::nullopt;
            if (!role) return VmValue::Ref(VmObjectRef{});
            try {
                const auto type = loaders.FindLoadedClass(*role, name);
                return VmValue::Ref(type ? c.vm.Model().ClassObject(*type) : VmObjectRef{});
            } catch (const ClassNameCodecError&) {
                return VmValue::Ref(VmObjectRef{});
            }
        },
        kAccStatic | kAccNative);
    b.StaticMethod(
        "loadClass", "(Ljava/lang/String;Z)Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto name = c.vm.StringUtf8(IntrinsicCall(c).NonNullRef(0, "name"));
            try {
                return VmValue::Ref(c.vm.Model().ClassObject(c.vm.ClassLoaders().LoadClass(kBootstrapLoader, name)));
            } catch (const ClassNameCodecError&) {
                // dvmFindClassByName returns null with a pending ClassNotFoundException.
                c.vm.SetPendingException(c.vm.MakeThrowable("Ljava/lang/ClassNotFoundException;", name));
                return VmValue::Ref(VmObjectRef{});
            } catch (const DexVmError& error) {
                if (error.Reason() != DexVmErrorReason::unknown_class) throw;
                // dvmFindClassByName returns null with a pending ClassNotFoundException.
                c.vm.SetPendingException(c.vm.MakeThrowable("Ljava/lang/ClassNotFoundException;", name));
                return VmValue::Ref(VmObjectRef{});
            }
        },
        kAccStatic | kAccNative);
    b.StaticMethod(
        "getPrimitiveClass", "(C)Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const std::string descriptor(1, static_cast<char>(c.arguments[0].AsInt()));
            if (descriptor.find_first_not_of("ZBCSIJFDV") != std::string::npos)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid primitive type"};
            return VmValue::Ref(c.vm.Model().ClassObject(c.vm.Linker().ResolveDescriptor(descriptor)));
        },
        kAccStatic | kAccNative);
    b.StaticMethod(
        "getBootClassPathSize", "()I",
        [services](IntrinsicContext&) {
            return VmValue::Int(services.boot_classpath_size ? services.boot_classpath_size() : 0);
        },
        kAccPrivate | kAccStatic | kAccNative);
    b.StaticMethod(
        "getBootClassPathResource", "(Ljava/lang/String;I)Ljava/lang/String;",
        [services](IntrinsicContext& c) {
            const auto name = c.vm.StringUtf8(IntrinsicCall(c).NonNullRef(0, "name"));
            const auto resource = services.find_boot_classpath_resource
                                      ? services.find_boot_classpath_resource(name, c.arguments[1].AsInt())
                                      : std::nullopt;
            if (!resource) return VmValue::Ref(VmObjectRef{});
            // Dalvik Class.cpp::dvmGetBootPathResource concatenates the raw entry name.
            constexpr boost::urls::grammar::lut_chars safe{
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~/"};
            return VmValue::Ref(c.vm.NewStringUtf8("jar:file:" + boost::urls::encode(resource->archive_path, safe) +
                                                   "!/" + resource->entry_name));
        },
        kAccPrivate | kAccStatic | kAccNative);
    return std::move(b).Build();
}

IntrinsicClassDecl Declare_dalvik_system_VMStack_Natives() {
    auto b = IntrinsicClassBuilder::Class("Ldalvik/system/VMStack;");
    b.MergesBootClass();
    b.StaticMethod(
        "getClasses", "(I)[Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto limit = static_cast<std::uint32_t>(c.arguments[0].AsInt());
            return VmValue::Ref(c.vm.Reflection().MaterializeTypeArray(c.vm.CallingStackClasses(1, limit)));
        },
        kAccPublic | kAccNative);
    b.UnimplementedStatic("fillStackTraceElements", "(Ljava/lang/Thread;[Ljava/lang/StackTraceElement;)I",
                          kAccPublic | kAccNative);
    b.UnimplementedStatic("getThreadStackTrace", "(Ljava/lang/Thread;)[Ljava/lang/StackTraceElement;",
                          kAccPublic | kAccNative);
    b.StaticMethod(
        "getCallingClassLoader", "()Ljava/lang/ClassLoader;",
        [](IntrinsicContext& c) {
            // Dalvik dvmGetCaller2Class: native frame (absent here), Java caller, caller's caller.
            const auto stack = c.vm.CallingStackClasses(1, 1);
            if (stack.empty() || c.vm.Linker().ClassHeader(stack.front()).defining_loader == kBootstrapLoader)
                return VmValue::Ref(VmObjectRef{});
            return VmValue::Ref(c.vm.ClassLoaders().LoaderForClass(stack.front()));
        },
        kAccPublic | kAccNative);
    b.StaticMethod(
        "getStackClass2", "()Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            // Despite the Java name, Dalvik uses dvmGetCaller3Class: skip native + two Java frames.
            const auto stack = c.vm.CallingStackClasses(2, 1);
            return VmValue::Ref(stack.empty() ? VmObjectRef{} : c.vm.Model().ClassObject(stack.front()));
        },
        kAccPublic | kAccNative);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::dexvm::intrinsics
