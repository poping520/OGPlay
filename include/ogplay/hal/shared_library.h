#pragma once

#include <filesystem>
#include <string>

namespace ogplay::hal {

// Platform naming convention for a versioned shared library, e.g.
// "avcodec-61.dll" (Windows), "libavcodec.so.61" (Linux),
// "libavcodec.61.dylib" (macOS).
[[nodiscard]] std::string SharedLibraryFileName(const std::string& base_name,
                                                unsigned major_version);

// Loads a shared library for the process lifetime; returns nullptr when the
// library cannot be loaded. A bare file name (no directory) uses the system
// default search order. Handles are never unloaded: resolved function
// pointers stay valid until process exit.
[[nodiscard]] void* OpenSharedLibrary(const std::filesystem::path& path);

// Resolves an exported symbol; returns nullptr when missing.
[[nodiscard]] void* ResolveSharedLibrarySymbol(void* handle,
                                               const char* symbol_name);

}  // namespace ogplay::hal
