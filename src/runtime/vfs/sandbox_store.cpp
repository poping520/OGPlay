// Per-title persistent sandbox storage (ADR-0020, docs/design/sandbox/ 02-03).
// fs/ mirrors guest absolute paths 1:1 so a user can read, back up and
// delete individual saves by hand; names are percent-escaped only where a
// host would refuse them, and the escape is byte-exact in both directions.

#include "ogplay/runtime/vfs/sandbox_store.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <map>
#include <system_error>

#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

constexpr std::int32_t kEnoent = 2;
constexpr std::int32_t kEio = 5;
constexpr std::int32_t kEexist = 17;
constexpr std::int32_t kEnotdir = 20;
constexpr std::int32_t kEinval = 22;
constexpr std::int32_t kEnospc = 28;
constexpr std::int32_t kEnotempty = 39;
constexpr std::int32_t kEnametoolong = 36;

constexpr std::string_view kTombstoneSuffix = ".__ogplay_tombstone__";
constexpr std::string_view kTemporarySuffix = ".__ogplay_tmp__";

[[nodiscard]] std::string FoldGuestPath(const std::string_view path) {
    std::string folded;
    folded.reserve(path.size());
    for (const auto character : path) {
        folded.push_back(character >= 'A' && character <= 'Z'
                             ? static_cast<char>(character - 'A' + 'a')
                             : character);
    }
    return folded;
}

template <typename Integer>
[[nodiscard]] Integer ParseMetaInteger(const std::string& text,
                                       const std::string_view key) {
    Integer value{};
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        throw VfsError(kEinval, "sandbox meta.toml has a malformed " +
                                    std::string(key));
    }
    return value;
}
constexpr std::string_view kMetaName = "meta.toml";
constexpr int kCurrentMetaSchema = 3;
// Host limits vary; this keeps the escaped form comfortably inside the
// shortest of them instead of discovering it at write time.
constexpr std::size_t kMaximumSegmentLength = 200;

[[nodiscard]] bool IsReservedDeviceName(const std::string_view segment) {
    static constexpr std::array<std::string_view, 22> kReserved{
        "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4",
        "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3",
        "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const auto dot = segment.find('.');
    const auto stem = segment.substr(0, dot);
    std::string folded;
    folded.reserve(stem.size());
    for (const auto character : stem) {
        folded.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(character))));
    }
    return std::find(kReserved.begin(), kReserved.end(), folded) !=
           kReserved.end();
}

[[nodiscard]] bool NeedsEscape(const char character) {
    const auto value = static_cast<unsigned char>(character);
    if (value < 0x20 || value == 0x7f) return true;
    switch (character) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '|':
        case '?':
        case '*':
        case '\\':
        case '/':
        // '%' is the escape marker itself, so it must always be escaped for
        // the translation to be reversible.
        case '%':
            return true;
        default:
            return false;
    }
}

void AppendEscaped(std::string& out, const char character) {
    static constexpr std::string_view kHex = "0123456789ABCDEF";
    const auto value = static_cast<unsigned char>(character);
    out.push_back('%');
    out.push_back(kHex[value >> 4U]);
    out.push_back(kHex[value & 0x0fU]);
}

[[nodiscard]] int HexValue(const char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    return -1;
}

