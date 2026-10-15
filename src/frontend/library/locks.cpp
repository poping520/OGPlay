#include "locks.h"
namespace ogplay::app {
std::optional<hal::FileLock> TryLibraryLock(const std::filesystem::path& root) {
    try {
        std::filesystem::create_directories(root / "library");
        return hal::FileLock::TryAcquire(root / "library/.lock");
    } catch (const std::exception& e) { throw LibraryError(LibraryErrorCode::io_error, std::string("library lock IO failed: ") + e.what(), root); }
}
hal::FileLock RequireLibraryLock(const std::filesystem::path& root) {
    auto lock = TryLibraryLock(root);
    if (!lock) throw LibraryError(LibraryErrorCode::busy, "library_busy: library is locked; wait for the other operation and retry", root);
    return std::move(*lock);
}
std::optional<hal::FileLock> TryRunningLock(const LibraryStore& store, std::string_view id) {
    if (id.empty() || id == "." || id == ".." || id.find_first_of("/\\:") != std::string_view::npos || id.find('\0') != std::string_view::npos)
        throw LibraryError(LibraryErrorCode::invalid_argument, "invalid installation id");
    const auto text = std::u8string_view(reinterpret_cast<const char8_t*>(id.data()), id.size());
    const auto directory = store.EntriesRoot() / std::filesystem::path(text);
    if (!std::filesystem::is_directory(directory)) throw LibraryError(LibraryErrorCode::not_found, "library entry does not exist", directory);
    try { return hal::FileLock::TryAcquire(directory / ".running.lock"); }
    catch (const std::exception& e) { throw LibraryError(LibraryErrorCode::io_error, std::string("running lock IO failed: ") + e.what(), directory); }
}
struct RunningLease::Impl {
    hal::FileLock lock;
    explicit Impl(hal::FileLock value) : lock(std::move(value)) {}
};
RunningLease::RunningLease(const std::filesystem::path& root, std::string_view id) {
    auto library = RequireLibraryLock(root);
    auto running = TryRunningLock(LibraryStore(root), id);
    if (!running) throw LibraryError(LibraryErrorCode::running, "instance_running: game is already running; stop it before launching or removing it");
    impl_ = std::make_unique<Impl>(std::move(*running));
}
RunningLease::~RunningLease() = default;
RunningLease::RunningLease(RunningLease&&) noexcept = default;
RunningLease& RunningLease::operator=(RunningLease&&) noexcept = default;
bool LibraryStore::IsRunning(std::string_view id) const {
    return !TryRunningLock(*this, id).has_value();
}
}
