#include "catalog.h"

#include <array>
#include <cctype>

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_net_URLEncoder(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/net/URLEncoder;", "Ljava/lang/Object;");
    builder.StaticMethod("encode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [](dx::IntrinsicContext& call) {
            auto charset = call.vm.StringUtf8(call.arguments[1].ref);
            for (auto& byte : charset) {
                byte = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(byte)));
            }
            if (charset != "UTF-8" && charset != "UTF8") {
                throw dx::VmJavaThrow{
                    "Ljava/io/UnsupportedEncodingException;", charset};
            }
            constexpr std::array<char, 16> kHex = {
                '0', '1', '2', '3', '4', '5', '6', '7',
                '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
            std::string encoded;
            for (const auto byte : call.vm.StringUtf8(call.arguments[0].ref)) {
                const auto value = static_cast<unsigned char>(byte);
                const bool safe = (value >= 'a' && value <= 'z') ||
                    (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9') || value == '-' ||
                    value == '_' || value == '.' || value == '*';
                if (safe) encoded.push_back(static_cast<char>(value));
                else if (value == ' ') encoded.push_back('+');
                else {
                    encoded.push_back('%');
                    encoded.push_back(kHex[value >> 4U]);
                    encoded.push_back(kHex[value & 0x0FU]);
                }
            }
            return MakeString(call, encoded);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