[[nodiscard]] bool IsAndroidId(const std::string_view value) {
    if (value.size() != 16U) return false;
    return std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

[[nodiscard]] std::string AndroidIdFromEntropy(
    const std::span<const std::byte> entropy) {
    if (entropy.size() != 8U) {
        throw VfsError(kEinval,
                       "ANDROID_ID entropy must contain exactly 8 bytes");
    }
    static constexpr std::string_view kHex = "0123456789abcdef";
    std::string value;
    value.reserve(16U);
    for (const auto item : entropy) {
        const auto byte = std::to_integer<std::uint8_t>(item);
        value.push_back(kHex[byte >> 4U]);
        value.push_back(kHex[byte & 0x0fU]);
    }
    return value;
}

[[nodiscard]] std::vector<std::string> SplitGuestPath(
    const std::string_view path) {
    if (path.empty() || path.front() != '/') {
        throw VfsError(kEnotdir, "sandbox path must be absolute");
    }
    std::vector<std::string> segments;
    std::size_t cursor = 1;
    while (cursor <= path.size()) {
        const auto end = path.find('/', cursor);
        const auto count =
            (end == std::string_view::npos ? path.size() : end) - cursor;
        const auto segment = path.substr(cursor, count);
        if (segment == "..") {
            throw VfsError(kEinval, "sandbox path traversal is forbidden");
        }
        if (!segment.empty() && segment != ".") {
            segments.emplace_back(segment);
        }
        if (end == std::string_view::npos) break;
        cursor = end + 1;
    }
    if (segments.empty()) {
        throw VfsError(kEinval, "sandbox path has no segments");
    }
    return segments;
}

[[nodiscard]] std::string JoinGuestPath(
    const std::span<const std::string> segments) {
    std::string path;
    for (const auto& segment : segments) {
        path.push_back('/');
        path.append(segment);
    }
    return path;
}

void RequireNoReservedSuffix(const std::string_view path) {
    if (path.ends_with(kTombstoneSuffix) || path.ends_with(kTemporarySuffix)) {
        throw VfsError(kEinval,
                       "sandbox reserved suffix is not a usable guest path");
    }
}

[[nodiscard]] std::uint64_t HostFileSize(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw VfsError(kEio, "cannot size a sandbox file");
    return size;
}

}  // namespace

class SandboxStore::Impl final {
public:
    std::filesystem::path directory;
    std::filesystem::path overlay;
    std::string installation_id;
    std::string package;
    SandboxConfig config;
    std::uint64_t used_bytes{};
    std::uint64_t temporaries_removed{};
    std::uint32_t version_code{};
    std::string android_id;
    // Guest path -> entry, ordered so enumeration is deterministic.
    std::map<std::string, SandboxEntry, std::less<>> entries;

    [[nodiscard]] std::filesystem::path HostPathFor(
        const std::string_view guest_path, const bool tombstone) const {
        const auto segments = SplitGuestPath(guest_path);
        std::filesystem::path host;
        std::size_t begin{};
        const auto matches = [&](const std::initializer_list<std::string_view> prefix) {
            if (segments.size() < prefix.size()) return false;
            return std::equal(prefix.begin(), prefix.end(), segments.begin());
        };
        if (matches({"data", "data", package})) {
            host = directory / "internal";
            begin = 3;
        } else if (matches({"sdcard", "android", "data", package})) {
            host = directory / "external";
            begin = 4;
        } else if (matches({"sdcard", "android", "obb", package})) {
            host = directory / "obb";
            begin = 4;
        } else if (!segments.empty() && segments.front() == "sdcard") {
            host = directory / "sdcard";
            begin = 1;
        } else {
            throw VfsError(kEinval, "sandbox path is outside its semantic roots");
        }
        for (std::size_t index = begin; index < segments.size(); ++index) {
            auto name = SandboxStore::EscapeSegment(segments[index]);
            if (tombstone && index + 1 == segments.size()) {
                name.append(kTombstoneSuffix);
            }
            if (name.size() > kMaximumSegmentLength) {
                throw VfsError(kEnametoolong,
                               "sandbox host name is too long: " +
                                   std::string(guest_path));
            }
            host /= name;
        }
        return host;
    }

    void RequireQuota(const std::uint64_t added_bytes,
                      const std::uint64_t removed_bytes,
                      const bool adds_entry) const {
        const auto projected = used_bytes - removed_bytes + added_bytes;
        if (projected > config.byte_quota) {
            throw VfsError(kEnospc,
                           "sandbox byte quota exhausted for " + package);
        }
        const auto active_entries = static_cast<std::uint64_t>(
            std::count_if(entries.begin(), entries.end(), [](const auto& item) {
                return !item.second.is_tombstone;
            }));
        if (adds_entry && active_entries >= config.maximum_files) {
            throw VfsError(kEnospc,
                           "sandbox file count limit reached for " + package);
        }
    }

