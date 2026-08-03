#include "ogplay/core/capability_ledger.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace ogplay::core {
namespace {

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string ParseQuotedValue(const std::string& line) {
    const auto equals = line.find('=');
    if (equals == std::string::npos) {
        throw std::runtime_error("invalid capability assignment");
    }
    const auto value = Trim(line.substr(equals + 1));
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
        throw std::runtime_error("capability values must use quoted strings");
    }
    return value.substr(1, value.size() - 2);
}

}  // namespace

CapabilityLedger CapabilityLedger::Load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open capability ledger: " + path.string());
    }

    CapabilityLedger ledger;
    Capability current;
    bool in_section = false;
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            if (in_section) {
                ledger.Register(std::move(current));
            }
            current = Capability{};
            current.id = Trim(line.substr(1, line.size() - 2));
            if (current.id.empty()) {
                throw std::runtime_error("empty capability section");
            }
            in_section = true;
            continue;
        }
        if (!in_section) {
            throw std::runtime_error("capability assignment outside a section");
        }
        if (line.starts_with("status")) {
            current.status = ParseCapabilityStatus(ParseQuotedValue(line));
        } else if (line.starts_with("test")) {
            current.test = ParseQuotedValue(line);
        } else if (line.starts_with("note")) {
            current.note = ParseQuotedValue(line);
        } else {
            throw std::runtime_error("unknown capability key in section " + current.id);
        }
    }
    if (in_section) {
        ledger.Register(std::move(current));
    }
    return ledger;
}

CapabilityLedger::CapabilityLedger(CapabilityLedger&& other) noexcept {
    std::scoped_lock lock(other.mutex_);
    capabilities_ = std::move(other.capabilities_);
    hits_ = std::move(other.hits_);
    null_calls_ = std::move(other.null_calls_);
}

CapabilityLedger& CapabilityLedger::operator=(CapabilityLedger&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(mutex_, other.mutex_);
        capabilities_ = std::move(other.capabilities_);
        hits_ = std::move(other.hits_);
        null_calls_ = std::move(other.null_calls_);
    }
    return *this;
}

void CapabilityLedger::Register(Capability capability) {
    if (capability.id.empty()) {
        throw std::invalid_argument("capability id must not be empty");
    }
    std::scoped_lock lock(mutex_);
    const auto id = capability.id;
    const auto [unused, inserted] = capabilities_.emplace(id, std::move(capability));
    if (!inserted) {
        throw std::runtime_error("duplicate capability: " + id);
    }
}

std::optional<Capability> CapabilityLedger::Find(const std::string_view id) const {
    std::scoped_lock lock(mutex_);
    const auto item = capabilities_.find(id);
    return item == capabilities_.end() ? std::nullopt : std::optional<Capability>(item->second);
}

std::vector<Capability> CapabilityLedger::All() const {
    std::scoped_lock lock(mutex_);
    std::vector<Capability> result;
    result.reserve(capabilities_.size());
    for (const auto& [id, capability] : capabilities_) {
        static_cast<void>(id);
        result.push_back(capability);
    }
    return result;
}

void CapabilityLedger::RecordUnimplemented(const std::string_view id,
                                           const std::uint64_t link_register) {
    std::scoped_lock lock(mutex_);
    auto [item, inserted] = hits_.try_emplace(std::string(id));
    auto& hit = item->second;
    if (inserted) {
        hit.id = id;
        hit.first_lr = link_register;
    }
    ++hit.count;
    hit.last_lr = link_register;
}

std::vector<UnimplementedHit> CapabilityLedger::Unimplemented() const {
    std::scoped_lock lock(mutex_);
    std::vector<UnimplementedHit> result;
    result.reserve(hits_.size());
    for (const auto& [id, hit] : hits_) {
        static_cast<void>(id);
        result.push_back(hit);
    }
    return result;
}

void CapabilityLedger::RecordNullCall(const std::uint64_t link_register,
                                      const std::string_view symbol) {
    std::scoped_lock lock(mutex_);
    const auto key = std::make_pair(link_register, std::string(symbol));
    auto [item, inserted] = null_calls_.try_emplace(key);
    auto& hit = item->second;
    if (inserted) {
        hit.link_register = link_register;
        hit.symbol = symbol;
    }
    ++hit.count;
}

std::vector<NullCallHit> CapabilityLedger::NullCalls() const {
    std::scoped_lock lock(mutex_);
    std::vector<NullCallHit> result;
    result.reserve(null_calls_.size());
    for (const auto& [key, hit] : null_calls_) {
        static_cast<void>(key);
        result.push_back(hit);
    }
    return result;
}

std::string_view ToString(const CapabilityStatus status) noexcept {
    switch (status) {
    case CapabilityStatus::unimplemented: return "unimplemented";
    case CapabilityStatus::stub: return "stub";
    case CapabilityStatus::partial: return "partial";
    case CapabilityStatus::complete: return "complete";
    }
    return "unknown";
}

CapabilityStatus ParseCapabilityStatus(const std::string_view value) {
    if (value == "unimplemented") return CapabilityStatus::unimplemented;
    if (value == "stub") return CapabilityStatus::stub;
    if (value == "partial") return CapabilityStatus::partial;
    if (value == "complete") return CapabilityStatus::complete;
    throw std::invalid_argument("unknown capability status: " + std::string(value));
}

}  // namespace ogplay::core
