#pragma once
#include "ogplay/app/library.h"
#include "ogplay/hal/file_lock.h"

#include <filesystem>
#include <optional>

namespace ogplay::app {

[[noreturn]] void ThrowImportCancelled();
void ThrowIfImportCancelled(const ImportProgress* progress);

// A data pack copied into <root>/import-staging outside the library lock. The
// held OS lock marks the directory live; unpublished copies are removed.
class StagedExternalData final {
public:
    StagedExternalData(const std::filesystem::path& library_root,
                       const std::filesystem::path& source, ImportProgress* progress);
    ~StagedExternalData();
    StagedExternalData(const StagedExternalData&) = delete;
    StagedExternalData& operator=(const StagedExternalData&) = delete;

    // Moves the copy to destination, which must be on the library volume.
    void Publish(const std::filesystem::path& destination);

private:
    std::optional<hal::FileLock> lock_;
    std::filesystem::path directory_;
};

}  // namespace ogplay::app