    void CreateParentDirectories(const std::filesystem::path& host) const {
        std::error_code error;
        std::filesystem::create_directories(host.parent_path(), error);
        if (error) {
            throw VfsError(kEio, "cannot create a sandbox directory");
        }
    }

    void LoadRoot(const std::string_view root_name,
                  const std::string_view guest_prefix,
                  std::map<std::string, std::string, std::less<>>& folded_paths) {
        overlay = directory / root_name;
        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(overlay, error);
        const std::filesystem::recursive_directory_iterator end;
        if (error) throw VfsError(kEio, "cannot enumerate the sandbox");
        std::vector<std::filesystem::path> temporaries;
        while (iterator != end) {
            const auto host = iterator->path();
            const auto status = iterator->symlink_status(error);
            if (error) throw VfsError(kEio, "cannot inspect a sandbox entry");
            if (std::filesystem::is_symlink(status)) {
                throw VfsError(kEinval,
                               "sandbox contains a symbolic link: " +
                                   host.string());
            }
            const auto relative = host.lexically_relative(overlay);
            if (relative.empty() || relative.is_absolute()) {
                throw VfsError(kEinval, "sandbox entry escaped its root");
            }
            std::vector<std::string> segments;
            for (const auto& part : relative) {
                segments.push_back(part.string());
            }
            auto& last = segments.back();
            const bool temporary =
                std::string_view(last).ends_with(kTemporarySuffix);
            const bool tombstone =
                std::string_view(last).ends_with(kTombstoneSuffix);
            if (temporary) {
                temporaries.push_back(host);
                iterator.increment(error);
                if (error) throw VfsError(kEio, "cannot walk the sandbox");
                continue;
            }
            if (tombstone) {
                last.resize(last.size() - kTombstoneSuffix.size());
            }
            for (auto& segment : segments) {
                segment = SandboxStore::UnescapeSegment(segment);
            }
            auto guest_path = std::string(guest_prefix);
            const auto relative_guest = JoinGuestPath(segments);
            guest_path.append(relative_guest);
            const auto folded = FoldGuestPath(guest_path);
            if (const auto conflict = folded_paths.find(folded);
                conflict != folded_paths.end() &&
                conflict->second != guest_path) {
                throw VfsError(kEexist,
                               "sandbox has an ASCII case-folding conflict: " +
                                   conflict->second + " and " + guest_path);
            }
            folded_paths.insert_or_assign(folded, guest_path);
            SandboxEntry entry;
            entry.path = guest_path;
            entry.is_directory = std::filesystem::is_directory(status);
            entry.is_tombstone = tombstone;
            if (entry.is_directory && tombstone) {
                throw VfsError(kEinval,
                               "sandbox tombstone must not be a directory");
            }
            if (!entry.is_directory && !std::filesystem::is_regular_file(status)) {
                throw VfsError(kEinval,
                               "sandbox contains a special file: " +
                                   host.string());
            }
            if (!entry.is_directory && !tombstone) {
                entry.size = HostFileSize(host);
                used_bytes += entry.size;
            }
            if (!entries.emplace(guest_path, std::move(entry)).second) {
                // Two host names decoding to one guest path can only happen
                // if somebody edited the sandbox by hand.
                throw VfsError(kEexist,
                               "sandbox has a duplicate guest path: " +
                                   guest_path);
            }
            iterator.increment(error);
            if (error) throw VfsError(kEio, "cannot walk the sandbox");
        }
        // Temporaries can only be crash residue; the committed files next to
        // them were never damaged.
        for (const auto& path : temporaries) {
            std::filesystem::remove(path, error);
            if (!error) ++temporaries_removed;
        }
    }

    void Load() {
        std::map<std::string, std::string, std::less<>> folded_paths;
        LoadRoot("internal", "/data/data/" + package, folded_paths);
        LoadRoot("external", "/sdcard/android/data/" + package, folded_paths);
        LoadRoot("obb", "/sdcard/android/obb/" + package, folded_paths);
        LoadRoot("sdcard", "/sdcard", folded_paths);
    }

