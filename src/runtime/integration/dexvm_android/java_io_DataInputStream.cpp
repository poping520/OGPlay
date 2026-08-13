#include <algorithm>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_DataInputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/DataInputStream;");
    builder.Super("Ljava/io/InputStream;");
    builder.Virtual("<init>", "(Ljava/io/InputStream;)V", ReaderAdoptStreamHandler(context));
    builder.Virtual("readFully", "([B)V",
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
    builder.Virtual("skipBytes", "(I)I",
        [context](dx::IntrinsicContext& call) {
            auto& stream = StreamOf(call, context);
            const auto wanted = call.arguments[0].AsInt();
            const auto amount = std::min<std::size_t>(
                wanted > 0 ? static_cast<std::size_t>(wanted) : 0,
                stream.bytes.size() - stream.cursor);
            stream.cursor += amount;
            return dx::VmValue::Int(static_cast<std::int32_t>(amount));
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
    builder.Virtual("readInt", "()I",
        [take_bytes](dx::IntrinsicContext& call) {
            const auto bytes = take_bytes(call, 4);
            std::uint32_t value = 0;
            for (const auto byte : bytes) {
                value = (value << 8U) | static_cast<std::uint8_t>(byte);
            }
            return dx::VmValue::Int(static_cast<std::int32_t>(value));
        });
    builder.Virtual("readLong", "()J",
        [take_bytes](dx::IntrinsicContext& call) {
            const auto bytes = take_bytes(call, 8);
            std::uint64_t value = 0;
            for (const auto byte : bytes) {
                value = (value << 8U) | static_cast<std::uint8_t>(byte);
            }
            return dx::VmValue::Long(static_cast<std::int64_t>(value));
        });
    builder.Virtual("readUTF", "()Ljava/lang/String;",
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
    builder.Virtual("close", "()V", StreamCloseHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
