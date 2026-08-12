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
            // Cooperative single-thread model (04 §3): a timed wait cannot
            // observe cross-thread progress inside one tick, so it returns
            // as an elapsed timeout. Untimed wait() would deadlock and is
            // deliberately not declared.
            {"wait", "(J)V", false, false, "core.object.wait_timed"},
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
        // invoke-interface dispatches through the receiver's vtable; the
        // handler key here is never called directly.
        sequence.methods = {
            {"length", "()I", false, false, "core.string.length"},
        };
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
            // Constructors bind the value into the freshly allocated
            // instance (JavaObjectModel::BindString). Byte decoding uses
            // the platform default charset (UTF-8) with U+FFFD
            // replacement, matching CodingErrorAction.REPLACE.
            {"<init>", "()V", false, false, "core.string.init_empty"},
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "core.string.init_copy"},
            {"<init>", "([B)V", false, false, "core.string.init_bytes"},
            {"<init>", "([BII)V", false, false,
             "core.string.init_bytes_range"},
            {"<init>", "([BLjava/lang/String;)V", false, false,
             "core.string.init_bytes_charset"},
            {"<init>", "([C)V", false, false, "core.string.init_chars"},
            {"<init>", "([CII)V", false, false,
             "core.string.init_chars_range"},
            {"getBytes", "()[B", false, false, "core.string.get_bytes"},
            {"length", "()I", false, false, "core.string.length"},
            {"charAt", "(I)C", false, false, "core.string.char_at"},
            {"equals", "(Ljava/lang/Object;)Z", false, false,
             "core.string.equals"},
            {"equalsIgnoreCase", "(Ljava/lang/String;)Z", false, false,
             "core.string.equals_ignore_case"},
            {"hashCode", "()I", false, false, "core.string.hash_code"},
            {"toString", "()Ljava/lang/String;", false, false,
             "core.string.to_string"},
            {"compareTo", "(Ljava/lang/String;)I", false, false,
             "core.string.compare_to"},
            {"compareToIgnoreCase", "(Ljava/lang/String;)I", false, false,
             "core.string.compare_to_ignore_case"},
            {"concat", "(Ljava/lang/String;)Ljava/lang/String;", false,
             false, "core.string.concat"},
            {"startsWith", "(Ljava/lang/String;)Z", false, false,
             "core.string.starts_with"},
            {"endsWith", "(Ljava/lang/String;)Z", false, false,
             "core.string.ends_with"},
            {"indexOf", "(I)I", false, false, "core.string.index_of_char"},
            {"indexOf", "(II)I", false, false,
             "core.string.index_of_char_from"},
            {"contains", "(Ljava/lang/CharSequence;)Z", false, false,
             "core.string.contains"},
            {"getChars", "(II[CI)V", false, false,
             "core.string.get_chars"},
            {"toCharArray", "()[C", false, false,
             "core.string.to_char_array"},
            {"replace",
             "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)"
             "Ljava/lang/String;",
             false, false, "core.string.replace_seq"},
            {"replaceAll",
             "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
             false, false, "core.string.replace_all"},
            {"split", "(Ljava/lang/String;)[Ljava/lang/String;", false,
             false, "core.string.split"},
            {"indexOf", "(Ljava/lang/String;)I", false, false,
             "core.string.index_of_string"},
            {"lastIndexOf", "(I)I", false, false,
             "core.string.last_index_of_char"},
            {"lastIndexOf", "(Ljava/lang/String;)I", false, false,
             "core.string.last_index_of_string"},
            {"substring", "(I)Ljava/lang/String;", false, false,
             "core.string.substring_from"},
            {"substring", "(II)Ljava/lang/String;", false, false,
             "core.string.substring_range"},
            {"toLowerCase", "()Ljava/lang/String;", false, false,
             "core.string.to_lower"},
            {"toUpperCase", "()Ljava/lang/String;", false, false,
             "core.string.to_upper"},
            {"trim", "()Ljava/lang/String;", false, false,
             "core.string.trim"},
            {"isEmpty", "()Z", false, false, "core.string.is_empty"},
            {"valueOf", "(I)Ljava/lang/String;", true, false,
             "core.string.value_of_int"},
            {"valueOf", "(J)Ljava/lang/String;", true, false,
             "core.string.value_of_long"},
            {"valueOf", "(F)Ljava/lang/String;", true, false,
             "core.string.value_of_float"},
            {"valueOf", "(D)Ljava/lang/String;", true, false,
             "core.string.value_of_double"},
            {"valueOf", "(Z)Ljava/lang/String;", true, false,
             "core.string.value_of_boolean"},
            {"valueOf", "(C)Ljava/lang/String;", true, false,
             "core.string.value_of_char"},
            {"valueOf", "(Ljava/lang/Object;)Ljava/lang/String;", true,
             false, "core.string.value_of_object"},
        };
        catalog.push_back(std::move(string));
    }
    {
        IntrinsicClassDecl builder;
        builder.descriptor = "Ljava/lang/StringBuilder;";
        builder.superclass = "Ljava/lang/Object;";
        builder.interfaces = {"Ljava/lang/CharSequence;"};
        builder.methods = {
            {"<init>", "()V", false, false, "core.builder.init"},
            {"<init>", "(I)V", false, false, "core.builder.init_capacity"},
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "core.builder.init_string"},
            {"append", "(Ljava/lang/String;)Ljava/lang/StringBuilder;",
             false, false, "core.builder.append_string"},
            {"append", "(Ljava/lang/Object;)Ljava/lang/StringBuilder;",
             false, false, "core.builder.append_object"},
            {"append", "(I)Ljava/lang/StringBuilder;", false, false,
             "core.builder.append_int"},
            {"append", "(J)Ljava/lang/StringBuilder;", false, false,
             "core.builder.append_long"},
            {"append", "(Z)Ljava/lang/StringBuilder;", false, false,
             "core.builder.append_boolean"},
            {"append", "(C)Ljava/lang/StringBuilder;", false, false,
             "core.builder.append_char"},
            {"append", "(F)Ljava/lang/StringBuilder;", false, false,
             "core.builder.append_float"},
            {"append", "(D)Ljava/lang/StringBuilder;", false, false,
             "core.builder.append_double"},
            {"toString", "()Ljava/lang/String;", false, false,
             "core.builder.to_string"},
            {"length", "()I", false, false, "core.builder.length"},
            {"charAt", "(I)C", false, false, "core.builder.char_at"},
            {"setCharAt", "(IC)V", false, false,
             "core.builder.set_char_at"},
            {"deleteCharAt", "(I)Ljava/lang/StringBuilder;", false, false,
             "core.builder.delete_char_at"},
            {"insert", "(IC)Ljava/lang/StringBuilder;", false, false,
             "core.builder.insert_char"},
        };
        catalog.push_back(std::move(builder));
        // StringBuffer shares the builder buffer handlers; the cooperative
        // single-thread model makes the synchronization difference moot.
        IntrinsicClassDecl buffer;
        buffer.descriptor = "Ljava/lang/StringBuffer;";
        buffer.superclass = "Ljava/lang/Object;";
        buffer.interfaces = {"Ljava/lang/CharSequence;"};
        buffer.methods = {
            {"<init>", "()V", false, false, "core.builder.init"},
            {"<init>", "(I)V", false, false, "core.builder.init_capacity"},
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "core.builder.init_string"},
            {"append", "(Ljava/lang/String;)Ljava/lang/StringBuffer;",
             false, false, "core.builder.append_string"},
            {"append", "(Ljava/lang/Object;)Ljava/lang/StringBuffer;",
             false, false, "core.builder.append_object"},
            {"append", "(I)Ljava/lang/StringBuffer;", false, false,
             "core.builder.append_int"},
            {"append", "(J)Ljava/lang/StringBuffer;", false, false,
             "core.builder.append_long"},
            {"append", "(Z)Ljava/lang/StringBuffer;", false, false,
             "core.builder.append_boolean"},
            {"append", "(C)Ljava/lang/StringBuffer;", false, false,
             "core.builder.append_char"},
            {"append", "(F)Ljava/lang/StringBuffer;", false, false,
             "core.builder.append_float"},
            {"append", "(D)Ljava/lang/StringBuffer;", false, false,
             "core.builder.append_double"},
            {"toString", "()Ljava/lang/String;", false, false,
             "core.builder.to_string"},
            {"length", "()I", false, false, "core.builder.length"},
            {"charAt", "(I)C", false, false, "core.builder.char_at"},
            {"setCharAt", "(IC)V", false, false,
             "core.builder.set_char_at"},
            {"deleteCharAt", "(I)Ljava/lang/StringBuffer;", false, false,
             "core.builder.delete_char_at"},
            {"insert", "(IC)Ljava/lang/StringBuffer;", false, false,
             "core.builder.insert_char"},
        };
        catalog.push_back(std::move(buffer));
    }
    {
        IntrinsicClassDecl system;
        system.descriptor = "Ljava/lang/System;";
        system.superclass = "Ljava/lang/Object;";
        system.clinit_handler = "core.system.clinit";
        system.fields = {
            {"out", "Ljava/io/PrintStream;", true, false, 0, ""},
            {"err", "Ljava/io/PrintStream;", true, false, 0, ""},
        };
        system.methods = {
            {"arraycopy",
             "(Ljava/lang/Object;ILjava/lang/Object;II)V", true, false,
             "core.system.arraycopy"},
            {"gc", "()V", true, false, "core.system.gc"},
            {"setProperty",
             "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
             true, false, "core.system.set_property"},
            // Time, loadLibrary and exit are platform actions: handlers are
            // injected at session assembly (unified Clock / ELF loader).
            {"currentTimeMillis", "()J", true, false,
             "platform.system.current_time_millis"},
            {"nanoTime", "()J", true, false, "platform.system.nano_time"},
            {"loadLibrary", "(Ljava/lang/String;)V", true, false,
             "platform.system.load_library"},
            {"exit", "(I)V", true, false, "platform.system.exit"},
        };
        catalog.push_back(std::move(system));
    }
    {
        IntrinsicClassDecl stream;
        stream.descriptor = "Ljava/io/PrintStream;";
        stream.superclass = "Ljava/lang/Object;";
        stream.methods = {
            {"println", "(Ljava/lang/String;)V", false, false,
             "core.printstream.println_string"},
            {"println", "(I)V", false, false,
             "core.printstream.println_int"},
            {"println", "()V", false, false,
             "core.printstream.println_empty"},
            {"print", "(Ljava/lang/String;)V", false, false,
             "core.printstream.print_string"},
        };
        catalog.push_back(std::move(stream));
    }
    {
        IntrinsicClassDecl math;
        math.descriptor = "Ljava/lang/Math;";
        math.superclass = "Ljava/lang/Object;";
        math.methods = {
            {"abs", "(I)I", true, false, "core.math.abs_int"},
            {"abs", "(J)J", true, false, "core.math.abs_long"},
            {"abs", "(F)F", true, false, "core.math.abs_float"},
            {"abs", "(D)D", true, false, "core.math.abs_double"},
            {"max", "(II)I", true, false, "core.math.max_int"},
            {"min", "(II)I", true, false, "core.math.min_int"},
            {"max", "(FF)F", true, false, "core.math.max_float"},
            {"min", "(FF)F", true, false, "core.math.min_float"},
            {"sqrt", "(D)D", true, false, "core.math.sqrt"},
            {"sin", "(D)D", true, false, "core.math.sin"},
            {"cos", "(D)D", true, false, "core.math.cos"},
            {"atan2", "(DD)D", true, false, "core.math.atan2"},
            {"pow", "(DD)D", true, false, "core.math.pow"},
            {"floor", "(D)D", true, false, "core.math.floor"},
            {"ceil", "(D)D", true, false, "core.math.ceil"},
        };
        catalog.push_back(std::move(math));
    }
    {
        IntrinsicClassDecl number;
        number.descriptor = "Ljava/lang/Number;";
        number.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(number));
    }
    {
        IntrinsicClassDecl boxed;
        boxed.descriptor = "Ljava/lang/Integer;";
        boxed.superclass = "Ljava/lang/Number;";
        boxed.fields = {{"value", "I", false, false, 0, ""}};
        boxed.methods = {
            {"<init>", "(I)V", false, false, "core.integer.init"},
            {"valueOf", "(I)Ljava/lang/Integer;", true, false,
             "core.integer.value_of"},
            {"intValue", "()I", false, false, "core.integer.int_value"},
            {"parseInt", "(Ljava/lang/String;)I", true, false,
             "core.integer.parse_int"},
            {"toString", "()Ljava/lang/String;", false, false,
             "core.integer.to_string"},
            {"toString", "(I)Ljava/lang/String;", true, false,
             "core.integer.to_string_static"},
        };
        catalog.push_back(std::move(boxed));
    }
    {
        IntrinsicClassDecl boxed;
        boxed.descriptor = "Ljava/lang/Long;";
        boxed.superclass = "Ljava/lang/Number;";
        boxed.fields = {{"value", "J", false, false, 0, ""}};
        boxed.methods = {
            {"<init>", "(J)V", false, false, "core.long.init"},
            {"valueOf", "(J)Ljava/lang/Long;", true, false,
             "core.long.value_of"},
            {"longValue", "()J", false, false, "core.long.long_value"},
            {"parseLong", "(Ljava/lang/String;)J", true, false,
             "core.long.parse_long"},
            {"toString", "()Ljava/lang/String;", false, false,
             "core.long.to_string"},
        };
        catalog.push_back(std::move(boxed));
    }
    {
        IntrinsicClassDecl boxed;
        boxed.descriptor = "Ljava/lang/Boolean;";
        boxed.superclass = "Ljava/lang/Object;";
        boxed.fields = {{"value", "Z", false, false, 0, ""}};
        boxed.methods = {
            {"<init>", "(Z)V", false, false, "core.boolean.init"},
            {"valueOf", "(Z)Ljava/lang/Boolean;", true, false,
             "core.boolean.value_of"},
            {"booleanValue", "()Z", false, false,
             "core.boolean.boolean_value"},
        };
        catalog.push_back(std::move(boxed));
    }
    {
        IntrinsicClassDecl boxed;
        boxed.descriptor = "Ljava/lang/Float;";
        boxed.superclass = "Ljava/lang/Number;";
        // TYPE is the primitive float class object, published at class
        // initialization (Array.newInstance consumers).
        boxed.fields = {{"value", "F", false, false, 0, ""},
                        {"TYPE", "Ljava/lang/Class;", true, false, 0, ""}};
        boxed.clinit_handler = "core.float.clinit";
        boxed.methods = {
            {"<init>", "(F)V", false, false, "core.float.init"},
            {"valueOf", "(F)Ljava/lang/Float;", true, false,
             "core.float.value_of"},
            {"floatValue", "()F", false, false, "core.float.float_value"},
        };
        catalog.push_back(std::move(boxed));
    }
    {
        IntrinsicClassDecl boxed;
        boxed.descriptor = "Ljava/lang/Double;";
        boxed.superclass = "Ljava/lang/Number;";
        boxed.fields = {{"value", "D", false, false, 0, ""}};
        boxed.methods = {
            {"<init>", "(D)V", false, false, "core.double.init"},
            {"valueOf", "(D)Ljava/lang/Double;", true, false,
             "core.double.value_of"},
            {"doubleValue", "()D", false, false, "core.double.double_value"},
        };
        catalog.push_back(std::move(boxed));
    }
    {
        const std::vector<IntrinsicMethodDecl> map_methods = {
            {"<init>", "()V", false, false, "core.map.init"},
            {"<init>", "(I)V", false, false, "core.map.init_capacity"},
            {"put",
             "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
             false, false, "core.map.put"},
            {"get", "(Ljava/lang/Object;)Ljava/lang/Object;", false, false,
             "core.map.get"},
            {"containsKey", "(Ljava/lang/Object;)Z", false, false,
             "core.map.contains_key"},
            {"remove", "(Ljava/lang/Object;)Ljava/lang/Object;", false,
             false, "core.map.remove"},
            {"size", "()I", false, false, "core.map.size"},
            {"clear", "()V", false, false, "core.map.clear"},
            {"isEmpty", "()Z", false, false, "core.map.is_empty"},
        };
        IntrinsicClassDecl hash_map;
        hash_map.descriptor = "Ljava/util/HashMap;";
        hash_map.superclass = "Ljava/lang/Object;";
        hash_map.methods = map_methods;
        catalog.push_back(std::move(hash_map));
        IntrinsicClassDecl hashtable;
        hashtable.descriptor = "Ljava/util/Hashtable;";
        hashtable.superclass = "Ljava/lang/Object;";
        hashtable.methods = map_methods;
        catalog.push_back(std::move(hashtable));
    }
    {
        IntrinsicClassDecl list_interface;
        list_interface.descriptor = "Ljava/util/List;";
        list_interface.is_interface = true;
        list_interface.methods = {
            {"add", "(Ljava/lang/Object;)Z", false, false,
             "core.list.add"},
            {"get", "(I)Ljava/lang/Object;", false, false,
             "core.list.get"},
            {"size", "()I", false, false, "core.list.size"},
            {"iterator", "()Ljava/util/Iterator;", false, false,
             "core.list.iterator"},
            {"contains", "(Ljava/lang/Object;)Z", false, false,
             "core.list.contains"},
        };
        catalog.push_back(std::move(list_interface));
        const std::vector<IntrinsicMethodDecl> list_methods = {
            {"<init>", "()V", false, false, "core.list.init"},
            {"<init>", "(I)V", false, false, "core.list.init_capacity"},
            {"add", "(Ljava/lang/Object;)Z", false, false, "core.list.add"},
            {"addElement", "(Ljava/lang/Object;)V", false, false,
             "core.list.add_element"},
            {"get", "(I)Ljava/lang/Object;", false, false, "core.list.get"},
            {"elementAt", "(I)Ljava/lang/Object;", false, false,
             "core.list.get"},
            {"set", "(ILjava/lang/Object;)Ljava/lang/Object;", false, false,
             "core.list.set"},
            {"setElementAt", "(Ljava/lang/Object;I)V", false, false,
             "core.list.set_element_at"},
            {"removeElementAt", "(I)V", false, false,
             "core.list.remove_at"},
            {"remove", "(I)Ljava/lang/Object;", false, false,
             "core.list.remove_returning"},
            {"size", "()I", false, false, "core.list.size"},
            {"clear", "()V", false, false, "core.list.clear"},
            {"isEmpty", "()Z", false, false, "core.list.is_empty"},
            {"iterator", "()Ljava/util/Iterator;", false, false,
             "core.list.iterator"},
            {"contains", "(Ljava/lang/Object;)Z", false, false,
             "core.list.contains"},
            {"copyInto", "([Ljava/lang/Object;)V", false, false,
             "core.list.copy_into"},
        };
        IntrinsicClassDecl vector;
        vector.descriptor = "Ljava/util/Vector;";
        vector.superclass = "Ljava/lang/Object;";
        vector.interfaces = {"Ljava/util/List;"};
        vector.methods = list_methods;
        catalog.push_back(std::move(vector));
        IntrinsicClassDecl array_list;
        array_list.descriptor = "Ljava/util/ArrayList;";
        array_list.superclass = "Ljava/lang/Object;";
        array_list.interfaces = {"Ljava/util/List;"};
        array_list.methods = list_methods;
        catalog.push_back(std::move(array_list));
    }
    {
        // Deterministic PRNG (unified-Clock world: an unseeded Random gets
        // a per-instance deterministic seed so replays reproduce).
        IntrinsicClassDecl random;
        random.descriptor = "Ljava/util/Random;";
        random.superclass = "Ljava/lang/Object;";
        random.methods = {
            {"<init>", "()V", false, false, "core.random.init"},
            {"<init>", "(J)V", false, false, "core.random.init_seed"},
            {"nextDouble", "()D", false, false, "core.random.next_double"},
            {"nextInt", "(I)I", false, false, "core.random.next_int_bound"},
            {"nextInt", "()I", false, false, "core.random.next_int"},
            {"nextFloat", "()F", false, false, "core.random.next_float"},
            {"nextLong", "()J", false, false, "core.random.next_long"},
        };
        catalog.push_back(std::move(random));
    }
    {
        // Wall-clock facts come from the platform clock (injected at
        // session assembly like System.currentTimeMillis).
        IntrinsicClassDecl date;
        date.descriptor = "Ljava/util/Date;";
        date.superclass = "Ljava/lang/Object;";
        date.methods = {
            {"<init>", "()V", false, false, "platform.date.init"},
            {"getTime", "()J", false, false, "platform.date.get_time"},
            {"getYear", "()I", false, false, "platform.date.get_year"},
        };
        date.fields = {{"millis", "J", false, false, 0, ""}};
        catalog.push_back(std::move(date));
    }
    {
        IntrinsicClassDecl iterator_interface;
        iterator_interface.descriptor = "Ljava/util/Iterator;";
        iterator_interface.is_interface = true;
        iterator_interface.methods = {
            {"hasNext", "()Z", false, false, "core.iterator.has_next"},
            {"next", "()Ljava/lang/Object;", false, false,
             "core.iterator.next"},
        };
        catalog.push_back(std::move(iterator_interface));
        // Host-provided iterator over the shared list storage; instances
        // only come from List.iterator().
        IntrinsicClassDecl iterator_impl;
        iterator_impl.descriptor = "Ljava/util/CollectionIterator;";
        iterator_impl.superclass = "Ljava/lang/Object;";
        iterator_impl.interfaces = {"Ljava/util/Iterator;"};
        iterator_impl.fields = {
            {"list", "Ljava/lang/Object;", false, false, 0, ""},
            {"index", "I", false, false, 0, ""},
        };
        iterator_impl.methods = {
            {"hasNext", "()Z", false, false, "core.iterator.has_next"},
            {"next", "()Ljava/lang/Object;", false, false,
             "core.iterator.next"},
        };
        catalog.push_back(std::move(iterator_impl));
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
        // GC-A never collects (04 §5), so a weak reference truthfully stays
        // reachable for the whole session.
        IntrinsicClassDecl weak_reference;
        weak_reference.descriptor = "Ljava/lang/ref/WeakReference;";
        weak_reference.superclass = "Ljava/lang/Object;";
        weak_reference.fields = {
            {"referent", "Ljava/lang/Object;", false, false, 0, ""},
        };
        weak_reference.methods = {
            {"<init>", "(Ljava/lang/Object;)V", false, false,
             "core.weak_reference.init"},
            {"get", "()Ljava/lang/Object;", false, false,
             "core.weak_reference.get"},
        };
        catalog.push_back(std::move(weak_reference));
        // Reflective array construction is real (typed arrays from the
        // object model); the rest of java.lang.reflect stays a non-goal.
        IntrinsicClassDecl reflect_array;
        reflect_array.descriptor = "Ljava/lang/reflect/Array;";
        reflect_array.superclass = "Ljava/lang/Object;";
        reflect_array.methods = {
            {"newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;", true,
             false, "core.reflect.array_new_instance"},
        };
        catalog.push_back(std::move(reflect_array));
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
            {"printStackTrace", "()V", false, false,
             "core.throwable.print_stack_trace"},
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
    catalog.push_back(Exception("Ljava/io/FileNotFoundException;",
                                "Ljava/io/IOException;"));
    catalog.push_back(Exception("Ljava/io/UnsupportedEncodingException;",
                                "Ljava/io/IOException;"));
    catalog.push_back(Exception("Ljava/util/regex/PatternSyntaxException;",
                                "Ljava/lang/IllegalArgumentException;"));
    catalog.push_back(Exception("Ljava/util/NoSuchElementException;",
                                "Ljava/lang/RuntimeException;"));
    // Network exception family: referenced by catch clauses on the titles'
    // (never successful) network paths.
    catalog.push_back(Exception("Ljava/net/SocketException;",
                                "Ljava/io/IOException;"));
    catalog.push_back(Exception("Ljava/net/SocketTimeoutException;",
                                "Ljava/io/IOException;"));
    catalog.push_back(Exception("Ljava/net/UnknownHostException;",
                                "Ljava/io/IOException;"));
    catalog.push_back(Exception("Ljava/net/MalformedURLException;",
                                "Ljava/io/IOException;"));
    catalog.push_back(Exception("Ljava/io/EOFException;",
                                "Ljava/io/IOException;"));
    catalog.push_back(Exception("Ljava/lang/NumberFormatException;",
                                "Ljava/lang/IllegalArgumentException;"));
    catalog.push_back(Exception("Ljava/lang/IllegalThreadStateException;",
                                "Ljava/lang/IllegalArgumentException;"));

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
