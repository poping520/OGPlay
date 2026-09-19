#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "ogplay/audio/encoded_audio.h"

TEST_CASE("encoded audio source window copies only the requested interval") {
    const std::array bytes{std::byte{1}, std::byte{2}, std::byte{3},
                           std::byte{4}, std::byte{5}};
    CHECK(ogplay::audio::SliceSourceWindow(bytes, 2U, 2U) ==
          std::vector<std::byte>({std::byte{3}, std::byte{4}}));
    CHECK(ogplay::audio::SliceSourceWindow(bytes, 1U, 0U).size() == 4U);
    CHECK_THROWS_AS(static_cast<void>(ogplay::audio::SliceSourceWindow(
                        bytes, 6U, 1U)),
                    std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(ogplay::audio::SliceSourceWindow(
                        bytes, 0U, 6U)),
                    std::invalid_argument);
    CHECK(ogplay::audio::SliceSourceWindow(
              bytes, 1U, std::numeric_limits<std::uint64_t>::max())
              .size() == 4U);
    CHECK(ogplay::audio::SliceSourceWindow(
              bytes, 1U,
              static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()))
              .size() == 4U);
}
