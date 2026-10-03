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

IntrinsicClassDecl Declare_dalvik_system_PathClassLoader() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ldalvik/system/PathClassLoader;", "Ljava/lang/ClassLoader;");
    builder.UnimplementedConstructor(
        "(Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    builder.UnimplementedConstructor(
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_BootClassLoader.cpp ----
#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_BootClassLoader() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/BootClassLoader;", "Ljava/lang/ClassLoader;", {}, kAccNone);
    builder.UnimplementedConstructor("()V");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_ClassLoader.cpp ----
#include "catalog.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "shared.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

struct ClassLoaderFields final {
    IntrinsicFieldHandle parent;
};

[[nodiscard]] VmClassLoaderId ReceiverRole(IntrinsicContext& context) {
    const auto role = context.vm.ClassLoaders().RoleOf(context.receiver);
    if (!role.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "ClassLoader receiver has no loader role");
    }
    return *role;
}

[[nodiscard]] VmValue Load(IntrinsicContext& context,
                           const VmClassLoaderId role) {
    IntrinsicCall call(context);
    const auto name_ref = call.NonNullRef(0, "className");
    const auto name = context.vm.StringUtf8(name_ref);
    try {
        const auto java_class = context.vm.ClassLoaders().LoadClass(role, name);
        return VmValue::Ref(context.vm.Model().ClassObject(java_class));
    } catch (const ClassNameCodecError&) {
        throw VmJavaThrow{"Ljava/lang/ClassNotFoundException;", name};
    } catch (const DexVmError& error) {
        if (error.Reason() == DexVmErrorReason::unknown_class) {
            throw VmJavaThrow{"Ljava/lang/ClassNotFoundException;", name};
        }
        throw VmJavaThrow{"Ljava/lang/LinkageError;", error.what()};
    }
}

}  // namespace

namespace detail {
namespace {

[[noreturn]] void MissingResourceBackend(IntrinsicContext& context) {
    if (auto* ledger = context.vm.Ledger()) {
        ledger->RecordUnimplemented("dexvm.classloader.resource_url", 0);
    }
    throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                      "sealed classpath resource provider is unavailable"};
}

std::optional<CoreIntrinsicServices::ClasspathResource> FindResource(
    IntrinsicContext& context, const CoreIntrinsicServices& services,
    CoreIntrinsicServices::ClasspathLoader loader, std::string_view name) {
    if (!services.find_classpath_resource) MissingResourceBackend(context);
    return services.find_classpath_resource(loader, name);
}

}  // namespace

VmObjectRef OpenClasspathResource(
    IntrinsicContext& context, const CoreIntrinsicServices& services,
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
        context.vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
        static_cast<JniSize>(bytes->size()));
    const auto roots = context.vm.ProtectReferences(std::array{array});
    if (!bytes->empty()) context.vm.Model().WriteByteRegion(array, 0, *bytes);
    const auto stream = context.vm.NewIntrinsicInstance("Ljava/io/ByteArrayInputStream;");
    const auto stream_roots = context.vm.ProtectReferences(std::array{stream});
    const auto constructor = context.vm.Linker().FindDirectMethod(
        context.vm.Linker().ResolveDescriptor("Ljava/io/ByteArrayInputStream;"),
        "<init>", "([B)V");
    if (!constructor) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "ByteArrayInputStream constructor is not linked");
    }
    const auto outcome = context.vm.Call(
        *constructor, std::array{VmValue::Ref(stream), VmValue::Ref(array)});
    if (outcome.exception.IsValid()) {
        throw VmJavaThrow{
            context.vm.Linker().Class(outcome.exception_class).descriptor,
            outcome.exception_message, outcome.exception};
    }
    return stream;
}

