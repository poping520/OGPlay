#include "ogplay/hal/shared_library.h"

#include <dlfcn.h>

namespace ogplay::hal {

std::string SharedLibraryFileName(const std::string& base_name,
                                  const unsigned major_version) {
    return "lib" + base_name + ".so." + std::to_string(major_version);
}

void* OpenSharedLibrary(const std::filesystem::path& path) {
    return ::dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* ResolveSharedLibrarySymbol(void* handle, const char* symbol_name) {
    return ::dlsym(handle, symbol_name);
}

}  // namespace ogplay::hal
