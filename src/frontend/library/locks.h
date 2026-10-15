#pragma once
#include "ogplay/app/library.h"
#include "ogplay/hal/file_lock.h"
namespace ogplay::app {
// Non-blocking exclusive probe; only opportunistic cleanup may skip on contention.
std::optional<hal::FileLock> TryLibraryLock(const std::filesystem::path& root);
// Bounded waits: brief holders (list refresh, launch lease) finish; long holders yield busy.
hal::FileLock RequireLibraryLock(const std::filesystem::path& root);
hal::FileLock RequireLibraryReadLock(const std::filesystem::path& root);
std::optional<hal::FileLock> TryRunningLock(const LibraryStore& store, std::string_view id);
}
