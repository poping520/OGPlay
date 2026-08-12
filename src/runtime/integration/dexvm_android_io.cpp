// InputStream/Reader/Writer and java.io.File handlers. Streams read APK
// entries, files go through the session VFS; absent paths answer the
// documented Java values instead of faking success.

#include <algorithm>
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/vfs/vfs.h"

#include "dexvm_android_internal.h"

namespace ogplay::runtime::android_intrinsics {

void RegisterStreams(dx::IntrinsicRegistry& registry,
                     const Context& context) {
    registry.Register("android.stream.read_one",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        if (stream.cursor >= stream.bytes.size()) {
            return dx::VmValue::Int(-1);
        }
        return dx::VmValue::Int(static_cast<std::int32_t>(
            static_cast<std::uint8_t>(stream.bytes[stream.cursor++])));
    });
    // Wrapper constructors adopt the wrapped stream's record: the wrapper
    // handle takes ownership and the wrapped object becomes closed.
    registry.Register("android.reader.adopt_stream",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.reader.read_line",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.reader.ready",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        return dx::VmValue::Int(
            stream.cursor < stream.bytes.size() ? 1 : 0);
    });
    registry.Register("android.charset.for_name",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Ljava/nio/charset/Charset;"));
    });
    registry.Register("android.byte_stream.init_input",
                      [context](dx::IntrinsicContext& call) {
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        context->streams[call.receiver.Value()] =
            DexVmAndroidContext::Stream{
                model.ReadByteRegion(array, 0, model.ArrayLength(array)),
                0, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.data_input.read_fully",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.data_input.read_int",
                      [take_bytes](dx::IntrinsicContext& call) {
        const auto bytes = take_bytes(call, 4);
        std::uint32_t value = 0;
        for (const auto byte : bytes) {
            value = (value << 8U) | static_cast<std::uint8_t>(byte);
        }
        return dx::VmValue::Int(static_cast<std::int32_t>(value));
    });
    registry.Register("android.data_input.read_long",
                      [take_bytes](dx::IntrinsicContext& call) {
        const auto bytes = take_bytes(call, 8);
        std::uint64_t value = 0;
        for (const auto byte : bytes) {
            value = (value << 8U) | static_cast<std::uint8_t>(byte);
        }
        return dx::VmValue::Long(static_cast<std::int64_t>(value));
    });
    registry.Register("android.data_input.read_utf",
                      [take_bytes](dx::IntrinsicContext& call) {
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
    registry.Register("android.data_input.skip_bytes",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto wanted = call.arguments[0].AsInt();
        const auto amount = std::min<std::size_t>(
            wanted > 0 ? static_cast<std::size_t>(wanted) : 0,
            stream.bytes.size() - stream.cursor);
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    registry.Register("android.byte_output.init",
                      [context](dx::IntrinsicContext& call) {
        // No path: bytes stay in memory and never publish to a file.
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{{}, {}, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.byte_output.write_range",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.byte_output.to_byte_array",
                      [byte_output_of](dx::IntrinsicContext& call) {
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
    registry.Register("android.byte_output.size",
                      [byte_output_of](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            byte_output_of(call).bytes.size()));
    });
    registry.Register("android.byte_output.to_string",
                      [byte_output_of](dx::IntrinsicContext& call) {
        auto& output = byte_output_of(call);
        return dx::VmValue::Ref(call.vm.NewStringUtf8(std::string(
            reinterpret_cast<const char*>(output.bytes.data()),
            output.bytes.size())));
    });
    registry.Register("android.file_writer.append_char",
                      [byte_output_of](dx::IntrinsicContext& call) {
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
    registry.Register("android.file_writer.append_sequence",
                      [byte_output_of](dx::IntrinsicContext& call) {
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
    registry.Register("android.stream.read_range",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.stream.read_full",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.stream.available",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        return dx::VmValue::Int(static_cast<std::int32_t>(
            stream.bytes.size() - stream.cursor));
    });
    registry.Register("android.stream.close",
                      [context](dx::IntrinsicContext& call) {
        const auto found = context->streams.find(call.receiver.Value());
        if (found != context->streams.end()) found->second.closed = true;
        return dx::VmValue::Void();
    });
    registry.Register("android.stream.skip",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.output.adopt",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.output.write_one",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.zip_input.init",
                      [context](dx::IntrinsicContext& call) {
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
    registry.Register("android.zip_input.get_next_entry",
                      [zip_of](dx::IntrinsicContext& call) {
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
    registry.Register("android.zip_input.read_range",
                      [zip_of](dx::IntrinsicContext& call) {
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
    registry.Register("android.zip_input.close_entry",
                      [zip_of](dx::IntrinsicContext& call) {
        auto& zip = zip_of(call);
        zip.entry_bytes.clear();
        zip.cursor = 0;
        zip.entry_open = false;
        return dx::VmValue::Void();
    });
    registry.Register("android.zip_input.close",
                      [context](dx::IntrinsicContext& call) {
        const auto found = context->zip_streams.find(call.receiver.Value());
        if (found != context->zip_streams.end()) {
            found->second.closed = true;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.zip_entry.get_name",
                      [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Ref(dx::VmObjectRef(slots[0].bits));
    });
    registry.Register("android.zip_entry.is_directory",
                      [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        const auto name =
            call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
        return dx::VmValue::Int(!name.empty() && name.back() == '/' ? 1
                                                                    : 0);
    });
}

// Reads a whole file from the shared guest VFS; nullopt when the VFS is
// absent or the path does not resolve.
[[nodiscard]] std::optional<std::vector<std::byte>> VfsReadAll(
    const Context& context, const std::string& path) {
    if (context->vfs == nullptr) return std::nullopt;
    try {
        const auto info = context->vfs->Stat(path);
        const auto descriptor =
            context->vfs->Open(path, VfsOpenOptions{.read = true});
        std::vector<std::byte> bytes(info.size);
        std::size_t cursor = 0;
        while (cursor < bytes.size()) {
            const auto got = context->vfs->Read(
                descriptor, std::span(bytes).subspan(cursor));
            if (got == 0) break;
            cursor += got;
        }
        context->vfs->Close(descriptor);
        bytes.resize(cursor);
        return bytes;
    } catch (const VfsError&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::uint64_t> VfsSizeOf(
    const Context& context, const std::string& path) {
    if (context->vfs == nullptr) return std::nullopt;
    try {
        return context->vfs->Stat(path).size;
    } catch (const VfsError&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::string FilePathOf(dx::IntrinsicContext& call,
                                     const dx::VmObjectRef file) {
    const auto slots = call.vm.Model().InstanceSlots(file);
    return call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
}

void RegisterFiles(dx::IntrinsicRegistry& registry, const Context& context) {
    registry.Register("android.file.init",
                      [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
    registry.Register("android.file.init_parent_child",
                      [](dx::IntrinsicContext& call) {
        auto joined = call.vm.StringUtf8(call.arguments[0].ref);
        if (!joined.empty() && joined.back() != '/') joined += '/';
        joined += call.vm.StringUtf8(call.arguments[1].ref);
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.vm.NewStringUtf8(joined).Value(),
                    dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
    // Directory facts merge the VFS view with the session memory overlay
    // (directories exist implicitly through the files beneath them).
    const auto list_children = [context](const std::string& path) {
        std::vector<std::string> names;
        if (context->vfs != nullptr) {
            for (auto& entry : context->vfs->ListDirectory(path)) {
                names.push_back(std::move(entry.name));
            }
        }
        auto prefix = path;
        if (prefix.empty() || prefix.back() != '/') prefix.push_back('/');
        for (const auto& [overlay_path, bytes] : context->memory_files) {
            static_cast<void>(bytes);
            if (!overlay_path.starts_with(prefix)) continue;
            const auto remainder =
                std::string_view(overlay_path).substr(prefix.size());
            auto name = std::string(remainder.substr(0, remainder.find('/')));
            if (name.empty()) continue;
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(std::move(name));
            }
        }
        return names;
    };
    registry.Register("android.file.exists",
                      [context, list_children](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        const bool exists = context->memory_files.contains(path) ||
                            VfsSizeOf(context, path).has_value() ||
                            !list_children(path).empty();
        return dx::VmValue::Int(exists ? 1 : 0);
    });
    registry.Register("android.file.is_directory",
                      [list_children](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        return dx::VmValue::Int(!list_children(path).empty() ? 1 : 0);
    });
    registry.Register("android.file.list",
                      [list_children](dx::IntrinsicContext& call) {
        auto names = list_children(FilePathOf(call, call.receiver));
        if (names.empty()) {
            // Documented value for a path that is not a listable directory.
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        auto& vm = call.vm;
        const auto array_class =
            vm.Linker().ResolveDescriptor("[Ljava/lang/String;");
        const auto element_class =
            vm.Linker().ResolveDescriptor("Ljava/lang/String;");
        const auto array = vm.Model().NewObjectArray(
            array_class, element_class,
            static_cast<JniSize>(names.size()));
        for (std::size_t index = 0; index < names.size(); ++index) {
            vm.Model().SetObjectElement(
                array, static_cast<JniSize>(index),
                vm.NewStringUtf8(names[index]));
        }
        return dx::VmValue::Ref(array);
    });
    registry.Register("android.file.length",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        const auto overlay = context->memory_files.find(path);
        if (overlay != context->memory_files.end()) {
            return dx::VmValue::Long(
                static_cast<std::int64_t>(overlay->second.size()));
        }
        const auto size = VfsSizeOf(context, path);
        // 0 is the documented value for nonexistent paths.
        return dx::VmValue::Long(
            size.has_value() ? static_cast<std::int64_t>(*size) : 0);
    });
    registry.Register("android.file.get_path",
                      [](dx::IntrinsicContext& call) {
        return MakeString(call, FilePathOf(call, call.receiver));
    });
    registry.Register("android.file.mkdirs", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.file.create_new",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        if (context->memory_files.contains(path) ||
            VfsSizeOf(context, path).has_value()) {
            return dx::VmValue::Int(0);
        }
        context->memory_files[path] = {};
        return dx::VmValue::Int(1);
    });
    registry.Register("android.environment.get_external_storage_dir",
                      [context](dx::IntrinsicContext& call) {
        const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
        const auto slots = call.vm.Model().InstanceSlots(file);
        slots[0] = {
            call.vm.NewStringUtf8(context->external_storage_root).Value(),
            dx::SlotTag::ref};
        return dx::VmValue::Ref(file);
    });
    registry.Register("android.context.get_external_files_dir",
                      [context](dx::IntrinsicContext& call) {
        // Platform layout under the external mount; a null type argument
        // answers the package files root.
        auto path = context->external_storage_root + "/Android/data/" +
                    context->package_name + "/files";
        const auto type = call.arguments[0].ref;
        if (type.IsValid()) {
            path += "/" + call.vm.StringUtf8(type);
        }
        const auto file = call.vm.NewIntrinsicInstance("Ljava/io/File;");
        const auto slots = call.vm.Model().InstanceSlots(file);
        slots[0] = {call.vm.NewStringUtf8(path).Value(), dx::SlotTag::ref};
        return dx::VmValue::Ref(file);
    });
    registry.Register("android.environment.get_external_storage_state",
                      [context](dx::IntrinsicContext& call) {
        // The external mount is required by the profile and read at
        // startup, so MEDIA_MOUNTED is the truthful state.
        return MakeString(call, "mounted");
    });
    registry.Register("android.statfs.init", [](dx::IntrinsicContext&) {
        // Only the external volume is queryable on this platform; the
        // constructor path argument selects nothing further.
        return dx::VmValue::Void();
    });
    registry.Register("android.statfs.get_block_size",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(4096);
    });
    registry.Register("android.statfs.get_available_blocks",
                      [context](dx::IntrinsicContext&) {
        const auto blocks = context->external_free_bytes / 4096U;
        return dx::VmValue::Int(static_cast<std::int32_t>(
            std::min<std::uint64_t>(blocks, INT32_MAX)));
    });
    registry.Register("android.file_writer.init_file_append",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.arguments[0].ref);
        DexVmAndroidContext::OutputStream output{path, {}, false};
        if (call.arguments[1].AsInt() != 0) {
            const auto overlay = context->memory_files.find(path);
            if (overlay != context->memory_files.end()) {
                output.bytes = overlay->second;
            } else if (const auto existing = VfsReadAll(context, path)) {
                output.bytes = *existing;
            }
        }
        context->output_streams[call.receiver.Value()] = std::move(output);
        return dx::VmValue::Void();
    });
    registry.Register("android.file.delete",
                      [context](dx::IntrinsicContext& call) {
        const auto path = FilePathOf(call, call.receiver);
        if (context->memory_files.erase(path) > 0) {
            return dx::VmValue::Int(1);
        }
        // Mounted (read-only) entries cannot be deleted: report failure.
        return dx::VmValue::Int(0);
    });
    const auto open_input = [context](dx::IntrinsicContext& call,
                                      const std::string& path) {
        const auto found = context->memory_files.find(path);
        if (found != context->memory_files.end()) {
            context->streams[call.receiver.Value()] =
                DexVmAndroidContext::Stream{found->second, 0, false};
            return dx::VmValue::Void();
        }
        auto bytes = VfsReadAll(context, path);
        if (!bytes.has_value()) {
            throw dx::VmJavaThrow{"Ljava/io/FileNotFoundException;",
                                  "file not found: " + path};
        }
        context->streams[call.receiver.Value()] =
            DexVmAndroidContext::Stream{std::move(*bytes), 0, false};
        return dx::VmValue::Void();
    };
    registry.Register("android.file_stream.init_file",
                      [context, open_input](dx::IntrinsicContext& call) {
        const auto file = call.arguments[0].ref;
        const auto slots = call.vm.Model().InstanceSlots(file);
        return open_input(
            call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
    registry.Register("android.file_stream.init_path",
                      [context, open_input](dx::IntrinsicContext& call) {
        return open_input(call,
                          call.vm.StringUtf8(call.arguments[0].ref));
    });
    const auto open_output = [context](dx::IntrinsicContext& call,
                                       const std::string& path) {
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{path, {}, false};
        return dx::VmValue::Void();
    };
    registry.Register("android.file_output.init_path",
                      [open_output](dx::IntrinsicContext& call) {
        return open_output(call,
                           call.vm.StringUtf8(call.arguments[0].ref));
    });
    registry.Register("android.file_output.init_file",
                      [open_output](dx::IntrinsicContext& call) {
        const auto slots =
            call.vm.Model().InstanceSlots(call.arguments[0].ref);
        return open_output(
            call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
    const auto flush_output = [context](dx::IntrinsicContext& call,
                                        const std::uint32_t handle) {
        const auto found = context->output_streams.find(handle);
        if (found == context->output_streams.end()) return;
        context->memory_files[found->second.path] = found->second.bytes;
        found->second.closed = true;
        static_cast<void>(call);
    };
    registry.Register("android.file_output.write_bytes",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() ||
            found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "output stream is closed"};
        }
        auto& model = call.vm.Model();
        const auto array = call.arguments[0].ref;
        const auto bytes =
            model.ReadByteRegion(array, 0, model.ArrayLength(array));
        found->second.bytes.insert(found->second.bytes.end(), bytes.begin(),
                                   bytes.end());
        return dx::VmValue::Void();
    });
    registry.Register("android.file_output.flush",
                      [context](dx::IntrinsicContext& call) {
        // Bytes become visible to readers at flush (and again at close).
        const auto found =
            context->output_streams.find(call.receiver.Value());
        if (found != context->output_streams.end() &&
            !found->second.closed) {
            context->memory_files[found->second.path] =
                found->second.bytes;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.file_output.close",
                      [context, flush_output](dx::IntrinsicContext& call) {
        flush_output(call, call.receiver.Value());
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.init",
                      [context](dx::IntrinsicContext& call) {
        // Chain: reuse the wrapped stream's output slot.
        const auto target = call.arguments[0].ref;
        const auto found = context->output_streams.find(target.Value());
        if (found == context->output_streams.end()) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "DataOutputStream target is not open"};
        }
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{found->second.path, {}, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.write_utf",
                      [context](dx::IntrinsicContext& call) {
        auto found = context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "DataOutputStream is closed"};
        }
        const auto text = call.vm.StringUtf8(call.arguments[0].ref);
        auto& bytes = found->second.bytes;
        bytes.push_back(static_cast<std::byte>((text.size() >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::byte>(text.size() & 0xffU));
        for (const auto character : text) {
            bytes.push_back(static_cast<std::byte>(character));
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.close",
                      [context, flush_output](dx::IntrinsicContext& call) {
        flush_output(call, call.receiver.Value());
        return dx::VmValue::Void();
    });
}

}  // namespace ogplay::runtime::android_intrinsics
