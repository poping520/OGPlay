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
  [[nodiscard]] bool MakeDirectory(std::string_view path) override;
  [[nodiscard]] bool MakeDirectories(std::string_view path) override;
  [[nodiscard]] bool CreateFile(std::string_view path) override;
  [[nodiscard]] bool Delete(std::string_view path) override;
  [[nodiscard]] bool Rename(std::string_view from,
                            std::string_view to) override;
  [[nodiscard]] std::optional<std::vector<std::byte>>
  ReadFile(std::string_view path) const override;
  void WriteFile(std::string_view path,
                 std::span<const std::byte> bytes) override;

private:
  VirtualFileSystem &file_system_;
};

} // namespace ogplay::runtime
