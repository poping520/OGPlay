#pragma once

#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime {

class VirtualFileSystem;

class DexVmIoVfsAdapter final : public dexvm::IoFileSystem {
public:
  explicit DexVmIoVfsAdapter(VirtualFileSystem &file_system) noexcept;

  [[nodiscard]] std::optional<dexvm::IoFileInfo>
  Stat(std::string_view path) const override;
  [[nodiscard]] std::optional<std::vector<std::string>>
  List(std::string_view path) const override;
  [[nodiscard]] std::optional<std::string> WorkingDirectory() const override;
  void MakeDirectory(std::string_view path) override;
  [[nodiscard]] bool MakeDirectories(std::string_view path) override;
  [[nodiscard]] bool CreateFile(std::string_view path) override;
  void Delete(std::string_view path) override;
  void Rename(std::string_view from, std::string_view to) override;
  [[nodiscard]] std::optional<std::vector<std::byte>>
  ReadFile(std::string_view path) const override;
  void WriteFile(std::string_view path,
                 std::span<const std::byte> bytes) override;
  [[nodiscard]] std::int32_t OpenHandle(
      std::string_view path, bool read, bool write, bool create,
      bool truncate) override;
  [[nodiscard]] dexvm::IoFileInfo HandleInfo(
      std::int32_t handle) const override;
  [[nodiscard]] std::size_t ReadHandle(
      std::int32_t handle, std::span<std::byte> destination) override;
  [[nodiscard]] std::size_t WriteHandle(
      std::int32_t handle, std::span<const std::byte> source) override;
  [[nodiscard]] std::uint64_t SeekHandle(
      std::int32_t handle, std::int64_t offset,
      SeekWhence whence) override;
  void FlushHandle(std::int32_t handle) override;
  void TruncateHandle(std::int32_t handle, std::uint64_t size) override;
  void CloseHandle(std::int32_t handle) override;

private:
  VirtualFileSystem &file_system_;
};

} // namespace ogplay::runtime
