// Copyright (C) 2006 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0
// Asset path ordering follows android-4.4.4_r2.0.1 libs/androidfw/AssetManager.cpp.
// ZIP validation and streaming DEFLATE are shared with loader/apk.cpp.
#include "binary.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>

namespace ogplay::runtime::resources {
using detail::require;

namespace {
std::atomic<std::size_t> count{};

bool safePath(std::string_view path) {
    if (path.starts_with('/') || path.find('\\') != std::string_view::npos || path.find('\0') != std::string_view::npos)
        return false;
    while (!path.empty()) {
        auto slash = path.find('/');
        auto part = path.substr(0, slash);
        if (part == "." || part == "..") return false;
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return true;
}
}  // namespace

AssetPath::AssetPath(Source source) : source_(std::move(source)), archive_(loader::ParseApkArchive(source_.bytes)) {
    require(source_.owner != nullptr && !source_.name.empty(), "asset source requires ownership and a name");
    entries_.reserve(archive_.entries.size());
    for (const auto& entry : archive_.entries)
        entries_.emplace(entry.name, &entry);
    if (const auto* e = find("resources.arsc")) {
        if (e->compression_method == 0) {
            auto arsc = source_;
            arsc.bytes = loader::ValidatedStoredApkEntryData(source_.bytes, *e);
            table_ = std::make_shared<ResTableData>(std::move(arsc));
        } else {
            require(e->uncompressed_size <= 32U * 1024 * 1024, "compressed resource table exceeds memory budget");
            auto data =
                std::make_shared<const std::vector<std::byte>>(loader::ReadApkEntry(source_.bytes, archive_, e->name));
            table_ = std::make_shared<ResTableData>(Source{data, *data, source_.name, source_.isUpToDate});
        }
    }
}

const Source& AssetPath::source() const {
    return source_;
}

const loader::ApkArchive& AssetPath::archive() const {
    return archive_;
}

const loader::ApkEntry* AssetPath::find(std::string_view name) const {
    const auto found = entries_.find(name);
    return found == entries_.end() ? nullptr : found->second;
}

std::shared_ptr<const ResTableData> AssetPath::table() const {
    return table_;
}

std::shared_ptr<const AssetPath> sharedFramework(const std::function<Source()>& provider) {
    static std::once_flag once;
    static std::shared_ptr<const AssetPath> framework;
    std::call_once(once, [&] {
        auto p = std::make_shared<AssetPath>(provider());
        require(p->table() != nullptr, "framework has no resources.arsc");
        const auto* e = p->find("resources.arsc");
        require(e->compression_method == 0, "framework table must be STORED");
        framework = std::move(p);
    });
    return framework;
}

Asset::Asset(std::shared_ptr<const AssetPath> path, loader::ApkEntry entry, std::int32_t cookie)
    : path_(std::move(path)), entry_(std::move(entry)), cookie_(cookie) {
    require(entry_.compression_method == 0 || entry_.compression_method == 8, "unsupported asset compression");
    // Keep memory constant and bound the work of replaying a backward seek.
    require(entry_.compression_method == 0 || entry_.uncompressed_size <= 256U * 1024 * 1024,
            "deflated asset exceeds expanded-size budget");
    if (entry_.compression_method == 0)
        stored_ = loader::ValidatedStoredApkEntryData(path_->source().bytes, entry_);
    else
        stream_ = std::make_unique<loader::ApkDeflateStream>(path_->source().bytes, entry_);
    ++count;
}

Asset::~Asset() {
    --count;
}

std::size_t Asset::read(std::span<std::byte> destination) {
    const auto offset = static_cast<std::size_t>(position_);
    const auto n = stream_ ? stream_->Read(offset, destination) : std::min(destination.size(), stored_.size() - offset);
    if (!stream_) std::copy_n(stored_.subspan(offset).begin(), n, destination.begin());
    position_ += n;
    return n;
}

std::uint64_t Asset::seek(std::int64_t offset, int whence) {
    require(whence == 0 || whence == 1 || whence == 2, "invalid asset seek origin");
    auto base = whence == 0 ? 0 : static_cast<std::int64_t>(whence == 1 ? position_ : length());
    require(offset >= -base && offset <= static_cast<std::int64_t>(length()) - base, "asset seek outside file");
    position_ = static_cast<std::uint64_t>(base + offset);
    return position_;
}

std::uint64_t Asset::length() const {
    return entry_.uncompressed_size;
}

std::uint64_t Asset::remaining() const {
    return length() - position_;
}

std::int32_t Asset::cookie() const {
    return cookie_;
}

std::string_view Asset::name() const {
    return entry_.name;
}

bool Asset::isStored() const {
    return entry_.compression_method == 0;
}

Bytes Asset::storedBuffer() const {
    require(entry_.compression_method == 0, "compressed asset has no stored buffer");
    return stored_;
}

AssetFd Asset::openFd() const {
    require(entry_.compression_method == 0, "compressed asset cannot be opened as FD");
    return {path_->source(), static_cast<std::uint64_t>(stored_.data() - path_->source().bytes.data()), length()};
}

std::size_t Asset::globalCount() {
    return count.load();
}

AssetManager::AssetManager(std::shared_ptr<const AssetPath> framework) {
    require(framework && framework->table(), "missing framework resource path");
    paths_.push_back(std::move(framework));
    table_.add(paths_.front()->table(), 1);
}

std::int32_t AssetManager::addAssetPath(Source source) {
    for (std::size_t i = 0; i < paths_.size(); ++i)
        if (paths_[i]->source().name == source.name) return static_cast<std::int32_t>(i + 1);
    auto p = std::make_shared<AssetPath>(std::move(source));
    auto cookie = static_cast<std::int32_t>(paths_.size() + 1);
    if (p->table()) table_.add(p->table(), cookie);
    paths_.push_back(std::move(p));
    return cookie;
}

std::unique_ptr<Asset> AssetManager::open(std::string_view name) const {
    if (!safePath(name)) return {};
    return openNonAsset("assets/" + std::string(name));
}

std::unique_ptr<Asset> AssetManager::openNonAsset(std::string_view name, std::int32_t cookie) const {
    if (!safePath(name) || cookie < 0 || static_cast<std::size_t>(cookie) > paths_.size()) return {};
    for (std::size_t i = paths_.size(); i > 0; --i) {
        if (cookie && static_cast<std::size_t>(cookie) != i) continue;
        const auto& p = paths_[i - 1];
        if (const auto* e = p->find(name); e && !e->name.ends_with('/'))
            return std::make_unique<Asset>(p, *e, static_cast<std::int32_t>(i));
    }
    return {};
}

std::vector<std::string> AssetManager::list(std::string_view directory) const {
    require(safePath(directory), "invalid asset directory");
    std::string prefix = "assets/";
    prefix += directory;
    if (!prefix.ends_with('/')) prefix += '/';
    std::set<std::string> names;
    for (const auto& p : paths_)
        for (const auto& e : p->archive().entries)
            if (e.name.starts_with(prefix)) {
                auto tail = std::string_view(e.name).substr(prefix.size());
                if (!tail.empty()) names.emplace(tail.substr(0, tail.find('/')));
            }
    return {names.begin(), names.end()};
}

std::string AssetManager::getCookieName(std::int32_t cookie) const {
    require(cookie > 0 && static_cast<std::size_t>(cookie) <= paths_.size(), "invalid asset cookie");
    return paths_[static_cast<std::size_t>(cookie) - 1]->source().name;
}

bool AssetManager::isUpToDate() const {
    return std::all_of(paths_.begin(), paths_.end(),
                       [](const auto& p) { return !p->source().isUpToDate || p->source().isUpToDate(); });
}

ResTable& AssetManager::resources() {
    return table_;
}

const ResTable& AssetManager::resources() const {
    return table_;
}
}  // namespace ogplay::runtime::resources

namespace ogplay::runtime::resources {
void AssetManager::setConfiguration(const ResTable_config& config, std::optional<std::string_view> locale) {
    if (locale)
        locale_ = *locale;
    else if (config.language[0])
        locale_ = config.localeName();
    auto next = config;
    next.language[0] = next.language[1] = next.country[0] = next.country[1] = 0;
    if (locale_.size() >= 2) {
        next.language[0] = locale_[0];
        next.language[1] = locale_[1];
    }
    if (locale_.size() >= 5) {
        next.country[0] = locale_[3];
        next.country[1] = locale_[4];
    }
    table_.setParameters(next);
}
}  // namespace ogplay::runtime::resources
