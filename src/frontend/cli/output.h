#pragma once
#include <cstdio>
#include <string>
#include <string_view>
namespace ogplay::cli {
inline void Write(FILE* stream, std::string_view text) {
    static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stream));
    static_cast<void>(std::fflush(stream));
}
inline std::string FormatError(std::string_view message) {
    std::string result{"\nerror [ogplay]\n  "};
    for (const auto c : message) { result += c; if (c == '\n') result += "  "; }
    return result + '\n';
}
}