    void LoadMeta() {
        const auto path = directory / kMetaName;
        std::error_code error;
        if (!std::filesystem::exists(path, error))
            throw VfsError(kEinval, "existing sandbox has no meta.toml");
        std::ifstream input(path);
        if (!input) throw VfsError(kEio, "cannot read the sandbox meta.toml");
        std::string line;
        std::uint32_t schema = 0;
        std::string stored_package;
        while (std::getline(input, line)) {
            while (!line.empty() &&
                   (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.empty() || line.front() == '#') continue;
            const auto equals = line.find('=');
            if (equals == std::string::npos) {
                throw VfsError(kEinval, "sandbox meta.toml is malformed");
            }
            auto key = line.substr(0, equals);
            auto value = line.substr(equals + 1);
            const auto trim = [](std::string& text) {
                while (!text.empty() && text.front() == ' ') text.erase(0, 1);
                while (!text.empty() && text.back() == ' ') text.pop_back();
            };
            trim(key);
            trim(value);
            const auto unquote = [](std::string text) {
                if (text.size() >= 2 && text.front() == '"' &&
                    text.back() == '"') {
                    return text.substr(1, text.size() - 2);
                }
                return text;
            };
            if (key == "schema") {
                schema = ParseMetaInteger<std::uint32_t>(value, key);
            } else if (key == "package") {
                stored_package = unquote(value);
            } else if (key == "version_code") {
                version_code = ParseMetaInteger<std::uint32_t>(value, key);
            } else if (key == "android_id") {
                android_id = unquote(value);
            } else {
                // No guessing at migrations: an unknown key means this
                // directory was written by something else.
                throw VfsError(kEinval,
                               "sandbox meta.toml has an unknown key: " + key);
            }
        }
        if (schema != kCurrentMetaSchema) {
            throw VfsError(kEinval,
                           "sandbox meta.toml schema is not supported; back "
                           "up and clear " + directory.string());
        }
        if (stored_package != package) {
            throw VfsError(kEinval,
                           "sandbox meta.toml belongs to " + stored_package +
                               ", not " + package);
        }
        if (!android_id.empty() && !IsAndroidId(android_id)) {
            throw VfsError(kEinval,
                           "sandbox meta.toml has a malformed android_id");
        }
    }

    void WriteMeta() const {
        std::string text = "schema = ";
        text.append(std::to_string(kCurrentMetaSchema));
        text.append("\npackage = \"");
        text.append(package);
        text.append("\"\nversion_code = ");
        text.append(std::to_string(version_code));
        text.push_back('\n');
        if (!android_id.empty()) {
            text.append("android_id = \"");
            text.append(android_id);
            text.append("\"\n");
        }
        const auto target = directory / kMetaName;
        auto temporary = target;
        temporary += std::string(kTemporarySuffix);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw VfsError(kEio, "cannot write the sandbox meta.toml");
            }
            output.write(text.data(),
                         static_cast<std::streamsize>(text.size()));
            if (!output) {
                throw VfsError(kEio, "cannot write the sandbox meta.toml");
            }
        }
        std::error_code error;
        std::filesystem::rename(temporary, target, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            throw VfsError(kEio, "cannot replace the sandbox meta.toml");
        }
    }
};

