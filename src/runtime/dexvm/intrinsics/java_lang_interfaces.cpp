#include "catalog.h"
#include "shared.h"

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
    using namespace detail;

    namespace {
        // Top-level `java.lang` interfaces from pinned libcore
        // luni/src/main/java/java/lang (android-4.4.4_r2.0.1). Nested types such as
        // Thread.UncaughtExceptionHandler and java.lang.annotation.* stay out.

        IntrinsicClassDecl Declare_java_lang_Appendable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Appendable;");
            builder.UnimplementedVirtual("append", "(C)Ljava/lang/Appendable;");
            builder.UnimplementedVirtual("append", "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;");
            builder.UnimplementedVirtual("append", "(Ljava/lang/CharSequence;II)Ljava/lang/Appendable;");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_AutoCloseable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/AutoCloseable;");
            builder.UnimplementedVirtual("close", "()V");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_CharSequence() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/CharSequence;");
            builder.FinalMethod("length", "()I", [](IntrinsicContext& context) {
                return VmValue::Int(static_cast<std::int32_t>(
                        context.vm.Model().StringValue(context.receiver).size())
                );
            });
            builder.UnimplementedVirtual("charAt", "(I)C");
            builder.UnimplementedVirtual("subSequence", "(II)Ljava/lang/CharSequence;");
            builder.UnimplementedVirtual("toString", "()Ljava/lang/String;");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Cloneable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Cloneable;");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Comparable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Comparable;");
            builder.UnimplementedVirtual("compareTo", "(Ljava/lang/Object;)I");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Iterable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Iterable;");
            builder.UnimplementedVirtual("iterator", "()Ljava/util/Iterator;");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Readable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Readable;");
            builder.UnimplementedVirtual("read", "(Ljava/nio/CharBuffer;)I");
            return std::move(builder).Build();
        }

        IntrinsicClassDecl Declare_java_lang_Runnable() {
            auto builder = IntrinsicClassBuilder::Interface("Ljava/lang/Runnable;");
            builder.UnimplementedVirtual("run", "()V");
            return std::move(builder).Build();
        }
    } // namespace

    void AppendJavaLangInterfaces(std::vector<IntrinsicClassDecl>& catalog) {
        catalog.push_back(Declare_java_lang_Appendable());
        catalog.push_back(Declare_java_lang_AutoCloseable());
        catalog.push_back(Declare_java_lang_CharSequence());
        catalog.push_back(Declare_java_lang_Cloneable());
        catalog.push_back(Declare_java_lang_Comparable());
        catalog.push_back(Declare_java_lang_Iterable());
        catalog.push_back(Declare_java_lang_Readable());
        catalog.push_back(Declare_java_lang_Runnable());
    }
} // namespace ogplay::runtime::dexvm::intrinsics