VmObjectRef ClasspathResourceUrl(
    IntrinsicContext& context, const CoreIntrinsicServices& services,
    CoreIntrinsicServices::ClasspathLoader loader, std::string_view name) {
    const auto resource = FindResource(context, services, loader, name);
    if (!resource) return VmObjectRef{};
    constexpr boost::urls::grammar::lut_chars safe{
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~/"};
    const auto spec = context.vm.NewStringUtf8(
        "jar:file:" + boost::urls::encode(resource->archive_path, safe) + "!/" +
        boost::urls::encode(resource->entry_name, safe));
    const auto roots = context.vm.ProtectReferences(std::array{spec});
    const auto url = context.vm.NewIntrinsicInstance("Ljava/net/URL;");
    const auto url_roots = context.vm.ProtectReferences(std::array{url});
    const auto constructor = context.vm.Linker().FindDirectMethod(
        context.vm.Linker().ResolveDescriptor("Ljava/net/URL;"),
        "<init>", "(Ljava/lang/String;)V");
    if (!constructor) throw DexVmError(DexVmErrorReason::internal_invariant,
                                      "URL constructor is not linked");
    const auto outcome = context.vm.Call(
        *constructor, std::array{VmValue::Ref(url), VmValue::Ref(spec)});
    if (outcome.exception.IsValid()) throw VmJavaThrow{
        context.vm.Linker().Class(outcome.exception_class).descriptor,
        outcome.exception_message, outcome.exception};
    return url;
}

VmObjectRef ClasspathResourceStream(
    IntrinsicContext& context, const CoreIntrinsicServices& services,
    CoreIntrinsicServices::ClasspathLoader loader, std::string_view name) {
    const auto resource = FindResource(context, services, loader, name);
    if (!resource) return VmObjectRef{};
    try {
        return OpenClasspathResource(context, services, *resource);
    } catch (const VmJavaThrow& error) {
        // API19 ClassLoader.getResourceAsStream catches IOException only.
        const auto type = context.vm.Linker().ResolveDescriptor(error.descriptor);
        const auto io = context.vm.Linker().ResolveDescriptor("Ljava/io/IOException;");
        if (context.vm.Linker().IsAssignable(io, type)) return VmObjectRef{};
        throw;
    }
}

}  // namespace detail

