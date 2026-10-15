#include <doctest/doctest.h>
#include "ogplay/hal/file_lock.h"
#include <filesystem>
TEST_CASE("HAL file locks conflict across handles and release on scope exit") {
    const auto root = std::filesystem::temp_directory_path() / "ogplay-hal-file-lock";
    std::filesystem::create_directories(root);
    {
        auto first = ogplay::hal::FileLock::TryAcquire(root / ".lock");
        REQUIRE(first);
        CHECK_FALSE(ogplay::hal::FileLock::TryAcquire(root / ".lock"));
        auto moved = std::move(*first);
        first.reset();
        CHECK_FALSE(ogplay::hal::FileLock::TryAcquire(root / ".lock"));
    }
    CHECK(ogplay::hal::FileLock::TryAcquire(root / ".lock"));
    std::filesystem::remove_all(root);
}