SandboxStore::SandboxStore(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
SandboxStore::~SandboxStore() = default;

std::unique_ptr<SandboxStore> SandboxStore::Open(
    const std::filesystem::path& root, const std::string_view installation_id,
    const std::string_view package,
    const SandboxConfig config) {
    return OpenInternal(root, installation_id, package, config, false);
}

std::unique_ptr<SandboxStore> SandboxStore::Create(
    const std::filesystem::path& root, const std::string_view installation_id,
    const std::string_view package,
    const SandboxConfig config) {
    return OpenInternal(root, installation_id, package, config, true);
}

std::unique_ptr<SandboxStore> SandboxStore::OpenInternal(
    const std::filesystem::path& root, const std::string_view installation_id,
    const std::string_view package, const SandboxConfig config,
    const bool require_new) {
    if (package.empty()) {
        throw VfsError(kEinval, "sandbox package name is empty");
    }
    for (const auto character : package) {
        const auto value = static_cast<unsigned char>(character);
        const bool allowed = (value >= 'a' && value <= 'z') ||
                             (value >= 'A' && value <= 'Z') ||
                             (value >= '0' && value <= '9') ||
                             character == '.' || character == '_';
        if (!allowed) {
            throw VfsError(kEinval,
                           "sandbox package name has an unusable character");
        }
    }
    if (package.front() == '.' || package.ends_with('.')) {
        throw VfsError(kEinval, "sandbox package name is malformed");
    }
    const auto valid_id = installation_id == package ||
        (installation_id.starts_with(package) &&
         installation_id.size() > package.size() + 1U &&
         installation_id[package.size()] == '-' &&
         installation_id[package.size() + 1U] >= '2' &&
         installation_id[package.size() + 1U] <= '9' &&
         std::ranges::all_of(installation_id.substr(package.size() + 1U),
                             [](const char value) {
                                 return value >= '0' && value <= '9';
                             }));
    if (!valid_id) {
        throw VfsError(kEinval, "sandbox installation id does not match package");
    }
    if (config.byte_quota == 0 || config.maximum_files == 0) {
        throw VfsError(kEinval, "sandbox quota must be positive");
    }
    auto impl = std::make_unique<Impl>();
    impl->directory = root / std::string(installation_id);
    impl->installation_id = std::string(installation_id);
    impl->package = std::string(package);
    impl->config = config;
    std::error_code error;
    const auto existed = std::filesystem::exists(impl->directory, error);
    if (error) throw VfsError(kEio, "cannot inspect sandbox directory");
    if (existed && require_new) {
        throw VfsError(kEexist, "sandbox installation id already exists");
    }
    bool created{};
    if (!existed) {
        created = std::filesystem::create_directory(impl->directory, error);
        if (error) throw VfsError(kEio, "cannot create sandbox directory");
        if (!created && require_new) {
            throw VfsError(kEexist, "sandbox installation id already exists");
        }
    }
    if (created) {
        try {
            for (const auto root_name : {"internal", "external", "obb", "sdcard"}) {
                std::filesystem::create_directory(impl->directory / root_name, error);
                if (error) throw VfsError(kEio, "cannot create sandbox semantic root");
            }
            impl->WriteMeta();
        } catch (...) {
            std::error_code cleanup_error;
            std::filesystem::remove_all(impl->directory, cleanup_error);
            throw;
        }
    } else {
        impl->LoadMeta();
        static constexpr std::array<std::string_view, 5> kAllowed{
            "meta.toml", "internal", "external", "obb", "sdcard"};
        for (std::filesystem::directory_iterator it(impl->directory, error), end;
             !error && it != end; it.increment(error)) {
            const auto name = it->path().filename().string();
            if (std::find(kAllowed.begin(), kAllowed.end(), name) == kAllowed.end()) {
                throw VfsError(kEinval, "sandbox contains an unknown or legacy root: " + name);
            }
        }
        if (error) throw VfsError(kEio, "cannot validate sandbox roots");
        for (const auto root_name : {"internal", "external", "obb", "sdcard"}) {
            if (!std::filesystem::is_directory(impl->directory / root_name, error) || error) {
                throw VfsError(kEinval, "sandbox semantic root is missing");
            }
        }
    }
    impl->Load();
    return std::unique_ptr<SandboxStore>(new SandboxStore(std::move(impl)));
}

std::string SandboxStore::EscapeSegment(const std::string_view segment) {
    std::string escaped;
    escaped.reserve(segment.size());
    const bool reserved = IsReservedDeviceName(segment);
    for (std::size_t index = 0; index < segment.size(); ++index) {
        const auto character = segment[index];
        const bool trailing = index + 1 == segment.size();
        const bool trailing_problem =
            trailing && (character == '.' || character == ' ');
        if (NeedsEscape(character) || trailing_problem ||
            (reserved && index == 0)) {
            AppendEscaped(escaped, character);
        } else {
            escaped.push_back(character);
        }
    }
    return escaped;
}

std::string SandboxStore::UnescapeSegment(const std::string_view segment) {
    std::string decoded;
    decoded.reserve(segment.size());
    for (std::size_t index = 0; index < segment.size(); ++index) {
        if (segment[index] != '%') {
            decoded.push_back(segment[index]);
            continue;
        }
        if (index + 2 >= segment.size()) {
            throw VfsError(kEinval, "sandbox name has a truncated escape");
        }
        const auto high = HexValue(segment[index + 1]);
        const auto low = HexValue(segment[index + 2]);
        if (high < 0 || low < 0) {
            throw VfsError(kEinval, "sandbox name has an invalid escape");
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return decoded;
}

std::vector<SandboxEntry> SandboxStore::Entries() const {
    std::vector<SandboxEntry> snapshot;
    snapshot.reserve(impl_->entries.size());
    for (const auto& [_, entry] : impl_->entries) snapshot.push_back(entry);
    return snapshot;
}

std::vector<std::byte> SandboxStore::ReadFile(
    const std::string_view guest_path) const {
    const auto found = impl_->entries.find(guest_path);
    if (found == impl_->entries.end() || found->second.is_directory ||
        found->second.is_tombstone) {
        throw VfsError(kEnoent, "sandbox file not found");
    }
    const auto host = impl_->HostPathFor(guest_path, false);
    std::ifstream input(host, std::ios::binary);
    if (!input) throw VfsError(kEio, "cannot open a sandbox file");
    std::vector<std::byte> contents(
        static_cast<std::size_t>(found->second.size));
    if (!contents.empty()) {
        input.read(reinterpret_cast<char*>(contents.data()),
                   static_cast<std::streamsize>(contents.size()));
    }
    if (!input) throw VfsError(kEio, "a sandbox file was truncated");
    return contents;
}

void SandboxStore::WriteFileAtomic(const std::string_view guest_path,
                                   const std::span<const std::byte> contents) {
    RequireNoReservedSuffix(guest_path);
    const auto existing = impl_->entries.find(guest_path);
    if (existing != impl_->entries.end() && existing->second.is_directory) {
        throw VfsError(kEinval, "sandbox path is a directory");
    }
    const auto removed = existing == impl_->entries.end()
                             ? 0
                             : existing->second.size;
    impl_->RequireQuota(contents.size(), removed,
                        existing == impl_->entries.end() ||
                            existing->second.is_tombstone);

    const auto target = impl_->HostPathFor(guest_path, false);
    impl_->CreateParentDirectories(target);
    auto temporary = target;
    temporary += std::string(kTemporarySuffix);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw VfsError(kEio, "cannot write a sandbox file");
        if (!contents.empty()) {
            output.write(reinterpret_cast<const char*>(contents.data()),
                         static_cast<std::streamsize>(contents.size()));
        }
        if (!output) throw VfsError(kEio, "cannot write a sandbox file");
    }
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        throw VfsError(kEio, "cannot replace a sandbox file");
    }
    // A write also clears any tombstone that used to shadow the base layer.
    std::filesystem::remove(impl_->HostPathFor(guest_path, true), error);

    impl_->used_bytes -= removed;
    impl_->used_bytes += contents.size();
    SandboxEntry entry;
    entry.path = std::string(guest_path);
    entry.size = contents.size();
    auto key = entry.path;
    impl_->entries.insert_or_assign(std::move(key), std::move(entry));
}

void SandboxStore::WriteTombstone(const std::string_view guest_path) {
    RequireNoReservedSuffix(guest_path);
    const auto existing = impl_->entries.find(guest_path);
    if (existing != impl_->entries.end() && existing->second.is_tombstone) {
        return;
    }
    // Deletion must remain possible at the file limit. Tombstones do not
    // consume the active-entry quota.
    impl_->RequireQuota(0, 0, false);
    const auto target = impl_->HostPathFor(guest_path, true);
    impl_->CreateParentDirectories(target);
    {
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output) throw VfsError(kEio, "cannot write a sandbox tombstone");
    }
    std::error_code error;
    // The overlay file, if any, goes away with the same call: the guest
    // asked for this path to be gone.
    std::filesystem::remove(impl_->HostPathFor(guest_path, false), error);
    if (existing != impl_->entries.end()) {
        impl_->used_bytes -= existing->second.size;
    }
    SandboxEntry entry;
    entry.path = std::string(guest_path);
    entry.is_tombstone = true;
    auto key = entry.path;
    impl_->entries.insert_or_assign(std::move(key), std::move(entry));
}

