#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

// Implemented by the executable's frontend adapter, never by the HAL library.
extern "C" int ogplay_host_main(int argc, const char* const argv[]) noexcept;

namespace {
int ForwardApplication(int (*entry)(int, const char* const[]) noexcept) noexcept {
    try {
        int count{};
        auto** wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!wide) throw std::runtime_error("cannot read Windows command line");
        struct Arguments final { wchar_t** value; ~Arguments() { LocalFree(value); } } owned{wide};
        std::vector<std::string> storage;
        storage.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[index], -1, nullptr, 0, nullptr, nullptr);
            if (size <= 0) throw std::runtime_error("invalid Windows command line encoding");
            std::string text(static_cast<std::size_t>(size), '\0');
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[index], -1, text.data(), size, nullptr, nullptr))
                throw std::runtime_error("cannot encode Windows command line");
            text.pop_back(); storage.push_back(std::move(text));
        }
        std::vector<const char*> args;
        for (const auto& argument : storage) args.push_back(argument.c_str());
        args.push_back(nullptr);
        return entry(count, args.data());
    } catch (const std::exception& error) {
        static_cast<void>(std::fputs("error [ogplay.bootstrap]\n  ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
    } catch (...) { static_cast<void>(std::fputs("error [ogplay.bootstrap] unknown failure\n", stderr)); }
    return 1;
}
}  // namespace
