#include "zip_fixture.h"

#include <fstream>

namespace ogplay::test {
static void Put16(std::string& out, std::uint32_t value) {
    out += static_cast<char>(value & 0xFFU);
    out += static_cast<char>((value >> 8U) & 0xFFU);
}
static void Put32(std::string& out, std::uint32_t value) {
    Put16(out, value & 0xFFFFU);
    Put16(out, value >> 16U);
}

void WriteZip(const std::filesystem::path& path, const std::vector<ZipMember>& members) {
    std::string body, directory;
    for (const auto& member : members) {
        std::string data = member.content;
        if (member.deflate) {
            data.clear();
            data += '\x01';   // final block, stored
            Put16(data, static_cast<std::uint32_t>(member.content.size()));
            Put16(data, static_cast<std::uint32_t>(~member.content.size() & 0xFFFFU));
            data += member.content;
        }
        const auto offset = static_cast<std::uint32_t>(body.size());
        const std::uint16_t method = member.deflate ? 8 : 0;
        Put32(body, 0x04034b50);
        Put16(body, 20);
        Put16(body, member.flags);
        Put16(body, method);
        Put32(body, 0);
        Put32(body, 0);
        Put32(body, static_cast<std::uint32_t>(data.size()));
        Put32(body, static_cast<std::uint32_t>(member.content.size()));
        Put16(body, static_cast<std::uint32_t>(member.name.size()));
        Put16(body, 0);
        body += member.name + data;
        Put32(directory, 0x02014b50);
        Put16(directory, 20);
        Put16(directory, 20);
        Put16(directory, member.flags);
        Put16(directory, method);
        Put32(directory, 0);
        Put32(directory, 0);
        Put32(directory, static_cast<std::uint32_t>(data.size()));
        Put32(directory, static_cast<std::uint32_t>(member.content.size()));
        Put16(directory, static_cast<std::uint32_t>(member.name.size()));
        Put16(directory, 0);
        Put16(directory, 0);
        Put16(directory, 0);
        Put16(directory, 0);
        Put32(directory, 0);
        Put32(directory, offset);
        directory += member.name;
    }
    std::string end;
    Put32(end, 0x06054b50);
    Put16(end, 0);
    Put16(end, 0);
    Put16(end, static_cast<std::uint32_t>(members.size()));
    Put16(end, static_cast<std::uint32_t>(members.size()));
    Put32(end, static_cast<std::uint32_t>(directory.size()));
    Put32(end, static_cast<std::uint32_t>(body.size()));
    Put16(end, 0);
    std::ofstream(path, std::ios::binary) << body << directory << end;
}

}  // namespace ogplay::test
