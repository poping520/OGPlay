#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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
  bool writable{};
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
  [[nodiscard]] virtual bool Rename(std::string_view from,
                                    std::string_view to) = 0;
  [[nodiscard]] virtual std::optional<std::vector<std::byte>>
  ReadFile(std::string_view path) const = 0;
  virtual void WriteFile(std::string_view path,
                         std::span<const std::byte> bytes) = 0;
};

// Per-VM java.io state. Core stream semantics do not depend on Android;
// Android assembly only injects the process guest VFS.
class IoRuntime final {
public:
  struct SerializedFieldDescriptor final {
    char type_code{};
    std::string name;
    std::string descriptor;
  };
  struct SerializedClassDescriptor final {
    std::string descriptor;
    std::uint32_t runtime_class{};
    std::int64_t serial_version_uid{};
    std::uint8_t flags{};
    std::vector<SerializedFieldDescriptor> fields;
    std::shared_ptr<SerializedClassDescriptor> super;
  };
  struct ObjectInputHandle final {
    VmObjectRef object{0};
    std::shared_ptr<SerializedClassDescriptor> class_descriptor;
  };
  enum class DescriptorKind : std::uint8_t {
    vfs_path,
    apk_entry,
  };
  struct InputState final {
    std::vector<std::byte> bytes;
    std::size_t cursor{};
    bool closed{};
  };
  struct OutputState final {
    std::string path;
    std::vector<std::byte> bytes;
    bool writable{true};
    bool closed{};
  };
  struct ObjectInputState final {
    std::size_t block_remaining{};
    std::optional<std::uint8_t> pushback;
    std::vector<ObjectInputHandle> handles;
  };
  struct ObjectOutputState final {
    std::unordered_map<std::uint32_t, std::uint32_t> object_handles;
    std::unordered_map<std::uint32_t, std::uint32_t> class_handles;
    std::vector<VmObjectRef> handle_objects;
    std::uint32_t next_handle{0x007e0000U};
  };
  struct DescriptorState final {
    DescriptorKind kind{DescriptorKind::vfs_path};
    std::string source;
    std::uint64_t base_offset{};
    bool closed{};
    std::shared_ptr<InputState> input;
    std::shared_ptr<OutputState> output;
  };
  void SetFileSystem(IoFileSystem *file_system) noexcept;
  [[nodiscard]] bool HasFileSystem() const noexcept;

  std::shared_ptr<InputState> SetInput(VmObjectRef owner, InputState state,
                                       bool close_underlying = true);
  void ShareInput(VmObjectRef owner, std::shared_ptr<InputState> state,
                  bool close_underlying);
  [[nodiscard]] InputState &Input(VmObjectRef owner);
  [[nodiscard]] InputState *FindInput(VmObjectRef owner) noexcept;
  void AdoptInput(VmObjectRef source, VmObjectRef target);
  [[nodiscard]] std::vector<std::byte> TakeRemainingInput(VmObjectRef owner);
  void CloseInput(VmObjectRef owner);
  void BeginObjectInput(VmObjectRef owner);
  [[nodiscard]] ObjectInputState &ObjectInput(VmObjectRef owner);
  void BeginObjectOutput(VmObjectRef owner);
  [[nodiscard]] ObjectOutputState &ObjectOutput(VmObjectRef owner);

  std::shared_ptr<OutputState> SetOutput(VmObjectRef owner, OutputState state,
                                         bool close_underlying = true);
  void ShareOutput(VmObjectRef owner, std::shared_ptr<OutputState> state,
                   bool close_underlying);
  [[nodiscard]] OutputState &Output(VmObjectRef owner);
  [[nodiscard]] OutputState *FindOutput(VmObjectRef owner) noexcept;
  void AdoptOutput(VmObjectRef source, VmObjectRef target);
  void FlushOutput(VmObjectRef owner, bool close);

  void SetDescriptor(VmObjectRef owner, DescriptorState state);
  [[nodiscard]] DescriptorState &Descriptor(VmObjectRef owner);
  [[nodiscard]] DescriptorState *FindDescriptor(
      VmObjectRef owner) noexcept;
  [[nodiscard]] const DescriptorState *FindDescriptor(
      VmObjectRef owner) const noexcept;
  void SyncDescriptor(VmObjectRef owner);
  void CloseDescriptor(VmObjectRef owner) noexcept;

  [[nodiscard]] std::optional<IoFileInfo> Stat(std::string_view path) const;
  [[nodiscard]] std::optional<std::vector<std::string>>
  List(std::string_view path) const;
  [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
  [[nodiscard]] bool MakeDirectory(std::string_view path);
  [[nodiscard]] bool MakeDirectories(std::string_view path);
  [[nodiscard]] bool CreateFile(std::string_view path);
  [[nodiscard]] bool Delete(std::string_view path);
  [[nodiscard]] bool Rename(std::string_view from, std::string_view to);
  [[nodiscard]] std::optional<std::vector<std::byte>>
  ReadFile(std::string_view path) const;
  void WriteFile(std::string_view path, std::span<const std::byte> bytes);

  void Sweep(VmObjectRef owner);
  void Trace(VmObjectRef owner,
             const std::function<void(VmObjectRef)> &visitor) const;

private:
  struct InputHandle final {
    std::shared_ptr<InputState> state;
    bool closed{};
    bool close_underlying{true};
  };

  struct OutputHandle final {
    std::shared_ptr<OutputState> state;
    bool closed{};
    bool close_underlying{true};
  };

  IoFileSystem *file_system_{};
  std::unordered_map<std::uint32_t, InputHandle> inputs_;
  std::unordered_map<std::uint32_t, ObjectInputState> object_inputs_;
  std::unordered_map<std::uint32_t, ObjectOutputState> object_outputs_;
  std::unordered_map<std::uint32_t, OutputHandle> outputs_;
  std::unordered_map<std::uint32_t, DescriptorState> descriptors_;
};

} // namespace ogplay::runtime::dexvm
