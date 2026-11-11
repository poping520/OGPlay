#pragma once

#include <cstddef>
#include <vector>

namespace ogplay::test {
// Minimal package archive accepted by both the binary Manifest reader and ROM PackageParser.
std::vector<std::byte> PackageManifestFixture();
}
