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

  [[nodiscard]] std::optional<IoFileInfo> Stat(std::string_view path) const;
  [[nodiscard]] std::optional<std::vector<std::string>>
  List(std::string_view path) const;
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
};

} // namespace ogplay::runtime::dexvm