IntrinsicClassDecl Declare_java_lang_ClassLoader(
    const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/ClassLoader;", "Ljava/lang/Object;", {},
        kAccPublic | kAccAbstract);
    const ClassLoaderFields fields{
        builder.BoundInstanceField("parent", "Ljava/lang/ClassLoader;",
                                   kAccPrivate),
    };

    // AOSP API19: libcore ClassLoader.java :: ClassLoader()/ClassLoader(parent)
    builder.Constructor("()V", [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        call.SetRef(fields.parent,
                    context.vm.ClassLoaders().ApplicationLoader());
        return VmValue::Void();
    }, kAccProtected);
    builder.Constructor("(Ljava/lang/ClassLoader;)V",
                        [fields](IntrinsicContext& context) {
        IntrinsicCall call(context);
        call.SetRef(fields.parent, call.NonNullRef(0, "parentLoader"));
        return VmValue::Void();
    }, kAccProtected);

    builder.StaticMethod(
        "getSystemClassLoader", "()Ljava/lang/ClassLoader;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(
                context.vm.ClassLoaders().ApplicationLoader());
        });
    builder.FinalMethod(
        "getParent", "()Ljava/lang/ClassLoader;",
        [fields](IntrinsicContext& context) {
            const auto fixed =
                context.vm.ClassLoaders().FacadeParent(context.receiver);
            if (fixed.has_value()) return VmValue::Ref(*fixed);
            return VmValue::Ref(IntrinsicCall(context).GetRef(fields.parent));
        });
    builder.FinalMethod(
        "findLoadedClass",
        "(Ljava/lang/String;)Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto name_ref = call.NonNullRef(0, "className");
            try {
                const auto found = context.vm.ClassLoaders().FindLoadedClass(
                    ReceiverRole(context), context.vm.StringUtf8(name_ref));
                return VmValue::Ref(
                    found.has_value()
                        ? context.vm.Model().ClassObject(*found)
                        : VmObjectRef{});
            } catch (const ClassNameCodecError&) {
                return VmValue::Ref(VmObjectRef(0));
            }
        }, kAccProtected);
    builder.FinalMethod(
        "findSystemClass",
        "(Ljava/lang/String;)Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            return Load(context, kApplicationLoader);
        }, kAccProtected);
    builder.VirtualMethod(
        "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            return Load(context, ReceiverRole(context));
        });
    builder.VirtualMethod(
        "loadClass", "(Ljava/lang/String;Z)Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            // API-19 Dalvik ignores resolve for ClassLoader.loadClass.
            return Load(context, ReceiverRole(context));
        }, kAccProtected);
    builder.VirtualMethod(
        "findClass", "(Ljava/lang/String;)Ljava/lang/Class;",
        [](IntrinsicContext& context) -> VmValue {
            IntrinsicCall call(context);
            const auto name = context.vm.StringUtf8(
                call.NonNullRef(0, "className"));
            throw VmJavaThrow{"Ljava/lang/ClassNotFoundException;", name};
        }, kAccProtected);
    builder.VirtualMethod(
        "getResourceAsStream", "(Ljava/lang/String;)Ljava/io/InputStream;",
        [services](IntrinsicContext& context) {
            const auto name = context.vm.StringUtf8(
                IntrinsicCall(context).NonNullRef(0, "resourceName"));
            const auto role = ReceiverRole(context) == kBootstrapLoader
                ? CoreIntrinsicServices::ClasspathLoader::bootstrap
                : CoreIntrinsicServices::ClasspathLoader::application;
            return VmValue::Ref(detail::ClasspathResourceStream(
                context, services, role, name));
        });
    builder.VirtualMethod(
        "getResources", "(Ljava/lang/String;)Ljava/util/Enumeration;",
        [](IntrinsicContext& context) {
            static_cast<void>(IntrinsicCall(context).NonNullRef(0, "resourceName"));
            if (auto* ledger = context.vm.Ledger()) {
                ledger->RecordUnimplemented(
                    "dexvm.classloader.resource_enumeration", 0);
            }
            const auto vector_class =
                context.vm.Linker().ResolveDescriptor("Ljava/util/Vector;");
            auto outcome = context.vm.EnsureClassInitialized(vector_class);
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{
                    context.vm.Linker().Class(outcome.exception_class).descriptor,
                    outcome.exception_message, outcome.exception};
            }
            const auto vector = context.vm.NewIntrinsicInstance("Ljava/util/Vector;");
            const auto root = context.vm.ProtectReferences(std::array{vector});
            const auto constructor = context.vm.Linker().FindDirectMethod(
                vector_class, "<init>", "()V");
            if (!constructor) {
                throw DexVmError(DexVmErrorReason::internal_invariant,
                                 "Vector constructor is not linked");
            }
            outcome = context.vm.Call(*constructor,
                                      std::array{VmValue::Ref(vector)});
            if (outcome.exception.IsValid()) {
                throw VmJavaThrow{
                    context.vm.Linker().Class(outcome.exception_class).descriptor,
                    outcome.exception_message, outcome.exception};
            }
            return detail::InvokeGuest(context.vm, vector, "elements",
                                       "()Ljava/util/Enumeration;");
        });
    builder.VirtualMethod(
        "getResource", "(Ljava/lang/String;)Ljava/net/URL;",
        [services](IntrinsicContext& context) {
            const auto name = context.vm.StringUtf8(
                IntrinsicCall(context).NonNullRef(0, "resourceName"));
            const auto role = ReceiverRole(context) == kBootstrapLoader
                ? CoreIntrinsicServices::ClasspathLoader::bootstrap
                : CoreIntrinsicServices::ClasspathLoader::application;
            return VmValue::Ref(detail::ClasspathResourceUrl(context, services, role, name));
        });
    builder.StaticMethod(
        "getSystemResource", "(Ljava/lang/String;)Ljava/net/URL;",
        [services](IntrinsicContext& context) {
            const auto name = context.vm.StringUtf8(
                IntrinsicCall(context).NonNullRef(0, "resourceName"));
            return VmValue::Ref(detail::ClasspathResourceUrl(
                context, services, CoreIntrinsicServices::ClasspathLoader::application, name));
        });
    builder.StaticMethod(
        "getSystemResourceAsStream", "(Ljava/lang/String;)Ljava/io/InputStream;",
        [services](IntrinsicContext& context) {
            const auto name = context.vm.StringUtf8(
                IntrinsicCall(context).NonNullRef(0, "resourceName"));
            return VmValue::Ref(detail::ClasspathResourceStream(
                context, services, CoreIntrinsicServices::ClasspathLoader::application, name));
        });
    builder.FinalMethod(
        "resolveClass", "(Ljava/lang/Class;)V",
        [](IntrinsicContext& context) {
            static_cast<void>(IntrinsicCall(context).NonNullRef(0, "clazz"));
            return VmValue::Void();
        }, kAccProtected);

    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_dalvik_system_VMStack() {
    auto b = IntrinsicClassBuilder::Class("Ldalvik/system/VMStack;");
    b.StaticMethod("getClasses", "(I)[Ljava/lang/Class;", [](IntrinsicContext& c) {
        const auto limit = static_cast<std::uint32_t>(c.arguments[0].AsInt());
        return VmValue::Ref(c.vm.Reflection().MaterializeTypeArray(c.vm.CallingStackClasses(1, limit)));
    }, kAccPublic | kAccNative);
    b.UnimplementedStatic("fillStackTraceElements", "(Ljava/lang/Thread;[Ljava/lang/StackTraceElement;)I", kAccPublic | kAccNative);
    b.UnimplementedStatic("getThreadStackTrace", "(Ljava/lang/Thread;)[Ljava/lang/StackTraceElement;", kAccPublic | kAccNative);
    b.UnimplementedStatic("getCallingClassLoader", "()Ljava/lang/ClassLoader;", kAccPublic | kAccNative);
    b.UnimplementedStatic("getStackClass2", "()Ljava/lang/Class;", kAccPublic | kAccNative);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::dexvm::intrinsics
