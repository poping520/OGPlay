// Built-in java.* core intrinsic declarations. These are the classes the
// interpreter itself depends on (Object identity, String form, Throwable
// with the implicit exception hierarchy). Handlers named here are bound by
// the interpreter's built-in registry (interpreter_builtins.cpp);
// platform-level android.* classes come from PlatformClassProvider instead.
//
// Semantics source: java.* native halves follow AOSP vm/native/java_lang_*
// at the pinned baseline; pure-Java halves follow JLS/class-library docs
// (07 §2 mode B, libcore is deliberately not vendored).

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] IntrinsicClassDecl Exception(const std::string& descriptor,
                                           const std::string& super) {
    IntrinsicClassDecl decl;
    decl.descriptor = descriptor;
    decl.superclass = super;
    decl.methods = {
        {"<init>", "()V", false, false, "core.throwable.init"},
        {"<init>", "(Ljava/lang/String;)V", false, false,
         "core.throwable.init_message"},
    };
    return decl;
}

}  // namespace

std::vector<IntrinsicClassDecl> CoreIntrinsicCatalog() {
    std::vector<IntrinsicClassDecl> catalog;

    {
        IntrinsicClassDecl object;
        object.descriptor = "Ljava/lang/Object;";
        object.methods = {
            {"<init>", "()V", false, false, "core.object.init"},
            {"equals", "(Ljava/lang/Object;)Z", false, true,
             "core.object.equals"},
            {"hashCode", "()I", false, true, "core.object.hash_code"},
            {"toString", "()Ljava/lang/String;", false, true,
             "core.object.to_string"},
            {"getClass", "()Ljava/lang/Class;", false, false,
             "core.object.get_class"},
        };
        catalog.push_back(std::move(object));
    }
    {
        IntrinsicClassDecl runnable;
        runnable.descriptor = "Ljava/lang/Runnable;";
        runnable.is_interface = true;
        catalog.push_back(std::move(runnable));
    }
    {
        IntrinsicClassDecl sequence;
        sequence.descriptor = "Ljava/lang/CharSequence;";
        sequence.is_interface = true;
        catalog.push_back(std::move(sequence));
    }
    {
        IntrinsicClassDecl comparable;
        comparable.descriptor = "Ljava/lang/Comparable;";
        comparable.is_interface = true;
        catalog.push_back(std::move(comparable));
    }
    {
        IntrinsicClassDecl serializable;
        serializable.descriptor = "Ljava/io/Serializable;";
        serializable.is_interface = true;
        catalog.push_back(std::move(serializable));
    }
    {
        IntrinsicClassDecl string;
        string.descriptor = "Ljava/lang/String;";
        string.superclass = "Ljava/lang/Object;";
        string.interfaces = {"Ljava/lang/CharSequence;",
                             "Ljava/lang/Comparable;",
                             "Ljava/io/Serializable;"};
        string.methods = {
            {"length", "()I", false, false, "core.string.length"},
            {"charAt", "(I)C", false, false, "core.string.char_at"},
            {"equals", "(Ljava/lang/Object;)Z", false, false,
             "core.string.equals"},
            {"hashCode", "()I", false, false, "core.string.hash_code"},
            {"toString", "()Ljava/lang/String;", false, false,
             "core.string.to_string"},
        };
        catalog.push_back(std::move(string));
    }
    {
        IntrinsicClassDecl class_class;
        class_class.descriptor = "Ljava/lang/Class;";
        class_class.superclass = "Ljava/lang/Object;";
        class_class.methods = {
            {"getName", "()Ljava/lang/String;", false, false,
             "core.class.get_name"},
        };
        catalog.push_back(std::move(class_class));
    }
    {
        IntrinsicClassDecl throwable;
        throwable.descriptor = "Ljava/lang/Throwable;";
        throwable.superclass = "Ljava/lang/Object;";
        throwable.interfaces = {"Ljava/io/Serializable;"};
        throwable.methods = {
            {"<init>", "()V", false, false, "core.throwable.init"},
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "core.throwable.init_message"},
            {"getMessage", "()Ljava/lang/String;", false, true,
             "core.throwable.get_message"},
            {"toString", "()Ljava/lang/String;", false, true,
             "core.throwable.to_string"},
        };
        catalog.push_back(std::move(throwable));
    }

    catalog.push_back(Exception("Ljava/lang/Exception;",
                                "Ljava/lang/Throwable;"));
    catalog.push_back(Exception("Ljava/lang/RuntimeException;",
                                "Ljava/lang/Exception;"));
    catalog.push_back(Exception("Ljava/lang/NullPointerException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/ArithmeticException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/IndexOutOfBoundsException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/ArrayIndexOutOfBoundsException;",
                                "Ljava/lang/IndexOutOfBoundsException;"));
    catalog.push_back(Exception("Ljava/lang/StringIndexOutOfBoundsException;",
                                "Ljava/lang/IndexOutOfBoundsException;"));
    catalog.push_back(Exception("Ljava/lang/ClassCastException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/NegativeArraySizeException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/ArrayStoreException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/IllegalMonitorStateException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/IllegalArgumentException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/IllegalStateException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/UnsupportedOperationException;",
                                "Ljava/lang/RuntimeException;"));
    catalog.push_back(Exception("Ljava/lang/ClassNotFoundException;",
                                "Ljava/lang/Exception;"));
    catalog.push_back(Exception("Ljava/lang/InterruptedException;",
                                "Ljava/lang/Exception;"));
    catalog.push_back(Exception("Ljava/io/IOException;",
                                "Ljava/lang/Exception;"));

    catalog.push_back(Exception("Ljava/lang/Error;",
                                "Ljava/lang/Throwable;"));
    catalog.push_back(Exception("Ljava/lang/LinkageError;",
                                "Ljava/lang/Error;"));
    catalog.push_back(Exception("Ljava/lang/NoClassDefFoundError;",
                                "Ljava/lang/LinkageError;"));
    catalog.push_back(Exception("Ljava/lang/UnsatisfiedLinkError;",
                                "Ljava/lang/LinkageError;"));
    catalog.push_back(Exception("Ljava/lang/VirtualMachineError;",
                                "Ljava/lang/Error;"));
    catalog.push_back(Exception("Ljava/lang/StackOverflowError;",
                                "Ljava/lang/VirtualMachineError;"));
    catalog.push_back(Exception("Ljava/lang/OutOfMemoryError;",
                                "Ljava/lang/VirtualMachineError;"));

    return catalog;
}

}  // namespace ogplay::runtime::dexvm