void SandboxStore::CreateDirectory(const std::string_view guest_path) {
    RequireNoReservedSuffix(guest_path);
    const auto existing = impl_->entries.find(guest_path);
    if (existing != impl_->entries.end() && !existing->second.is_tombstone) {
        if (existing->second.is_directory) return;
        throw VfsError(kEexist, "sandbox path already holds a file");
    }
    impl_->RequireQuota(0, 0,
                        existing == impl_->entries.end() ||
                            existing->second.is_tombstone);
    const auto target = impl_->HostPathFor(guest_path, false);
    std::error_code error;
    std::filesystem::create_directories(target, error);
    if (error) throw VfsError(kEio, "cannot create a sandbox directory");
    std::filesystem::remove(impl_->HostPathFor(guest_path, true), error);
    SandboxEntry entry;
    entry.path = std::string(guest_path);
    entry.is_directory = true;
    auto key = entry.path;
    impl_->entries.insert_or_assign(std::move(key), std::move(entry));
}

void SandboxStore::Remove(const std::string_view guest_path) {
    const auto found = impl_->entries.find(guest_path);
    if (found == impl_->entries.end()) {
        throw VfsError(kEnoent, "sandbox path not found");
    }
    if (found->second.is_directory) {
        auto prefix = std::string(guest_path);
        prefix.push_back('/');
        const auto child = impl_->entries.upper_bound(guest_path);
        if (child != impl_->entries.end() &&
            child->first.starts_with(prefix)) {
            throw VfsError(kEnotempty, "sandbox directory is not empty");
        }
    }
    const auto host =
        impl_->HostPathFor(guest_path, found->second.is_tombstone);
    std::error_code error;
    std::filesystem::remove(host, error);
    if (error) throw VfsError(kEio, "cannot remove a sandbox path");
    impl_->used_bytes -= found->second.size;
    impl_->entries.erase(found);
}

