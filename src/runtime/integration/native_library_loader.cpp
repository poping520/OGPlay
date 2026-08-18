#include "ogplay/runtime/integration/native_library_loader.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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
        return LoadResolved(*library, class_loader);
    }

    NativeLibraryLoadResult LoadPath(
        const std::string_view guest_path,
        const JavaClassLoaderToken class_loader) {
        const loader::ApkNativeLibrary* library{};
        if (guest_path.starts_with(kSyntheticLibraryRoot)) {
            const auto soname = guest_path.substr(kSyntheticLibraryRoot.size());
            if (IsLibraryBasename(soname)) {
                library = libraries_->FindSoname(soname);
            }
        } else {
            library = libraries_->FindEntryName(guest_path);
        }
        if (library == nullptr) {
            throw NativeLibraryLoadError(
                NativeLibraryLoadErrorReason::library_not_found,
                "APK native guest path is unavailable: " +
                    std::string(guest_path));
        }
        return LoadResolved(*library, class_loader);
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

    [[nodiscard]] std::vector<const loader::ApkNativeLibrary*> Closure(
        const loader::ApkNativeLibrary& root) const {
        std::vector<const loader::ApkNativeLibrary*> result{&root};
        std::set<std::string, std::less<>> selected{root.soname};
        for (std::size_t index = 0; index < result.size(); ++index) {
            const auto& library = *result[index];
            try {
                const auto image = loader::ParseElf32Arm(library.image);
                if (image.type != loader::Elf32ImageType::shared_object) {
                    throw NativeLibraryLoadError(
                        NativeLibraryLoadErrorReason::malformed_elf,
                        "APK native library is not ET_DYN: " +
                            library.entry_name);
                }
                const auto dynamic = loader::ReadElf32DynamicInfo(
                    library.image, image);
                if (dynamic.soname.has_value() &&
                    *dynamic.soname != library.soname) {
                    throw NativeLibraryLoadError(
                        NativeLibraryLoadErrorReason::soname_mismatch,
                        "APK native DT_SONAME disagrees with inventory: " +
                            library.entry_name);
                }
                for (const auto& needed : dynamic.needed) {
                    if (selected.contains(needed) ||
                        process_->HasLoadedModule(needed)) {
                        continue;
                    }
                    const auto* dependency = libraries_->FindSoname(needed);
                    if (dependency == nullptr) continue;
                    selected.insert(needed);
                    result.push_back(dependency);
                }
            } catch (const NativeLibraryLoadError&) {
                throw;
            } catch (const std::exception& error) {
                throw NativeLibraryLoadError(
                    NativeLibraryLoadErrorReason::malformed_elf,
                    "APK native ELF is malformed: " + library.entry_name +
                        ": " + error.what());
            }
        }
        return result;
    }

    [[nodiscard]] NativeLibraryLoadResult LoadResolved(
        const loader::ApkNativeLibrary& library,
        const JavaClassLoaderToken class_loader) {
        const auto canonical_path = SyntheticGuestPath(library.soname);
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
            entry.record.soname = library.soname;
            entry.record.class_loader = class_loader;
            entry.loading_thread = std::this_thread::get_id();
            records_.emplace(canonical_path, std::move(entry));
        }

        try {
            const auto closure = Closure(library);
            std::vector<AndroidGuestApplicationModuleSource> sources;
            sources.reserve(closure.size());
            for (const auto* module : closure) {
                if (process_->HasLoadedModule(module->soname)) continue;
                sources.push_back({module->soname, module->image});
            }
            auto application = process_->LoadApplicationModules(
                library.soname, sources);
            std::optional<std::uint32_t> jni_version;
            try {
                jni_version = process_->InitializeExplicitJniLibrary(
                    library.soname);
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
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::map<std::string, RegistryEntry, std::less<>> records_;
    NativeLibraryHandle next_handle_{1};
};

NativeLibraryLoader::NativeLibraryLoader(
    AndroidGuestProcess& process,
    const loader::ApkSelectedNativeLibraries& libraries)
    : impl_(std::make_unique<Impl>(process, libraries)) {}

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
