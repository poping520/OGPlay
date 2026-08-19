#include <algorithm>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_InputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/InputStream;", "Ljava/lang/Object;");
    builder.VirtualMethod("read", "([BII)I",
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
    builder.VirtualMethod("read", "([B)I",
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
    builder.VirtualMethod("read", "()I",
        [context](dx::IntrinsicContext& call) {
            auto& stream = StreamOf(call, context);
            if (stream.cursor >= stream.bytes.size()) {
                return dx::VmValue::Int(-1);
            }
            return dx::VmValue::Int(static_cast<std::int32_t>(
                static_cast<std::uint8_t>(stream.bytes[stream.cursor++])));
        });
    builder.VirtualMethod("available", "()I",
        [context](dx::IntrinsicContext& call) {
            auto& stream = StreamOf(call, context);
            return dx::VmValue::Int(static_cast<std::int32_t>(
                stream.bytes.size() - stream.cursor));
        });
    builder.VirtualMethod("close", "()V", StreamCloseHandler(context));
    builder.VirtualMethod("skip", "(J)J",
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
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
