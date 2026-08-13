// InputStream/Reader/Writer and java.io.File handlers. Streams read APK
// entries, files go through the session VFS; absent paths answer the
// documented Java values instead of faking success.

#include <algorithm>
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/vfs/vfs.h"

#include "shared.h"

namespace ogplay::runtime::android_intrinsics {

void PopulateStreams(AndroidHandlers& handlers,
                     const Context& context) {
    handlers.handler_android_stream_read_one = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        if (stream.cursor >= stream.bytes.size()) {
            return dx::VmValue::Int(-1);
        }
        return dx::VmValue::Int(static_cast<std::int32_t>(
            static_cast<std::uint8_t>(stream.bytes[stream.cursor++])));
    });
    // Wrapper constructors adopt the wrapped stream's record: the wrapper
    // handle takes ownership and the wrapped object becomes closed.
    handlers.handler_android_reader_adopt_stream = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto target = call.arguments[0].ref;
        const auto found = context->streams.find(target.Value());
        if (found == context->streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "wrapped stream is closed or was never "
                                  "opened"};
        }
        context->streams[call.receiver.Value()] =
            std::move(found->second);
        context->streams.erase(target.Value());
        return dx::VmValue::Void();
    });
    handlers.handler_android_reader_read_line = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        if (stream.cursor >= stream.bytes.size()) {
            return dx::VmValue::Ref(dx::VmObjectRef{});  // EOF is null
        }
        // Line terminators: \n, \r\n or \r; decoded as UTF-8 (the
        // platform default; explicit charsets are not tracked yet).
        std::string line;
        while (stream.cursor < stream.bytes.size()) {
            const auto byte =
                static_cast<char>(stream.bytes[stream.cursor++]);
            if (byte == '\n') break;
            if (byte == '\r') {
                if (stream.cursor < stream.bytes.size() &&
                    static_cast<char>(stream.bytes[stream.cursor]) ==
                        '\n') {
                    ++stream.cursor;
                }
                break;
            }
            line.push_back(byte);
        }
        return dx::VmValue::Ref(call.vm.NewStringUtf8(line));
    });
    handlers.handler_android_reader_ready = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        return dx::VmValue::Int(
            stream.cursor < stream.bytes.size() ? 1 : 0);
    });
    handlers.handler_android_charset_for_name = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Ljava/nio/charset/Charset;"));
    });
    handlers.handler_android_byte_stream_init_input = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        context->streams[call.receiver.Value()] =
            DexVmAndroidContext::Stream{
                model.ReadByteRegion(array, 0, model.ArrayLength(array)),
                0, false};
        return dx::VmValue::Void();
    });
    handlers.handler_android_data_input_read_fully = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto wanted =
            static_cast<std::size_t>(model.ArrayLength(array));
        if (stream.bytes.size() - stream.cursor < wanted) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "readFully hit end of stream"};
        }
        model.WriteByteRegion(
            array, 0,
            std::span(stream.bytes).subspan(stream.cursor, wanted));
        stream.cursor += wanted;
        return dx::VmValue::Void();
    });
    // Big-endian primitive reads with the documented EOFException when the
    // stream runs out (the honest end-of-data signal read loops rely on).
    const auto take_bytes = [context](dx::IntrinsicContext& call,
                                      const std::size_t wanted) {
        auto& stream = StreamOf(call, context);
        if (stream.bytes.size() - stream.cursor < wanted) {
            throw dx::VmJavaThrow{"Ljava/io/EOFException;",
                                  "end of stream"};
        }
        const auto begin = stream.cursor;
        stream.cursor += wanted;
        return std::span(stream.bytes).subspan(begin, wanted);
    };
    handlers.handler_android_data_input_read_int = dx::IntrinsicHandler([take_bytes](dx::IntrinsicContext& call) {
        const auto bytes = take_bytes(call, 4);
        std::uint32_t value = 0;
        for (const auto byte : bytes) {
            value = (value << 8U) | static_cast<std::uint8_t>(byte);
        }
        return dx::VmValue::Int(static_cast<std::int32_t>(value));
    });
    handlers.handler_android_data_input_read_long = dx::IntrinsicHandler([take_bytes](dx::IntrinsicContext& call) {
        const auto bytes = take_bytes(call, 8);
        std::uint64_t value = 0;
        for (const auto byte : bytes) {
            value = (value << 8U) | static_cast<std::uint8_t>(byte);
        }
        return dx::VmValue::Long(static_cast<std::int64_t>(value));
    });
    handlers.handler_android_data_input_read_utf = dx::IntrinsicHandler([take_bytes](dx::IntrinsicContext& call) {
        const auto length_bytes = take_bytes(call, 2);
        const auto length = static_cast<std::size_t>(
            (static_cast<std::uint8_t>(length_bytes[0]) << 8U) |
            static_cast<std::uint8_t>(length_bytes[1]));
        const auto bytes = take_bytes(call, length);
        // writeUTF pairs with this reader; the session writes plain
        // ASCII/UTF-8 subset, which modified UTF-8 matches byte-for-byte
        // for code points below 0x80. Non-ASCII goes through the string
        // store's UTF-8 decoding unchanged.
        std::string value(reinterpret_cast<const char*>(bytes.data()),
                          bytes.size());
        return dx::VmValue::Ref(call.vm.NewStringUtf8(value));
    });
    handlers.handler_android_data_input_skip_bytes = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto wanted = call.arguments[0].AsInt();
        const auto amount = std::min<std::size_t>(
            wanted > 0 ? static_cast<std::size_t>(wanted) : 0,
            stream.bytes.size() - stream.cursor);
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    handlers.handler_android_byte_output_init = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        // No path: bytes stay in memory and never publish to a file.
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{{}, {}, false};
        return dx::VmValue::Void();
    });
    handlers.handler_android_byte_output_write_range = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() ||
            found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "output stream is closed"};
        }
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        if (offset < 0 || length < 0 ||
            static_cast<std::int64_t>(offset) + length >
                model.ArrayLength(array)) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IndexOutOfBoundsException;",
                "write range exceeds the source array"};
        }
        const auto bytes = model.ReadByteRegion(array, offset, length);
        found->second.bytes.insert(found->second.bytes.end(), bytes.begin(),
                                   bytes.end());
        return dx::VmValue::Void();
    });
    const auto byte_output_of = [context](dx::IntrinsicContext& call)
        -> DexVmAndroidContext::OutputStream& {
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end()) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "output stream was never opened"};
        }
        return found->second;
    };
    handlers.handler_android_byte_output_to_byte_array = dx::IntrinsicHandler([byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        auto& vm = call.vm;
        const auto array_class = vm.Linker().ResolveDescriptor("[B");
        const auto array = vm.Model().NewPrimitiveArray(
            array_class, JniPrimitiveKind::byte,
            static_cast<JniSize>(output.bytes.size()));
        if (!output.bytes.empty()) {
            vm.Model().WriteByteRegion(array, 0, output.bytes);
        }
        return dx::VmValue::Ref(array);
    });
    handlers.handler_android_byte_output_size = dx::IntrinsicHandler([byte_output_of](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            byte_output_of(call).bytes.size()));
    });
    handlers.handler_android_byte_output_to_string = dx::IntrinsicHandler([byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        return dx::VmValue::Ref(call.vm.NewStringUtf8(std::string(
            reinterpret_cast<const char*>(output.bytes.data()),
            output.bytes.size())));
    });
    handlers.handler_android_file_writer_append_char = dx::IntrinsicHandler([byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        // BMP code unit encoded as UTF-8 (ASCII fast path; otherwise a
        // string round-trip through the interpreter's UTF-8 rendering).
        const auto unit = static_cast<char16_t>(
            call.arguments[0].cat1 & 0xffffU);
        std::string encoded;
        if (unit < 0x80U) {
            encoded.push_back(static_cast<char>(unit));
        } else {
            encoded = call.vm.StringUtf8(
                call.vm.Model().NewString(std::u16string(1, unit)));
        }
        for (const auto character : encoded) {
            output.bytes.push_back(static_cast<std::byte>(character));
        }
        return dx::VmValue::Ref(call.receiver);
    });
    handlers.handler_android_file_writer_append_sequence = dx::IntrinsicHandler([byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        const auto value = call.arguments[0].ref;
        const auto text = value.IsValid()
                              ? call.vm.StringUtf8(value)
                              : std::string("null");
        for (const auto character : text) {
            output.bytes.push_back(static_cast<std::byte>(character));
        }
        return dx::VmValue::Ref(call.receiver);
    });
    handlers.handler_android_stream_read_range = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        if (offset < 0 || length < 0) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IndexOutOfBoundsException;",
                "negative stream read range"};
        }
        const auto remaining = stream.bytes.size() - stream.cursor;
        if (remaining == 0) return dx::VmValue::Int(-1);
        const auto amount = std::min<std::size_t>(
            static_cast<std::size_t>(length), remaining);
        call.vm.Model().WriteByteRegion(
            array, offset,
            std::span(stream.bytes).subspan(stream.cursor, amount));
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    handlers.handler_android_stream_read_full = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto array = call.arguments[0].ref;
        const auto capacity = call.vm.Model().ArrayLength(array);
        const auto remaining = stream.bytes.size() - stream.cursor;
        if (remaining == 0) return dx::VmValue::Int(-1);
        const auto amount = std::min<std::size_t>(
            static_cast<std::size_t>(capacity), remaining);
        call.vm.Model().WriteByteRegion(
            array, 0,
            std::span(stream.bytes).subspan(stream.cursor, amount));
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    handlers.handler_android_stream_available = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        return dx::VmValue::Int(static_cast<std::int32_t>(
            stream.bytes.size() - stream.cursor));
    });
    handlers.handler_android_stream_close = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found = context->streams.find(call.receiver.Value());
        if (found != context->streams.end()) found->second.closed = true;
        return dx::VmValue::Void();
    });
    handlers.handler_android_stream_skip = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto requested = call.arguments[0].AsLong();
        const auto remaining = static_cast<std::int64_t>(
            stream.bytes.size() - stream.cursor);
        const auto amount =
            std::max<std::int64_t>(0, std::min(requested, remaining));
        stream.cursor += static_cast<std::size_t>(amount);
        return dx::VmValue::Long(amount);
    });

    // Output wrapper constructors move the wrapped record to the wrapper
    // handle (single-owner, mirroring android.reader.adopt_stream).
    handlers.handler_android_output_adopt = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto target = call.arguments[0].ref;
        const auto found = context->output_streams.find(target.Value());
        if (found == context->output_streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "wrapped output stream is closed or was "
                                  "never opened"};
        }
        context->output_streams[call.receiver.Value()] =
            std::move(found->second);
        context->output_streams.erase(target.Value());
        return dx::VmValue::Void();
    });
    handlers.handler_android_output_write_one = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() ||
            found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "output stream is closed"};
        }
        found->second.bytes.push_back(static_cast<std::byte>(
            call.arguments[0].AsInt() & 0xff));
        return dx::VmValue::Void();
    });

    // ZipInputStream: adopts the wrapped stream's remaining bytes and reads
    // them with the strict loader ZIP parser (real inflate, CRC-checked).
    const auto zip_of = [context](dx::IntrinsicContext& call)
        -> DexVmAndroidContext::ZipStream& {
        const auto found = context->zip_streams.find(call.receiver.Value());
        if (found == context->zip_streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "zip stream is closed or was never opened"};
        }
        return found->second;
    };
    handlers.handler_android_zip_input_init = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto target = call.arguments[0].ref;
        const auto found = context->streams.find(target.Value());
        if (found == context->streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "wrapped stream is closed or was never "
                                  "opened"};
        }
        DexVmAndroidContext::ZipStream zip;
        auto& source = found->second;
        zip.raw.assign(source.bytes.begin() +
                           static_cast<std::ptrdiff_t>(source.cursor),
                       source.bytes.end());
        context->streams.erase(target.Value());
        try {
            zip.archive = loader::ParseApkArchive(zip.raw);
        } catch (const std::exception& error) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;", error.what()};
        }
        context->zip_streams[call.receiver.Value()] = std::move(zip);
        return dx::VmValue::Void();
    });
    handlers.handler_android_zip_input_get_next_entry = dx::IntrinsicHandler([zip_of](dx::IntrinsicContext& call) {
        auto& zip = zip_of(call);
        zip.entry_bytes.clear();
        zip.cursor = 0;
        zip.entry_open = false;
        if (zip.next_entry >= zip.archive.entries.size()) {
            return dx::VmValue::Ref(dx::VmObjectRef{});  // end of archive
        }
        const auto& entry = zip.archive.entries[zip.next_entry++];
        try {
            zip.entry_bytes =
                loader::ReadApkEntry(zip.raw, zip.archive, entry.name);
        } catch (const std::exception& error) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;", error.what()};
        }
        zip.entry_open = true;
        const auto object =
            call.vm.NewIntrinsicInstance("Ljava/util/zip/ZipEntry;");
        const auto slots = call.vm.Model().InstanceSlots(object);
        slots[0] = {call.vm.NewStringUtf8(entry.name).Value(),
                    dx::SlotTag::ref};
        return dx::VmValue::Ref(object);
    });
    handlers.handler_android_zip_input_read_range = dx::IntrinsicHandler([zip_of](dx::IntrinsicContext& call) {
        auto& zip = zip_of(call);
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        if (offset < 0 || length < 0 ||
            static_cast<std::int64_t>(offset) + length >
                call.vm.Model().ArrayLength(array)) {
            throw dx::VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                                  "zip read range exceeds the array"};
        }
        if (!zip.entry_open) return dx::VmValue::Int(-1);
        const auto remaining = zip.entry_bytes.size() - zip.cursor;
        if (remaining == 0) return dx::VmValue::Int(-1);
        const auto amount = std::min<std::size_t>(
            static_cast<std::size_t>(length), remaining);
        call.vm.Model().WriteByteRegion(
            array, offset,
            std::span(zip.entry_bytes).subspan(zip.cursor, amount));
        zip.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    handlers.handler_android_zip_input_close_entry = dx::IntrinsicHandler([zip_of](dx::IntrinsicContext& call) {
        auto& zip = zip_of(call);
        zip.entry_bytes.clear();
        zip.cursor = 0;
        zip.entry_open = false;
        return dx::VmValue::Void();
    });
    handlers.handler_android_zip_input_close = dx::IntrinsicHandler([context](dx::IntrinsicContext& call) {
        const auto found = context->zip_streams.find(call.receiver.Value());
        if (found != context->zip_streams.end()) {
            found->second.closed = true;
        }
        return dx::VmValue::Void();
    });
    handlers.handler_android_zip_entry_get_name = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Ref(dx::VmObjectRef(slots[0].bits));
    });
    handlers.handler_android_zip_entry_is_directory = dx::IntrinsicHandler([](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        const auto name =
            call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
        return dx::VmValue::Int(!name.empty() && name.back() == '/' ? 1
                                                                    : 0);
    });
}

}  // namespace ogplay::runtime::android_intrinsics
