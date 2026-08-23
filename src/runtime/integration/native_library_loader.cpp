#include "ogplay/runtime/integration/native_library_loader.h"

#include "ogplay/runtime/boundary/boundary_catalog.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ogplay/loader/elf.h"

namespace ogplay::runtime {
namespace {

constexpr std::string_view kSyntheticLibraryRoot = "/data/app-lib/";

[[nodiscard]] bool IsLibraryBasename(const std::string_view name) {
    return !name.empty() && name.ends_with(".so") &&
           name.find('/') == std::string_view::npos &&
           name.find('\\') == std::string_view::npos;
}

[[nodiscard]] char FoldGuestAscii(const char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

[[nodiscard]] std::string CanonicalGuestLibraryPath(
    const std::string_view path) {
    if (path.empty() || path.front() != '/') {
        throw NativeLibraryLoadError(
            NativeLibraryLoadErrorReason::invalid_request,
            "System.load requires an absolute guest path");
    }
    std::string result;
    std::size_t cursor = 1;
    while (cursor <= path.size()) {
        const auto end = path.find('/', cursor);
        const auto count =
            (end == std::string_view::npos ? path.size() : end) - cursor;
        const auto component = path.substr(cursor, count);
        if (component == "..") {
            throw NativeLibraryLoadError(
                NativeLibraryLoadErrorReason::invalid_request,
                "System.load guest path traversal is forbidden");
        }
        if (!component.empty() && component != ".") {
            result.push_back('/');
            for (const auto character : component) {
                if (character == '\0' || character == '\\') {
                    throw NativeLibraryLoadError(
                        NativeLibraryLoadErrorReason::invalid_request,
                        "System.load guest path contains an invalid character");
                }
                result.push_back(FoldGuestAscii(character));
            }
        }
        if (end == std::string_view::npos) break;
        cursor = end + 1U;
    }
    if (result.empty()) {
        throw NativeLibraryLoadError(
            NativeLibraryLoadErrorReason::invalid_request,
            "System.load guest path does not name a file");
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> ReadGuestLibraryImage(
    VirtualFileSystem& filesystem, const std::string_view path) {
    VfsFileInfo info;
    try {
        info = filesystem.Stat(path);
    } catch (const VfsError& error) {
        throw NativeLibraryLoadError(
            error.ErrorNumber() == 2
                ? NativeLibraryLoadErrorReason::library_not_found
                : NativeLibraryLoadErrorReason::invalid_request,
            "guest native library is unavailable: " + std::string(path) +
                ": " + error.what());
    }
    if (info.is_directory ||
        info.size > std::numeric_limits<std::size_t>::max()) {
        throw NativeLibraryLoadError(
            NativeLibraryLoadErrorReason::invalid_request,
            "guest native library path is not a readable file: " +
                std::string(path));
    }

    std::int32_t descriptor{};
    try {
        descriptor = filesystem.Open(path, {.read = true});
    } catch (const VfsError& error) {
        throw NativeLibraryLoadError(
            error.ErrorNumber() == 2
                ? NativeLibraryLoadErrorReason::library_not_found
                : NativeLibraryLoadErrorReason::invalid_request,
            "guest native library cannot be opened: " + std::string(path) +
                ": " + error.what());
    }

    std::vector<std::byte> result(static_cast<std::size_t>(info.size));
    std::size_t offset{};
    try {
        while (offset < result.size()) {
            const auto count = filesystem.Read(
                descriptor, std::span{result}.subspan(offset));
            if (count == 0U) break;
            offset += count;
        }
        filesystem.Close(descriptor);
    } catch (const std::exception& error) {
        try {
            filesystem.Close(descriptor);
        } catch (const std::exception&) {
        }
        throw NativeLibraryLoadError(
            NativeLibraryLoadErrorReason::link_failure,
            "guest native library read failed: " + std::string(path) +
                ": " + error.what());
    }
    if (offset != result.size()) {
        throw NativeLibraryLoadError(
            NativeLibraryLoadErrorReason::link_failure,
            "guest native library was truncated while reading: " +
                std::string(path));
    }
    return result;
}

[[nodiscard]] NativeLibraryLoadErrorReason ClassifyProcessFailure(
    const std::string_view message) {
    if (message.find("constructor") != std::string_view::npos ||
        message.find("guest invocation") != std::string_view::npos) {
        return NativeLibraryLoadErrorReason::constructor_failure;
    }
    if (message.find("required ELF module is unavailable") !=
            std::string_view::npos ||
        message.find("strong ELF symbol is unresolved") !=
            std::string_view::npos) {
        return NativeLibraryLoadErrorReason::unresolved_dependency;
    }
    return NativeLibraryLoadErrorReason::link_failure;
}

}  // namespace

NativeLibraryLoadError::NativeLibraryLoadError(
    const NativeLibraryLoadErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

NativeLibraryLoadErrorReason NativeLibraryLoadError::Reason() const noexcept {
    return reason_;
}

class NativeLibraryLoader::Impl final {
public:
    Impl(AndroidGuestProcess& process,
         const loader::ApkSelectedNativeLibraries& libraries)
        : process_(&process), libraries_(&libraries) {}

    Impl(AndroidGuestProcess& process,
         const loader::ApkSelectedNativeLibraries& libraries,
         const BionicProfile& bionic,
         const std::span<const BionicModuleSource> system_libraries,
         core::Logger* logger)
        : process_(&process), libraries_(&libraries),
          boundary_api_(bionic.api),
          logger_(logger) {
        system_libraries_.reserve(system_libraries.size());
        for (const auto& source : system_libraries) {
            if (!IsLibraryBasename(source.name) || source.image.empty() ||
                std::any_of(system_libraries_.begin(), system_libraries_.end(),
                            [&](const auto& existing) {
                                return existing.name == source.name;
                            })) {
                throw NativeLibraryLoadError(
                    NativeLibraryLoadErrorReason::invalid_request,
                    "dynamic Bionic system library source is invalid or duplicate: " +
                        source.name);
            }
            system_libraries_.push_back(
                {source.name,
                 std::vector<std::byte>(source.image.begin(),
                                        source.image.end())});
        }
    }

    NativeLibraryLoadResult LoadLibrary(
        const std::string_view logical_name,
        const JavaClassLoaderToken class_loader) {
        if (logical_name.empty() ||
            logical_name.find('/') != std::string_view::npos ||
            logical_name.find('\\') != std::string_view::npos) {
            throw NativeLibraryLoadError(
                NativeLibraryLoadErrorReason::invalid_request,
                "native logical library name is invalid");
        }
        const auto* library = libraries_->FindLogicalName(logical_name);
        if (library == nullptr) {
            throw NativeLibraryLoadError(
                NativeLibraryLoadErrorReason::library_not_found,
                "APK native logical library is unavailable: " +
                    std::string(logical_name));
        }
        return LoadResolved(
            {SyntheticGuestPath(library->soname), library->soname,
             library->image, library->entry_name, true},
            class_loader);
    }

    NativeLibraryLoadResult LoadPath(
        const std::string_view guest_path,
        const JavaClassLoaderToken class_loader) {
        if (guest_path.starts_with(kSyntheticLibraryRoot)) {
            const auto soname = guest_path.substr(kSyntheticLibraryRoot.size());
            const auto* library =
                IsLibraryBasename(soname) ? libraries_->FindSoname(soname)
                                          : nullptr;
            if (library == nullptr) {
                throw NativeLibraryLoadError(
                    NativeLibraryLoadErrorReason::library_not_found,
                    "APK native guest path is unavailable: " +
                        std::string(guest_path));
            }
            return LoadResolved(
                {SyntheticGuestPath(library->soname), library->soname,
                 library->image, library->entry_name, true},
                class_loader);
        }

        if (const auto* library = libraries_->FindEntryName(guest_path);
            library != nullptr) {
            return LoadResolved(
                {SyntheticGuestPath(library->soname), library->soname,
                 library->image, library->entry_name, true},
                class_loader);
        }

        const auto canonical_path = CanonicalGuestLibraryPath(guest_path);
        auto* filesystem = process_->Filesystem();
        if (filesystem == nullptr) {
            throw NativeLibraryLoadError(
                NativeLibraryLoadErrorReason::library_not_found,
                "guest native library VFS is unavailable: " + canonical_path);
        }
        auto image = ReadGuestLibraryImage(*filesystem, canonical_path);
        const auto separator = canonical_path.find_last_of('/');
        const auto module_name = canonical_path.substr(separator + 1U);
        return LoadResolved(
            {canonical_path, module_name, image, canonical_path, false},
            class_loader);
    }

    std::vector<NativeLibraryRecord> Records() const {
        std::scoped_lock lock(mutex_);
        std::vector<NativeLibraryRecord> result;
        result.reserve(records_.size());
        for (const auto& [path, entry] : records_) {
            static_cast<void>(path);
            result.push_back(entry.record);
        }
        return result;
    }

private:
    struct RegistryEntry final {
        NativeLibraryRecord record;
        std::thread::id loading_thread;
        NativeLibraryLoadErrorReason failure_reason{
            NativeLibraryLoadErrorReason::prior_failure};
    };

    struct OwnedSystemLibrary final {
        std::string name;
        std::vector<std::byte> image;
    };

    struct ExplicitLibrary final {
        std::string canonical_path;
        std::string module_name;
        std::span<const std::byte> image;
        std::string diagnostic_name;
        bool require_soname_match{};
    };

    struct ClosureModule final {
        std::string name;
        std::span<const std::byte> image;
        std::string diagnostic_name;
        bool require_soname_match{};
    };

    [[nodiscard]] bool IsBoundary(const std::string_view name) const {
        return boundary_api_.has_value() &&
               IsAndroidBoundaryLibrary(*boundary_api_, name);
    }

    [[nodiscard]] const OwnedSystemLibrary* FindSystemLibrary(
        const std::string_view name) const {
        const auto found = std::find_if(
            system_libraries_.begin(), system_libraries_.end(),
            [&](const auto& source) { return source.name == name; });
        return found == system_libraries_.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::vector<ClosureModule> Closure(
        const ExplicitLibrary& root) const {
        std::vector<ClosureModule> result{{
            root.module_name, root.image, root.diagnostic_name,
            root.require_soname_match}};
        std::set<std::string, std::less<>> selected{root.module_name};
        for (std::size_t index = 0; index < result.size(); ++index) {
            const auto& library = result[index];
            try {
                const auto image = loader::ParseElf32Arm(library.image);
                if (image.type != loader::Elf32ImageType::shared_object) {
                    throw NativeLibraryLoadError(
                        NativeLibraryLoadErrorReason::malformed_elf,
                        "native library is not ET_DYN: " +
                            library.diagnostic_name);
                }
                const auto dynamic = loader::ReadElf32DynamicInfo(
                    library.image, image);
                if (library.require_soname_match &&
                    dynamic.soname.has_value() &&
                    *dynamic.soname != library.name) {
                    throw NativeLibraryLoadError(
                        NativeLibraryLoadErrorReason::soname_mismatch,
                        "native DT_SONAME disagrees with inventory identity: " +
                            library.diagnostic_name);
                }
                for (const auto& needed : dynamic.needed) {
                    if (selected.contains(needed) ||
                        process_->HasLoadedModule(needed) ||
                        IsBoundary(needed)) {
                        continue;
                    }
                    const auto* dependency = libraries_->FindSoname(needed);
                    if (dependency != nullptr) {
                        selected.insert(needed);
                        result.push_back({dependency->soname,
                                          dependency->image,
                                          dependency->entry_name, true});
                        continue;
                    }
                    const auto* system = FindSystemLibrary(needed);
                    if (system != nullptr) {
                        selected.insert(needed);
                        result.push_back({system->name, system->image,
                                          system->name, true});
                    }
                }
            } catch (const NativeLibraryLoadError&) {
                throw;
            } catch (const std::exception& error) {
                throw NativeLibraryLoadError(
                    NativeLibraryLoadErrorReason::malformed_elf,
                    "native ELF is malformed: " + library.diagnostic_name +
                        ": " + error.what());
            }
        }
        return result;
    }

    [[nodiscard]] NativeLibraryLoadResult LoadResolved(
        const ExplicitLibrary& library,
        const JavaClassLoaderToken class_loader) {
        const auto canonical_path = library.canonical_path;
        NativeLibraryHandle handle{};
        {
            std::unique_lock lock(mutex_);
            const auto found = records_.find(canonical_path);
            if (found != records_.end()) {
                auto& entry = found->second;
                if (entry.record.class_loader != class_loader) {
                    throw NativeLibraryLoadError(
                        NativeLibraryLoadErrorReason::cross_loader_conflict,
                        "native library is already associated with another "
                        "ClassLoader: " + canonical_path);
                }
                if (entry.record.state == NativeLibraryLoadState::loading &&
                    entry.loading_thread == std::this_thread::get_id()) {
                    return ExistingResult(entry.record, true);
                }
                while (entry.record.state == NativeLibraryLoadState::loading) {
                    condition_.wait(lock);
                }
                if (entry.record.state == NativeLibraryLoadState::loaded) {
                    return ExistingResult(entry.record, false);
                }
                throw NativeLibraryLoadError(
                    entry.failure_reason,
                    entry.record.failure);
            }
            if (next_handle_ == 0) {
                throw NativeLibraryLoadError(
                    NativeLibraryLoadErrorReason::invalid_request,
                    "native library handle space is exhausted");
            }
            handle = next_handle_++;
            RegistryEntry entry;
            entry.record.handle = handle;
            entry.record.canonical_path = canonical_path;
            entry.record.soname = library.module_name;
            entry.record.class_loader = class_loader;
            entry.loading_thread = std::this_thread::get_id();
            records_.emplace(canonical_path, std::move(entry));
        }

        try {
            const auto closure = Closure(library);
            std::vector<AndroidGuestApplicationModuleSource> sources;
            sources.reserve(closure.size());
            for (const auto& module : closure) {
                if (process_->HasLoadedModule(module.name)) continue;
                sources.push_back({module.name, module.image});
            }
            auto application = process_->LoadApplicationModules(
                library.module_name, sources);
            std::optional<std::uint32_t> jni_version;
            try {
                jni_version = process_->InitializeExplicitJniLibrary(
                    library.module_name);
            } catch (const std::exception& error) {
                const auto message = std::string_view(error.what());
                const auto reason =
                    message.find("unsupported JNI version") !=
                            std::string_view::npos
                        ? NativeLibraryLoadErrorReason::invalid_jni_version
                        : NativeLibraryLoadErrorReason::jni_on_load_failure;
                throw NativeLibraryLoadError(
                    reason, error.what());
            }

            std::scoped_lock lock(mutex_);
            auto& entry = records_.at(canonical_path);
            entry.record.state = NativeLibraryLoadState::loaded;
            entry.record.module_index = application.root_module_index;
            entry.record.jni_version = jni_version;
            entry.record.jni_on_load_calls = jni_version.has_value() ? 1U : 0U;
            entry.loading_thread = {};
            condition_.notify_all();
            if (logger_ != nullptr) {
                logger_->Write(
                    core::LogLevel::info, "runtime.native_library",
                    "explicit native load completed", {},
                    {{"sequence", handle},
                     {"soname", library.module_name},
                     {"initialized_modules",
                      static_cast<std::uint64_t>(
                          application.initialized_modules.size())},
                     {"jni_on_load_calls",
                      static_cast<std::uint64_t>(
                          jni_version.has_value() ? 1U : 0U)}},
                    {.mode = core::RateLimitMode::none});
            }
            return {handle, canonical_path, application.root_module_index,
                    std::move(application.initialized_modules), jni_version,
                    false, false};
        } catch (const NativeLibraryLoadError& error) {
            Fail(canonical_path, error.Reason(), error.what());
            throw;
        } catch (const AndroidGuestProcessError& error) {
            const auto reason = ClassifyProcessFailure(error.what());
            Fail(canonical_path, reason, error.what());
            throw NativeLibraryLoadError(reason, error.what());
        } catch (const std::exception& error) {
            Fail(canonical_path,
                 NativeLibraryLoadErrorReason::link_failure, error.what());
            throw NativeLibraryLoadError(
                NativeLibraryLoadErrorReason::link_failure, error.what());
        }
    }

    [[nodiscard]] static NativeLibraryLoadResult ExistingResult(
        const NativeLibraryRecord& record, const bool recursive) {
        return {record.handle, record.canonical_path, record.module_index, {},
                record.jni_version, true, recursive};
    }

    void Fail(const std::string& canonical_path,
              const NativeLibraryLoadErrorReason reason,
              const std::string_view message) {
        std::scoped_lock lock(mutex_);
        auto& entry = records_.at(canonical_path);
        entry.record.state = NativeLibraryLoadState::failed;
        entry.record.failure = std::string(message);
        entry.failure_reason = reason;
        entry.loading_thread = {};
        condition_.notify_all();
    }

    AndroidGuestProcess* process_{};
    const loader::ApkSelectedNativeLibraries* libraries_{};
    std::optional<AndroidApi> boundary_api_;
    std::vector<OwnedSystemLibrary> system_libraries_;
    core::Logger* logger_{};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::map<std::string, RegistryEntry, std::less<>> records_;
    NativeLibraryHandle next_handle_{1};
};

NativeLibraryLoader::NativeLibraryLoader(
    AndroidGuestProcess& process,
    const loader::ApkSelectedNativeLibraries& libraries)
    : impl_(std::make_unique<Impl>(process, libraries)) {}

NativeLibraryLoader::NativeLibraryLoader(
    AndroidGuestProcess& process,
    const loader::ApkSelectedNativeLibraries& libraries,
    const BionicProfile& bionic,
    const std::span<const BionicModuleSource> system_libraries,
    core::Logger* logger)
    : impl_(std::make_unique<Impl>(process, libraries, bionic,
                                   system_libraries, logger)) {}

NativeLibraryLoader::~NativeLibraryLoader() = default;

NativeLibraryLoadResult NativeLibraryLoader::LoadLibrary(
    const std::string_view logical_name,
    const JavaClassLoaderToken class_loader) {
    return impl_->LoadLibrary(logical_name, class_loader);
}

NativeLibraryLoadResult NativeLibraryLoader::LoadPath(
    const std::string_view guest_path,
    const JavaClassLoaderToken class_loader) {
    return impl_->LoadPath(guest_path, class_loader);
}

std::vector<NativeLibraryRecord> NativeLibraryLoader::Records() const {
    return impl_->Records();
}

std::string NativeLibraryLoader::SyntheticGuestPath(
    const std::string_view soname) {
    if (!IsLibraryBasename(soname)) {
        throw NativeLibraryLoadError(
            NativeLibraryLoadErrorReason::invalid_request,
            "native library soname is invalid");
    }
    return std::string(kSyntheticLibraryRoot) + std::string(soname);
}

}  // namespace ogplay::runtime
