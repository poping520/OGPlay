#include "ogplay/runtime/integration/dexvm_io_vfs.h"

#include <utility>

#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {
namespace {

void CloseIfOpen(VirtualFileSystem &file_system,
                 std::optional<std::int32_t> &descriptor) noexcept {
  if (!descriptor.has_value())
    return;
  try {
    file_system.Close(*descriptor);
  } catch (const VfsError &) {
  }
  descriptor.reset();
}

} // namespace

DexVmIoVfsAdapter::DexVmIoVfsAdapter(VirtualFileSystem &file_system) noexcept
    : file_system_(file_system) {}

std::optional<dexvm::IoFileInfo>
DexVmIoVfsAdapter::Stat(const std::string_view path) const {
  try {
    const auto info = file_system_.Stat(path);
    return dexvm::IoFileInfo{info.size, info.is_directory, info.writable};
  } catch (const VfsError &) {
    return std::nullopt;
  }
}

std::optional<std::vector<std::string>>
DexVmIoVfsAdapter::List(const std::string_view path) const {
  const auto info = Stat(path);
  if (!info.has_value() || !info->is_directory)
    return std::nullopt;
  try {
    std::vector<std::string> names;
    for (auto &entry : file_system_.ListDirectory(path)) {
      names.push_back(std::move(entry.name));
    }
    return names;
  } catch (const VfsError &) {
    return std::nullopt;
  }
}

std::optional<std::string> DexVmIoVfsAdapter::WorkingDirectory() const {
  return file_system_.WorkingDirectory();
}

void DexVmIoVfsAdapter::MakeDirectory(const std::string_view path) {
  try {
    file_system_.CreateDirectory(path);
  } catch (const VfsError &error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

bool DexVmIoVfsAdapter::MakeDirectories(const std::string_view path_view) {
  if (Stat(path_view).has_value())
    return false;
  const std::string path(path_view);
  bool created = false;
  for (std::size_t cursor = 1; cursor <= path.size(); ++cursor) {
    if (cursor != path.size() && path[cursor] != '/')
      continue;
    const auto prefix = path.substr(0, cursor);
    if (Stat(prefix).has_value())
      continue;
    try {
      file_system_.CreateDirectory(prefix);
      created = true;
    } catch (const VfsError &) {
      return false;
    }
  }
  return created;
}

bool DexVmIoVfsAdapter::CreateFile(const std::string_view path) {
  if (Stat(path).has_value())
    return false;
  WriteFile(path, {});
  return true;
}

void DexVmIoVfsAdapter::Delete(const std::string_view path) {
  try {
    const auto info = file_system_.Stat(path);
    if (info.is_directory) {
      file_system_.RemoveDirectory(path);
    } else {
      file_system_.RemoveFile(path);
    }
  } catch (const VfsError &error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

void DexVmIoVfsAdapter::Rename(const std::string_view from,
                               const std::string_view to) {
  try {
    file_system_.Rename(from, to);
  } catch (const VfsError &error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

std::optional<std::vector<std::byte>>
DexVmIoVfsAdapter::ReadFile(const std::string_view path) const {
  std::optional<std::int32_t> descriptor;
  try {
    const auto info = file_system_.Stat(path);
    descriptor = file_system_.Open(path, {.read = true});
    std::vector<std::byte> bytes(info.size);
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
      const auto count =
          file_system_.Read(*descriptor, std::span(bytes).subspan(cursor));
      if (count == 0)
        break;
      cursor += count;
    }
    file_system_.Close(*descriptor);
    descriptor.reset();
    bytes.resize(cursor);
    return bytes;
  } catch (const VfsError &) {
    CloseIfOpen(file_system_, descriptor);
    return std::nullopt;
  }
}

void DexVmIoVfsAdapter::WriteFile(const std::string_view path,
                                  const std::span<const std::byte> bytes) {
  std::optional<std::int32_t> descriptor;
  try {
    descriptor = file_system_.Open(
        path, {.write = true, .create = true, .truncate = true});
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
      cursor += file_system_.Write(*descriptor, bytes.subspan(cursor));
    }
    file_system_.Close(*descriptor);
    descriptor.reset();
  } catch (const VfsError &error) {
    CloseIfOpen(file_system_, descriptor);
    throw dexvm::IoRuntimeError("cannot write " + std::string(path) + ": " +
                                    error.what(),
                                error.ErrorNumber());
  }
}

std::int32_t DexVmIoVfsAdapter::OpenHandle(
    const std::string_view path, const bool read, const bool write,
    const bool create, const bool truncate) {
  try {
    return file_system_.Open(
        path, {.read = read, .write = write, .create = create,
               .truncate = truncate});
  } catch (const VfsError& error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

dexvm::IoFileInfo DexVmIoVfsAdapter::HandleInfo(
    const std::int32_t handle) const {
  try {
    const auto info = file_system_.DescriptorInfo(handle);
    return {info.size, info.is_directory, info.writable};
  } catch (const VfsError& error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

std::size_t DexVmIoVfsAdapter::ReadHandle(
    const std::int32_t handle, const std::span<std::byte> destination) {
  try {
    return file_system_.Read(handle, destination);
  } catch (const VfsError& error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

std::size_t DexVmIoVfsAdapter::WriteHandle(
    const std::int32_t handle, const std::span<const std::byte> source) {
  try {
    return file_system_.Write(handle, source);
  } catch (const VfsError& error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

std::uint64_t DexVmIoVfsAdapter::SeekHandle(
    const std::int32_t handle, const std::int64_t offset,
    const SeekWhence whence) {
  const auto translated =
      whence == SeekWhence::begin
          ? VfsSeekWhence::begin
          : whence == SeekWhence::current ? VfsSeekWhence::current
                                           : VfsSeekWhence::end;
  try {
    return file_system_.Seek(handle, offset, translated);
  } catch (const VfsError& error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

void DexVmIoVfsAdapter::FlushHandle(const std::int32_t handle) {
  try {
    file_system_.Flush(handle);
  } catch (const VfsError& error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

void DexVmIoVfsAdapter::TruncateHandle(const std::int32_t handle,
                                       const std::uint64_t size) {
  try {
    file_system_.Truncate(handle, size);
  } catch (const VfsError& error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

void DexVmIoVfsAdapter::CloseHandle(const std::int32_t handle) {
  try {
    file_system_.Close(handle);
  } catch (const VfsError& error) {
    throw dexvm::IoRuntimeError(error.what(), error.ErrorNumber());
  }
}

} // namespace ogplay::runtime
