#pragma once
#include <cstddef>
#include <span>
#include <string>
namespace ogplay::core {
[[nodiscard]] std::string Sha256(std::span<const std::byte> bytes);
}
