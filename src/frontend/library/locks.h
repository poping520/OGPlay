#pragma once
#include "ogplay/app/library.h"
#include "ogplay/hal/file_lock.h"
namespace ogplay::app {
std::optional<hal::FileLock> TryLibraryLock(const std::filesystem::path& root);
hal::FileLock RequireLibraryLock(const std::filesystem::path& root);
std::optional<hal::FileLock> TryRunningLock(const LibraryStore& store, std::string_view id);
}
