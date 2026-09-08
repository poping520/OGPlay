#include "ogplay/runtime/integration/guest_process_environment.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace ogplay::runtime {
namespace {

void ValidateText(const std::string_view text, const std::size_t maximum,
                  const char* label) {
    if (text.size() > maximum || text.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string("guest environment ") + label +
                                    " is invalid");
    }
}

}  // namespace

GuestProcessEnvironment::GuestProcessEnvironment(
    std::vector<GuestProcessEnvironmentEntry> entries)
    : entries_(std::move(entries)) {
    if (entries_.size() > kMaximumEntries) {
        throw std::invalid_argument("guest environment has too many entries");
    }
    std::unordered_set<std::string> names;
    std::size_t bytes = (entries_.size() + 1U) * sizeof(std::uint32_t);
    for (const auto& entry : entries_) {
        ValidateText(entry.name, kMaximumNameBytes, "name");
        ValidateText(entry.value, kMaximumValueBytes, "value");
        if (entry.name.empty() || entry.name.find('=') != std::string::npos) {
            throw std::invalid_argument("guest environment name is invalid");
        }
        if (!names.insert(entry.name).second) {
            throw std::invalid_argument("guest environment name is duplicated");
        }
        const auto item_bytes = entry.name.size() + entry.value.size() + 2U;
        if (bytes > kMaximumSerializedBytes ||
            item_bytes > kMaximumSerializedBytes - bytes) {
            throw std::invalid_argument("guest environment is too large");
        }
        bytes += item_bytes;
    }
    if (bytes > kMaximumSerializedBytes) {
        throw std::invalid_argument("guest environment is too large");
    }
    serialized_bytes_ = bytes;
}

GuestProcessEnvironment GuestProcessEnvironment::Api19(
    std::string external_storage_root) {
    if (external_storage_root.empty() || external_storage_root.front() != '/' ||
        external_storage_root.find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "guest external storage root must be an absolute path");
    }
    return GuestProcessEnvironment({
        {"PATH", "/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin"},
        {"ANDROID_ROOT", "/system"},
        {"ANDROID_DATA", "/data"},
        {"EXTERNAL_STORAGE", std::move(external_storage_root)},
    });
}

std::span<const GuestProcessEnvironmentEntry>
GuestProcessEnvironment::Entries() const noexcept {
    return entries_;
}

std::optional<std::string_view> GuestProcessEnvironment::Find(
    const std::string_view name) const noexcept {
    const auto found = std::ranges::find(entries_, name,
                                         &GuestProcessEnvironmentEntry::name);
    if (found == entries_.end()) return std::nullopt;
    return found->value;
}

std::size_t GuestProcessEnvironment::SerializedBytes() const noexcept {
    return serialized_bytes_;
}

}  // namespace ogplay::runtime
