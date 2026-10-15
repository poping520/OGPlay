#pragma once
#include "ogplay/app/export.h"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
namespace ogplay::app {
struct ConsoleResponse final { bool ok{}; std::string json; };
class OGPLAYCORE_API AgentConsole final {
public:
    explicit AgentConsole(const std::filesystem::path& ledger_path);
    ~AgentConsole();
    AgentConsole(const AgentConsole&) = delete;
    AgentConsole& operator=(const AgentConsole&) = delete;
    ConsoleResponse Request(std::string_view method);
    std::string HandleJsonRpc(std::string_view line);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
