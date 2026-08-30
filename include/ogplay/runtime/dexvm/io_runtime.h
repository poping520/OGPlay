#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "ogplay/runtime/dexvm/dexvm_types.h"

namespace ogplay::runtime::dexvm {

class IoRuntimeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct IoFileInfo final {
  std::uint64_t size{};
  bool is_directory{};
};

class IoFileSystem {
public:
  virtual ~IoFileSystem() = default;
  [[nodiscard]] virtual std::optional<IoFileInfo>
  Stat(std::string_view path) const = 0;
  [[nodiscard]] virtual std::optional<std::vector<std::string>>
  List(std::string_view path) const = 0;
  [[nodiscard]] virtual std::optional<std::string> WorkingDirectory() const = 0;
  [[nodiscard]] virtual bool MakeDirectory(std::string_view path) = 0;
  [[nodiscard]] virtual bool MakeDirectories(std::string_view path) = 0;
  [[nodiscard]] virtual bool CreateFile(std::string_view path) = 0;
  [[nodiscard]] virtual bool Delete(std::string_view path) = 0;
  [[nodiscard]] virtual std::optional<std::vector<std::byte>>
  ReadFile(std::string_view path) const = 0;
  virtual void WriteFile(std::string_view path,
                         std::span<const std::byte> bytes) = 0;
};

// Per-VM java.io state. Core stream semantics do not depend on Android;
// Android assembly only injects the process guest VFS.
class IoRuntime final {
public:
  enum class DescriptorKind : std::uint8_t {
    vfs_path,
    apk_entry,
  };
  struct DescriptorState final {
    DescriptorKind kind{DescriptorKind::vfs_path};
    std::string source;
    std::uint64_t base_offset{};
    bool closed{};
  };
  struct InputState final {
    std::vector<std::byte> bytes;
    std::size_t cursor{};
    bool closed{};
  };
  struct OutputState final {
    std::string path;
    std::vector<std::byte> bytes;
    bool closed{};
  };
  void SetFileSystem(IoFileSystem *file_system) noexcept;

  void SetInput(VmObjectRef owner, InputState state);
  [[nodiscard]] InputState &Input(VmObjectRef owner);
  [[nodiscard]] InputState *FindInput(VmObjectRef owner) noexcept;
  void AdoptInput(VmObjectRef source, VmObjectRef target);
  [[nodiscard]] std::vector<std::byte> TakeRemainingInput(VmObjectRef owner);
  void CloseInput(VmObjectRef owner);

  void SetOutput(VmObjectRef owner, OutputState state);
  [[nodiscard]] OutputState &Output(VmObjectRef owner);
  [[nodiscard]] OutputState *FindOutput(VmObjectRef owner) noexcept;
  void AdoptOutput(VmObjectRef source, VmObjectRef target);
  void FlushOutput(VmObjectRef owner, bool close);

  void SetDescriptor(VmObjectRef owner, DescriptorState state);
  [[nodiscard]] DescriptorState &Descriptor(VmObjectRef owner);
  [[nodiscard]] const DescriptorState *FindDescriptor(
      VmObjectRef owner) const noexcept;
  void CloseDescriptor(VmObjectRef owner) noexcept;

  [[nodiscard]] std::optional<IoFileInfo> Stat(std::string_view path) const;
  [[nodiscard]] std::optional<std::vector<std::string>>
  List(std::string_view path) const;
  [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
  [[nodiscard]] bool MakeDirectory(std::string_view path);
  [[nodiscard]] bool MakeDirectories(std::string_view path);
  [[nodiscard]] bool CreateFile(std::string_view path);
  [[nodiscard]] bool Delete(std::string_view path);
  [[nodiscard]] std::optional<std::vector<std::byte>>
  ReadFile(std::string_view path) const;
  void WriteFile(std::string_view path, std::span<const std::byte> bytes);

  void Sweep(VmObjectRef owner);

private:
  IoFileSystem *file_system_{};
  std::unordered_map<std::uint32_t, InputState> inputs_;
  std::unordered_map<std::uint32_t, OutputState> outputs_;
  std::unordered_map<std::uint32_t, DescriptorState> descriptors_;
};

} // namespace ogplay::runtime::dexvm
