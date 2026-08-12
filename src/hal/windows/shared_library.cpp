#include "ogplay/hal/shared_library.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ogplay::hal {

std::string SharedLibraryFileName(const std::string& base_name,
                                  const unsigned major_version) {
    return base_name + "-" + std::to_string(major_version) + ".dll";
}

void* OpenSharedLibrary(const std::filesystem::path& path) {
    return reinterpret_cast<void*>(::LoadLibraryW(path.wstring().c_str()));
}

void* ResolveSharedLibrarySymbol(void* handle, const char* symbol_name) {
    return reinterpret_cast<void*>(
        ::GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name));
}

}  // namespace ogplay::hal
