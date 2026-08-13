#include <algorithm>

#include "ogplay/loader/apk.h"

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_util_zip_ZipInputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/util/zip/ZipInputStream;");
    builder.Super("Ljava/io/FilterInputStream;");
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
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V",
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
    builder.Virtual("getNextEntry", "()Ljava/util/zip/ZipEntry;",
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
    builder.Virtual("read", "([BII)I",
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
    builder.Virtual("closeEntry", "()V",
        [zip_of](dx::IntrinsicContext& call) {
            auto& zip = zip_of(call);
            zip.entry_bytes.clear();
            zip.cursor = 0;
            zip.entry_open = false;
            return dx::VmValue::Void();
        });
    builder.Virtual("close", "()V",
        [context](dx::IntrinsicContext& call) {
            const auto found =
                context->zip_streams.find(call.receiver.Value());
            if (found != context->zip_streams.end()) {
                found->second.closed = true;
            }
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
