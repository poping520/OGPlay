#pragma once

#include "ogplay/app/export.h"
#include "ogplay/app/library.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::app {

// One regular file of an import source: a '/'-separated relative UTF-8 path without empty, "." or ".." parts.
struct ImportFile final {
    std::string path;
    std::uint64_t size{};
};

struct ImportExtraction final {
    std::size_t index{};                // into ImportSource::Files()
    std::filesystem::path destination;  // must not exist; parent directories are created
};

// Files that can be brought into the library without copying them anywhere first: a game archive, or a host's
// directory handle (Android SAF). Extract reports bytes to progress and stops with import_cancelled when asked.
class OGPLAYCORE_API ImportSource {
public:
    virtual ~ImportSource();
    [[nodiscard]] virtual const std::vector<ImportFile>& Files() const = 0;
    virtual void Extract(std::span<const ImportExtraction> files, ImportProgress* progress) = 0;
};

enum class ImportSourceKind : std::uint8_t { apk, archive, unknown };

// From the file head, never the name: a ZIP with a root AndroidManifest.xml is an APK, any other ZIP (also ZIP64)
// or a RAR or 7z is a game archive.
[[nodiscard]] OGPLAYCORE_API ImportSourceKind DetectImportSource(const std::filesystem::path& path);
// Lists a ZIP, RAR or 7z archive. Encrypted or multi-volume archives and unsafe member paths are refused.
[[nodiscard]] OGPLAYCORE_API std::unique_ptr<ImportSource> OpenArchive(const std::filesystem::path& path);

// What a game archive holds. Data members are mapped to absolute guest paths under /sdcard; members of no known
// use are grouped by their top-level name as extras, left out unless chosen (then mounted at /sdcard/<path>).
struct ArchiveData final {
    std::size_t index{};
    std::string guest_path;
};

struct ArchiveExtra final {
    std::string name;
    std::vector<ArchiveData> members;
    std::uint64_t bytes{};
};

struct ArchivePlan final {
    std::vector<std::size_t> apks;  // APK candidates, preferred first
    std::vector<ArchiveData> data;
    std::vector<ArchiveExtra> extras;
};

// package is the chosen APK's, or empty before it is known; it only decides which top-level directory
// names count as package directories when they do not look like one on their own.
[[nodiscard]] OGPLAYCORE_API ArchivePlan PlanArchive(const std::vector<ImportFile>& files, std::string_view package);
// Mount for the planned data plus the chosen extras: their closest common directory, cut back to
// /sdcard/Android/(obb|data|media)/<name> when deeper; "/sdcard" once an extra is chosen; empty without data.
[[nodiscard]] OGPLAYCORE_API std::string ArchiveMount(const ArchivePlan& plan, std::span<const std::string> extras);
// Writes the planned data and chosen extras under destination, laid out relative to ArchiveMount, and returns
// that mount. Unknown extra names are refused.
OGPLAYCORE_API std::string ExtractArchiveData(ImportSource& source, const ArchivePlan& plan,
                                              std::span<const std::string> extras,
                                              const std::filesystem::path& destination, ImportProgress* progress);

// A picked data directory planned the way an archive's data is. files are its regular files relative to it; base
// is the directory's own place under /sdcard (GuessDataMount, "/sdcard" for an sdcard copy). Only the package's own
// directories below /sdcard/Android/(data|obb|media) are data: the APK package's, or else the one package name
// present there (package then names it). Everything else but APKs and top-level readmes is an extra, named by its
// shallowest part holding no data. With no such directory (or several foreign ones) package is empty and every
// file is data where it lies, as a plain directory copy. Guest spellings of Android/<kind> are canonical.
struct DataDirectoryPlan final {
    ArchivePlan plan;
    std::string package;
};

[[nodiscard]] OGPLAYCORE_API DataDirectoryPlan PlanDataDirectory(const std::vector<ImportFile>& files,
                                                                 std::string_view base, std::string_view package);

// A host data directory as an import source: its regular files, extracted by clone or copy. A directory that
// overlaps the library, or holds symbolic links or special files, is refused.
[[nodiscard]] OGPLAYCORE_API std::unique_ptr<ImportSource> OpenDataDirectory(const std::filesystem::path& directory,
                                                                             const std::filesystem::path& library_root);

// A private directory under the library root (same volume, so its contents can be moved into an entry) for
// one import's snapshot and extracted data: <root>/import-staging/work-N, alive while its OS lock is held.
// Removed with everything left in it when this object goes; ones left by a killed process (and the
// .gui-import-* directories of older versions) are swept when the next one is made.
class OGPLAYCORE_API ImportWorkDirectory final {
public:
    explicit ImportWorkDirectory(const std::filesystem::path& library_root);
    ~ImportWorkDirectory();
    ImportWorkDirectory(const ImportWorkDirectory&) = delete;
    ImportWorkDirectory& operator=(const ImportWorkDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

private:
    struct Lock;
    std::unique_ptr<Lock> lock_;
    std::filesystem::path path_;
};

// A member path as an import path: '\' separates directories (Windows-made packs), "." parts and leading '/'
// are dropped. Nullopt for a path that would leave the destination ("..", a drive or control characters);
// an empty result is the archive root (a directory entry).
[[nodiscard]] OGPLAYCORE_API std::optional<std::string> NormalizeArchivePath(std::string_view utf8);

// Fills destination (which must not exist; parents are created) with size bytes pulled from read, counting them
// into progress and stopping with import_cancelled when asked. read returns how many bytes it wrote; 0 before
// size bytes means the source ended early and fails the import.
OGPLAYCORE_API void WriteImportFile(const std::filesystem::path& destination, std::uint64_t size,
                                    const std::function<std::size_t(std::span<std::byte>)>& read,
                                    ImportProgress* progress);

// A picked file's size and a copy of it, read the way archives are (an Android SAF pick at /proc/self/fd/<n>
// can only be read through the held descriptor, not reopened by path).
[[nodiscard]] OGPLAYCORE_API std::uint64_t ImportFileSize(const std::filesystem::path& source);
OGPLAYCORE_API void CopyImportFile(const std::filesystem::path& source, const std::filesystem::path& destination);

// An import path (UTF-8, '/'-separated) as a host relative path.
[[nodiscard]] OGPLAYCORE_API std::filesystem::path ImportFilePath(std::string_view utf8);

// Legacy archive names: UTF-8 when valid, otherwise GBK (CP936), the encoding of the Chinese game packs these
// archives come from. Undecodable bytes become U+FFFD.
[[nodiscard]] OGPLAYCORE_API std::string DecodeArchiveName(std::string_view bytes);

}  // namespace ogplay::app
