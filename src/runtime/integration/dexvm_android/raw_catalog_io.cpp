// Catalog batch: java.io streams, readers, writers, File and Log.

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void AppendIoClasses(std::vector<Decl>& catalog) {
    {
        Decl stream;
        stream.descriptor = "Ljava/io/InputStream;";
        stream.superclass = "Ljava/lang/Object;";
        // Overridable: on the JDK these are plain virtuals and stream
        // subclasses (FilterInputStream chains in title code) override
        // them; super calls still reach the intrinsic.
        stream.methods = {
            {"read", "([BII)I", false, true, "android.stream.read_range"},
            {"read", "([B)I", false, true, "android.stream.read_full"},
            {"read", "()I", false, true, "android.stream.read_one"},
            {"available", "()I", false, true, "android.stream.available"},
            {"close", "()V", false, true, "android.stream.close"},
            {"skip", "(J)J", false, true, "android.stream.skip"},
        };
        catalog.push_back(std::move(stream));
    }
    {
        Decl file;
        file.descriptor = "Ljava/io/File;";
        file.superclass = "Ljava/lang/Object;";
        file.fields = {{"path", "Ljava/lang/String;", false, false, 0, ""}};
        file.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.file.init"},
            {"<init>", "(Ljava/lang/String;Ljava/lang/String;)V", false,
             false, "android.file.init_parent_child"},
            {"exists", "()Z", false, false, "android.file.exists"},
            {"length", "()J", false, false, "android.file.length"},
            {"getPath", "()Ljava/lang/String;", false, false,
             "android.file.get_path"},
            {"getAbsolutePath", "()Ljava/lang/String;", false, false,
             "android.file.get_path"},
            // Directories are implicit in the guest VFS, so creating them
            // trivially succeeds; delete only reaches the session overlay
            // (read-only mounts truthfully report failure).
            {"mkdir", "()Z", false, false, "android.file.mkdirs"},
            {"mkdirs", "()Z", false, false, "android.file.mkdirs"},
            {"createNewFile", "()Z", false, false,
             "android.file.create_new"},
            {"delete", "()Z", false, false, "android.file.delete"},
            {"isDirectory", "()Z", false, false,
             "android.file.is_directory"},
            {"list", "()[Ljava/lang/String;", false, false,
             "android.file.list"},
        };
        catalog.push_back(std::move(file));
        Decl file_input;
        file_input.descriptor = "Ljava/io/FileInputStream;";
        file_input.superclass = "Ljava/io/InputStream;";
        file_input.methods = {
            {"<init>", "(Ljava/io/File;)V", false, false,
             "android.file_stream.init_file"},
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.file_stream.init_path"},
        };
        catalog.push_back(std::move(file_input));
        // Filter/Buffered wrappers adopt the wrapped stream's record (the
        // single-owner convention documented on the reader family).
        Decl filter_input;
        filter_input.descriptor = "Ljava/io/FilterInputStream;";
        filter_input.superclass = "Ljava/io/InputStream;";
        filter_input.methods = {
            {"<init>", "(Ljava/io/InputStream;)V", false, false,
             "android.reader.adopt_stream"},
        };
        catalog.push_back(std::move(filter_input));
        Decl buffered_input;
        buffered_input.descriptor = "Ljava/io/BufferedInputStream;";
        buffered_input.superclass = "Ljava/io/FilterInputStream;";
        buffered_input.methods = {
            {"<init>", "(Ljava/io/InputStream;)V", false, false,
             "android.reader.adopt_stream"},
            {"<init>", "(Ljava/io/InputStream;I)V", false, false,
             "android.reader.adopt_stream"},
        };
        catalog.push_back(std::move(buffered_input));
        Decl output;
        output.descriptor = "Ljava/io/OutputStream;";
        output.superclass = "Ljava/lang/Object;";
        output.methods = {
            {"write", "([BII)V", false, true,
             "android.byte_output.write_range"},
            {"write", "([B)V", false, true,
             "android.file_output.write_bytes"},
            {"write", "(I)V", false, true, "android.output.write_one"},
            {"flush", "()V", false, true, "android.file_output.flush"},
            {"close", "()V", false, true, "android.file_output.close"},
        };
        catalog.push_back(std::move(output));
        Decl filter_output;
        filter_output.descriptor = "Ljava/io/FilterOutputStream;";
        filter_output.superclass = "Ljava/io/OutputStream;";
        filter_output.methods = {
            {"<init>", "(Ljava/io/OutputStream;)V", false, false,
             "android.output.adopt"},
        };
        catalog.push_back(std::move(filter_output));
        Decl buffered_output;
        buffered_output.descriptor = "Ljava/io/BufferedOutputStream;";
        buffered_output.superclass = "Ljava/io/FilterOutputStream;";
        buffered_output.methods = {
            {"<init>", "(Ljava/io/OutputStream;)V", false, false,
             "android.output.adopt"},
            {"<init>", "(Ljava/io/OutputStream;I)V", false, false,
             "android.output.adopt"},
        };
        catalog.push_back(std::move(buffered_output));
        Decl file_output;
        file_output.descriptor = "Ljava/io/FileOutputStream;";
        file_output.superclass = "Ljava/io/OutputStream;";
        file_output.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.file_output.init_path"},
            {"<init>", "(Ljava/io/File;)V", false, false,
             "android.file_output.init_file"},
            {"write", "([B)V", false, false,
             "android.file_output.write_bytes"},
            {"flush", "()V", false, false, "android.file_output.flush"},
            {"close", "()V", false, false, "android.file_output.close"},
        };
        catalog.push_back(std::move(file_output));
        // Reader family: byte streams with line decoding on top. Wrapper
        // constructors adopt the wrapped stream's record (the wrapped
        // object becomes unusable, matching single-owner usage).
        Decl reader;
        reader.descriptor = "Ljava/io/Reader;";
        reader.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(reader));
        Decl file_reader;
        file_reader.descriptor = "Ljava/io/FileReader;";
        file_reader.superclass = "Ljava/io/Reader;";
        file_reader.methods = {
            {"<init>", "(Ljava/lang/String;)V", false, false,
             "android.file_stream.init_path"},
            {"<init>", "(Ljava/io/File;)V", false, false,
             "android.file_stream.init_file"},
        };
        catalog.push_back(std::move(file_reader));
        Decl stream_reader;
        stream_reader.descriptor = "Ljava/io/InputStreamReader;";
        stream_reader.superclass = "Ljava/io/Reader;";
        stream_reader.methods = {
            {"<init>", "(Ljava/io/InputStream;)V", false, false,
             "android.reader.adopt_stream"},
            {"<init>",
             "(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V", false,
             false, "android.reader.adopt_stream"},
        };
        catalog.push_back(std::move(stream_reader));
        Decl buffered_reader;
        buffered_reader.descriptor = "Ljava/io/BufferedReader;";
        buffered_reader.superclass = "Ljava/io/Reader;";
        buffered_reader.methods = {
            {"<init>", "(Ljava/io/Reader;)V", false, false,
             "android.reader.adopt_stream"},
            {"readLine", "()Ljava/lang/String;", false, false,
             "android.reader.read_line"},
            {"ready", "()Z", false, false, "android.reader.ready"},
            {"close", "()V", false, false, "android.stream.close"},
        };
        catalog.push_back(std::move(buffered_reader));
        Decl charset;
        charset.descriptor = "Ljava/nio/charset/Charset;";
        charset.superclass = "Ljava/lang/Object;";
        charset.methods = {
            {"forName",
             "(Ljava/lang/String;)Ljava/nio/charset/Charset;", true, false,
             "android.charset.for_name"},
        };
        catalog.push_back(std::move(charset));
        Decl byte_input;
        byte_input.descriptor = "Ljava/io/ByteArrayInputStream;";
        byte_input.superclass = "Ljava/io/InputStream;";
        byte_input.methods = {
            {"<init>", "([B)V", false, false,
             "android.byte_stream.init_input"},
        };
        catalog.push_back(std::move(byte_input));
        Decl data_input;
        data_input.descriptor = "Ljava/io/DataInputStream;";
        data_input.superclass = "Ljava/io/InputStream;";
        data_input.methods = {
            {"<init>", "(Ljava/io/InputStream;)V", false, false,
             "android.reader.adopt_stream"},
            {"readFully", "([B)V", false, false,
             "android.data_input.read_fully"},
            {"skipBytes", "(I)I", false, false,
             "android.data_input.skip_bytes"},
            {"readInt", "()I", false, false, "android.data_input.read_int"},
            {"readLong", "()J", false, false,
             "android.data_input.read_long"},
            {"readUTF", "()Ljava/lang/String;", false, false,
             "android.data_input.read_utf"},
            {"close", "()V", false, false, "android.stream.close"},
        };
        catalog.push_back(std::move(data_input));
        Decl byte_output;
        byte_output.descriptor = "Ljava/io/ByteArrayOutputStream;";
        byte_output.superclass = "Ljava/io/OutputStream;";
        byte_output.methods = {
            {"<init>", "()V", false, false,
             "android.byte_output.init"},
            {"write", "([BII)V", false, false,
             "android.byte_output.write_range"},
            {"write", "([B)V", false, false,
             "android.file_output.write_bytes"},
            {"toByteArray", "()[B", false, false,
             "android.byte_output.to_byte_array"},
            {"size", "()I", false, false, "android.byte_output.size"},
            {"toString", "()Ljava/lang/String;", false, false,
             "android.byte_output.to_string"},
            {"close", "()V", false, false, "android.graphics.noop"},
        };
        catalog.push_back(std::move(byte_output));
        Decl writer;
        writer.descriptor = "Ljava/io/Writer;";
        writer.superclass = "Ljava/lang/Object;";
        catalog.push_back(std::move(writer));
        Decl file_writer;
        file_writer.descriptor = "Ljava/io/FileWriter;";
        file_writer.superclass = "Ljava/io/Writer;";
        file_writer.methods = {
            {"<init>", "(Ljava/io/File;Z)V", false, false,
             "android.file_writer.init_file_append"},
            {"append", "(C)Ljava/io/Writer;", false, false,
             "android.file_writer.append_char"},
            {"append", "(Ljava/lang/CharSequence;)Ljava/io/Writer;", false,
             false, "android.file_writer.append_sequence"},
            {"flush", "()V", false, false, "android.file_output.flush"},
            {"close", "()V", false, false, "android.file_output.close"},
        };
        catalog.push_back(std::move(file_writer));
        // Real zip reading over an adopted stream (title data installers
        // unpack asset zips at first launch). Entries inflate through the
        // strict loader ZIP reader; malformed input throws IOException.
        Decl zip_entry;
        zip_entry.descriptor = "Ljava/util/zip/ZipEntry;";
        zip_entry.superclass = "Ljava/lang/Object;";
        zip_entry.fields = {{"name", "Ljava/lang/String;", false, false, 0,
                             ""}};
        zip_entry.methods = {
            {"getName", "()Ljava/lang/String;", false, false,
             "android.zip_entry.get_name"},
            {"isDirectory", "()Z", false, false,
             "android.zip_entry.is_directory"},
        };
        catalog.push_back(std::move(zip_entry));
        Decl zip_input;
        zip_input.descriptor = "Ljava/util/zip/ZipInputStream;";
        zip_input.superclass = "Ljava/io/FilterInputStream;";
        zip_input.methods = {
            {"<init>", "(Ljava/io/InputStream;)V", false, false,
             "android.zip_input.init"},
            {"getNextEntry", "()Ljava/util/zip/ZipEntry;", false, false,
             "android.zip_input.get_next_entry"},
            {"read", "([BII)I", false, false,
             "android.zip_input.read_range"},
            {"closeEntry", "()V", false, false,
             "android.zip_input.close_entry"},
            {"close", "()V", false, false, "android.zip_input.close"},
        };
        catalog.push_back(std::move(zip_input));
        Decl data_output;
        data_output.descriptor = "Ljava/io/DataOutputStream;";
        data_output.superclass = "Ljava/io/OutputStream;";
        data_output.methods = {
            {"<init>", "(Ljava/io/OutputStream;)V", false, false,
             "android.data_output.init"},
            {"writeUTF", "(Ljava/lang/String;)V", false, false,
             "android.data_output.write_utf"},
            {"close", "()V", false, false, "android.data_output.close"},
        };
        catalog.push_back(std::move(data_output));
    }
    {
        Decl log;
        log.descriptor = "Landroid/util/Log;";
        log.superclass = "Ljava/lang/Object;";
        log.methods = {
            {"d", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.d"},
            {"e", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.e"},
            {"i", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.i"},
            {"w", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.w"},
            {"v", "(Ljava/lang/String;Ljava/lang/String;)I", true, false,
             "android.log.d"},
            {"e",
             "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I",
             true, false, "android.log.e"},
        };
        catalog.push_back(std::move(log));
    }
}

}  // namespace ogplay::runtime::android_intrinsics