std::uint64_t SandboxStore::UsedBytes() const { return impl_->used_bytes; }
std::uint64_t SandboxStore::FileCount() const {
    return static_cast<std::uint64_t>(impl_->entries.size());
}
std::uint64_t SandboxStore::QuotaBytes() const {
    return impl_->config.byte_quota;
}
const std::string& SandboxStore::Package() const { return impl_->package; }
const std::string& SandboxStore::InstallationId() const {
    return impl_->installation_id;
}
const std::filesystem::path& SandboxStore::Directory() const {
    return impl_->directory;
}
std::uint64_t SandboxStore::TemporaryFilesRemoved() const {
    return impl_->temporaries_removed;
}

void SandboxStore::RecordVersionCode(const std::uint32_t version_code) {
    if (impl_->version_code == version_code) return;
    impl_->version_code = version_code;
    impl_->WriteMeta();
}

std::string SandboxStore::EnsureAndroidId(
    const std::span<const std::byte> entropy) {
    if (!impl_->android_id.empty()) return impl_->android_id;
    impl_->android_id = AndroidIdFromEntropy(entropy);
    impl_->WriteMeta();
    return impl_->android_id;
}

std::optional<std::string> SandboxStore::AndroidId() const {
    if (impl_->android_id.empty()) return std::nullopt;
    return impl_->android_id;
}

}  // namespace ogplay::runtime
