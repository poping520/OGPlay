#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <deque>
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
  explicit IoRuntimeError(std::string message,
                          std::int32_t error_number = 5)
      : std::runtime_error(std::move(message)), error_number_(error_number) {}
  [[nodiscard]] std::int32_t ErrorNumber() const noexcept {
    return error_number_;
  }

private:
  std::int32_t error_number_{};
};

struct IoFileInfo final {
  std::uint64_t size{};
  bool is_directory{};
  bool writable{};
};

class IoFileSystem {
public:
  enum class SeekWhence : std::uint8_t { begin, current, end };
  virtual ~IoFileSystem() = default;
  [[nodiscard]] virtual std::optional<IoFileInfo>
  Stat(std::string_view path) const = 0;
  [[nodiscard]] virtual std::optional<std::vector<std::string>>
  List(std::string_view path) const = 0;
  [[nodiscard]] virtual std::optional<std::string> WorkingDirectory() const = 0;
  virtual void MakeDirectory(std::string_view path) = 0;
  [[nodiscard]] virtual bool MakeDirectories(std::string_view path) = 0;
  [[nodiscard]] virtual bool CreateFile(std::string_view path) = 0;
  virtual void Delete(std::string_view path) = 0;
  virtual void Rename(std::string_view from, std::string_view to) = 0;
  [[nodiscard]] virtual std::optional<std::vector<std::byte>>
  ReadFile(std::string_view path) const = 0;
  virtual void WriteFile(std::string_view path,
                         std::span<const std::byte> bytes) = 0;
  [[nodiscard]] virtual std::int32_t OpenHandle(
      std::string_view path, bool read, bool write, bool create,
      bool truncate) = 0;
  [[nodiscard]] virtual IoFileInfo HandleInfo(std::int32_t handle) const = 0;
  [[nodiscard]] virtual std::size_t ReadHandle(
      std::int32_t handle, std::span<std::byte> destination) = 0;
  [[nodiscard]] virtual std::size_t WriteHandle(
      std::int32_t handle, std::span<const std::byte> source) = 0;
  [[nodiscard]] virtual std::uint64_t SeekHandle(
      std::int32_t handle, std::int64_t offset, SeekWhence whence) = 0;
  virtual void FlushHandle(std::int32_t handle) = 0;
  virtual void TruncateHandle(std::int32_t handle, std::uint64_t size) = 0;
  virtual void CloseHandle(std::int32_t handle) = 0;
};

// Per-VM file resources and character decoders, without guest reference edges.
// Java stream/protocol state lives in BootDex objects; assembly injects guest VFS.
class IoRuntime final {
public:
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
  struct DecoderState final {
    std::string encoding;
    std::vector<std::byte> encoded;
    std::deque<char16_t> pending;
    bool ended{};
  };
  DecoderState& Decoder(VmObjectRef owner) { return decoders_[owner.Value()]; }
  struct OpenFileDescription final {
    std::string path;
    std::int32_t handle{-1};
    bool readable{};
    bool writable{};
    bool append{};
    bool closed{};
  };
  struct DescriptorState final {
    DescriptorKind kind{DescriptorKind::vfs_path};
    std::string source;
    std::uint64_t base_offset{};
    bool closed{};
    std::shared_ptr<InputState> input;
    std::shared_ptr<OutputState> output;
    std::shared_ptr<OpenFileDescription> file;
  };
  void SetFileSystem(IoFileSystem *file_system) noexcept;
  [[nodiscard]] bool HasFileSystem() const noexcept;

  std::shared_ptr<InputState> SetInput(VmObjectRef owner, InputState state,
                                       bool close_underlying = true);
  void ShareInput(VmObjectRef owner, std::shared_ptr<InputState> state,
                  bool close_underlying);
  [[nodiscard]] InputState &Input(VmObjectRef owner);
  [[nodiscard]] InputState *FindInput(VmObjectRef owner) noexcept;
  void CloseInput(VmObjectRef owner);

  std::shared_ptr<OutputState> SetOutput(VmObjectRef owner, OutputState state,
                                         bool close_underlying = true);
  void ShareOutput(VmObjectRef owner, std::shared_ptr<OutputState> state,
                   bool close_underlying);
  [[nodiscard]] OutputState &Output(VmObjectRef owner);
  [[nodiscard]] OutputState *FindOutput(VmObjectRef owner) noexcept;
  void FlushOutput(VmObjectRef owner, bool close);

  void SetDescriptor(VmObjectRef owner, DescriptorState state);
  [[nodiscard]] DescriptorState &Descriptor(VmObjectRef owner);
  [[nodiscard]] DescriptorState *FindDescriptor(
      VmObjectRef owner) noexcept;
  [[nodiscard]] const DescriptorState *FindDescriptor(
      VmObjectRef owner) const noexcept;
  void SyncDescriptor(VmObjectRef owner);
  void CloseDescriptor(VmObjectRef owner) noexcept;

  [[nodiscard]] std::shared_ptr<OpenFileDescription>
  OpenFile(std::string path, bool readable, bool writable, bool append,
           bool truncate, bool create);
  void BindFileStream(VmObjectRef owner,
                      std::shared_ptr<OpenFileDescription> file,
                      bool close_underlying);
  [[nodiscard]] std::size_t FileAvailable(VmObjectRef owner) const;
  [[nodiscard]] std::size_t ReadFileStream(VmObjectRef owner,
                                           std::span<std::byte> destination);
  [[nodiscard]] std::uint64_t SkipFileStream(VmObjectRef owner,
                                             std::uint64_t count);
  [[nodiscard]] std::uint64_t FileOffset(VmObjectRef owner) const;
  [[nodiscard]] std::uint64_t FileSize(VmObjectRef owner) const;
  void SetFileOffset(VmObjectRef owner, std::uint64_t offset);
  void SetFileSize(VmObjectRef owner, std::uint64_t size);
  [[nodiscard]] std::uint64_t TransferFile(VmObjectRef source,
                                           std::uint64_t position,
                                           std::uint64_t count,
                                           VmObjectRef target);
  void WriteFileStream(VmObjectRef owner, std::span<const std::byte> source);
  void FlushFileStream(VmObjectRef owner);
  void CloseFileStream(VmObjectRef owner);

  [[nodiscard]] std::optional<IoFileInfo> Stat(std::string_view path) const;
  [[nodiscard]] std::optional<std::vector<std::string>>
  List(std::string_view path) const;
  [[nodiscard]] std::optional<std::string> WorkingDirectory() const;
  void MakeDirectory(std::string_view path);
  [[nodiscard]] bool MakeDirectories(std::string_view path);
  [[nodiscard]] bool CreateFile(std::string_view path);
  void Delete(std::string_view path);
  void Rename(std::string_view from, std::string_view to);
  [[nodiscard]] std::optional<std::vector<std::byte>>
  ReadFile(std::string_view path) const;
  void WriteFile(std::string_view path, std::span<const std::byte> bytes);

  void Sweep(VmObjectRef owner);

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

  struct FileStreamHandle final {
    std::shared_ptr<OpenFileDescription> file;
    bool closed{};
    bool close_underlying{true};
  };

  IoFileSystem *file_system_{};
  std::unordered_map<std::uint32_t, DecoderState> decoders_;
  std::unordered_map<std::uint32_t, InputHandle> inputs_;
  std::unordered_map<std::uint32_t, OutputHandle> outputs_;
  std::unordered_map<std::uint32_t, FileStreamHandle> file_streams_;
  std::unordered_map<std::uint32_t, DescriptorState> descriptors_;
};

} // namespace ogplay::runtime::dexvm
