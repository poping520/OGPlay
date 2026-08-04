#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "ogplay/runtime/headless_bionic_runner.h"

namespace {

[[nodiscard]] std::optional<std::filesystem::path> EnvironmentPath(
    const char* name) {
#if defined(_WIN32)
    char* value{};
    std::size_t size{};
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::nullopt;
    }
    const std::filesystem::path result{value};
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::filesystem::path{value};
#endif
}

[[nodiscard]] std::vector<std::byte> ReadBytes(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("cannot read M2 guest module");
    const auto size = input.tellg();
    if (size <= 0) throw std::runtime_error("M2 guest module is empty");
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()), size);
    if (!input) throw std::runtime_error("M2 guest module read was truncated");
    return result;
}

}  // namespace

TEST_CASE("API 19 22 and 23 Bionic run the headless M2 NDK payload") {
    const auto oracle = EnvironmentPath("OGPLAY_BIONIC_ORACLE_ROOT");
    const auto sample = EnvironmentPath("OGPLAY_M2_NDK_SAMPLE");
    if (!oracle.has_value() || !sample.has_value()) return;

    const auto payload = ReadBytes(*sample);
    for (const std::uint32_t api : {19U, 22U, 23U}) {
        INFO("Bionic API: " << api);
        const auto directory = *oracle / ("api" + std::to_string(api)) / "lib";
        const auto libm = ReadBytes(directory / "libm.so");
        const auto libdl = ReadBytes(directory / "libdl.so");
        const auto libc = ReadBytes(directory / "libc.so");
        const ogplay::loader::Elf32ModuleInput modules[]{
            {"libogplay_m2_ndk.so", payload,
             ogplay::memory::GuestAddress{0x10000000U}},
            {"libm.so", libm, ogplay::memory::GuestAddress{0x20000000U}},
            {"libdl.so", libdl, ogplay::memory::GuestAddress{0x30000000U}},
            {"libc.so", libc, ogplay::memory::GuestAddress{0x40000000U}}};
        ogplay::runtime::HeadlessBionicRunRequest request{
            api, "libogplay_m2_ndk.so", "ogplay_m2_entry", modules,
            "/data/ogplay/m2-output.bin", UINT64_C(200000000), {}};
        const auto report = ogplay::runtime::RunHeadlessBionicEntry(request);
        std::string unimplemented;
        for (const auto& hit : report.unimplemented) {
            if (!unimplemented.empty()) unimplemented += ", ";
            unimplemented += hit.id;
        }
        INFO("unimplemented syscalls: " << unimplemented);
        std::string syscall_trace;
        const auto trace_start = report.root_syscalls.size() > 24
                                     ? report.root_syscalls.size() - 24
                                     : 0;
        for (std::size_t index = trace_start;
             index < report.root_syscalls.size(); ++index) {
            const auto& call = report.root_syscalls[index];
            syscall_trace += " t" + std::to_string(call.thread_id) + ":" +
                             std::to_string(call.number) + "=" +
                             std::to_string(call.result);
            if (call.number == 172) {
                syscall_trace += "(";
                for (std::size_t argument = 0; argument < 5; ++argument) {
                    if (argument != 0) syscall_trace += ",";
                    syscall_trace += std::to_string(call.arguments[argument]);
                }
                syscall_trace += ")";
            }
        }
        INFO("recent root syscalls:" << syscall_trace);
        CHECK(report.entry_result == 0);
        CHECK(report.guest_child_count == 1);
        CHECK(report.output.size() == 32);
        CHECK(report.unimplemented.empty());
    }
}
