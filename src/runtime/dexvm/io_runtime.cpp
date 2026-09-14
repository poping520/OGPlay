#include "ogplay/runtime/dexvm/io_runtime.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace ogplay::runtime::dexvm {

void IoRuntime::SetFileSystem(IoFileSystem *file_system) noexcept {
  file_system_ = file_system;
}

bool IoRuntime::HasFileSystem() const noexcept {
  return file_system_ != nullptr;
}

std::shared_ptr<IoRuntime::InputState>
IoRuntime::SetInput(const VmObjectRef owner, InputState state,
                    const bool close_underlying) {
  auto shared = std::make_shared<InputState>(std::move(state));
  inputs_[owner.Value()] = {shared, false, close_underlying};
  return shared;
}

void IoRuntime::ShareInput(const VmObjectRef owner,
                           std::shared_ptr<InputState> state,
                           const bool close_underlying) {
  if (state == nullptr) {
    throw IoRuntimeError("input descriptor has no state");
  }
  inputs_[owner.Value()] = {std::move(state), false, close_underlying};
}

IoRuntime::InputState &IoRuntime::Input(const VmObjectRef owner) {
  const auto found = inputs_.find(owner.Value());
  if (found == inputs_.end() || found->second.closed ||
      found->second.state->closed) {
    throw IoRuntimeError("input stream is closed or was never opened");
  }
  return *found->second.state;
}

IoRuntime::InputState *IoRuntime::FindInput(const VmObjectRef owner) noexcept {
  const auto found = inputs_.find(owner.Value());
  return found == inputs_.end() ? nullptr : found->second.state.get();
}

void IoRuntime::CloseInput(const VmObjectRef owner) {
  const auto found = inputs_.find(owner.Value());
  if (found == inputs_.end() || found->second.closed)
    return;
  found->second.closed = true;
  if (found->second.close_underlying)
    found->second.state->closed = true;
}

std::shared_ptr<IoRuntime::OutputState>
IoRuntime::SetOutput(const VmObjectRef owner, OutputState state,
                     const bool close_underlying) {
  auto shared = std::make_shared<OutputState>(std::move(state));
  outputs_[owner.Value()] = {shared, false, close_underlying};
  return shared;
}

void IoRuntime::ShareOutput(const VmObjectRef owner,
                            std::shared_ptr<OutputState> state,
                            const bool close_underlying) {
  if (state == nullptr) {
    throw IoRuntimeError("output descriptor has no state");
  }
  outputs_[owner.Value()] = {std::move(state), false, close_underlying};
}

IoRuntime::OutputState &IoRuntime::Output(const VmObjectRef owner) {
  const auto found = outputs_.find(owner.Value());
  if (found == outputs_.end() || found->second.closed ||
      found->second.state->closed) {
    throw IoRuntimeError("output stream is closed or was never opened");
  }
  if (!found->second.state->writable) {
    throw IoRuntimeError("file descriptor is not writable");
  }
  return *found->second.state;
}

IoRuntime::OutputState *
IoRuntime::FindOutput(const VmObjectRef owner) noexcept {
  const auto found = outputs_.find(owner.Value());
  return found == outputs_.end() ? nullptr : found->second.state.get();
}

void IoRuntime::FlushOutput(const VmObjectRef owner, const bool close) {
  const auto found = outputs_.find(owner.Value());
  if (found == outputs_.end() || found->second.closed ||
      found->second.state->closed)
    return;
  auto &handle = found->second;
  auto &state = *handle.state;
  if (!state.writable) {
    if (close) handle.closed = true;
    return;
  }
  if (!state.path.empty())
    WriteFile(state.path, state.bytes);
  if (close) {
    handle.closed = true;
    if (handle.close_underlying) state.closed = true;
  }
}

void IoRuntime::SetDescriptor(const VmObjectRef owner,
                              DescriptorState state) {
  descriptors_[owner.Value()] = std::move(state);
}

IoRuntime::DescriptorState &IoRuntime::Descriptor(const VmObjectRef owner) {
  const auto found = descriptors_.find(owner.Value());
  if (found == descriptors_.end() || found->second.closed) {
    throw IoRuntimeError("file descriptor is closed or was never opened");
  }
  return found->second;
}

const IoRuntime::DescriptorState *
IoRuntime::FindDescriptor(const VmObjectRef owner) const noexcept {
  const auto found = descriptors_.find(owner.Value());
  return found == descriptors_.end() ? nullptr : &found->second;
}

IoRuntime::DescriptorState *
IoRuntime::FindDescriptor(const VmObjectRef owner) noexcept {
  const auto found = descriptors_.find(owner.Value());
  return found == descriptors_.end() ? nullptr : &found->second;
}

void IoRuntime::SyncDescriptor(const VmObjectRef owner) {
  auto &descriptor = Descriptor(owner);
  if (descriptor.file != nullptr) {
    if (!descriptor.file->writable || descriptor.file->closed) {
      throw IoRuntimeError("file descriptor is not writable");
    }
    if (file_system_ == nullptr)
      throw IoRuntimeError("guest filesystem is unavailable");
    file_system_->FlushHandle(descriptor.file->handle);
    return;
  }
  if (descriptor.output == nullptr || descriptor.output->closed ||
      !descriptor.output->writable) {
    throw IoRuntimeError("file descriptor is not writable");
  }
  if (descriptor.output->path.empty()) {
    throw IoRuntimeError("file descriptor has no VFS path");
  }
  WriteFile(descriptor.output->path, descriptor.output->bytes);
}

void IoRuntime::CloseDescriptor(const VmObjectRef owner) noexcept {
  const auto found = descriptors_.find(owner.Value());
  if (found != descriptors_.end()) {
    found->second.closed = true;
    if (found->second.input != nullptr) found->second.input->closed = true;
    if (found->second.output != nullptr) found->second.output->closed = true;
    if (found->second.file != nullptr && !found->second.file->closed) {
      try {
        if (file_system_ != nullptr)
          file_system_->CloseHandle(found->second.file->handle);
      } catch (const IoRuntimeError&) {
      }
      found->second.file->closed = true;
    }
  }
}

std::shared_ptr<IoRuntime::OpenFileDescription> IoRuntime::OpenFile(
    std::string path, const bool readable, const bool writable,
    const bool append, const bool truncate, const bool create) {
  if (!readable && !writable) {
    throw IoRuntimeError("file descriptor has no access mode");
  }
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  auto file = std::make_shared<OpenFileDescription>();
  file->path = std::move(path);
  file->readable = readable;
  file->writable = writable;
  file->append = append;
  file->handle = file_system_->OpenHandle(file->path, readable, writable,
                                          create, truncate);
  try {
    if (append)
      static_cast<void>(file_system_->SeekHandle(
          file->handle, 0, IoFileSystem::SeekWhence::end));
  } catch (...) {
    try {
      file_system_->CloseHandle(file->handle);
    } catch (const IoRuntimeError&) {
    }
    throw;
  }
  return file;
}

void IoRuntime::BindFileStream(
    const VmObjectRef owner, std::shared_ptr<OpenFileDescription> file,
    const bool close_underlying) {
  if (file == nullptr) throw IoRuntimeError("file descriptor has no state");
  file_streams_[owner.Value()] = {
      std::move(file), false, close_underlying};
}

namespace {
IoRuntime::OpenFileDescription& RequireFile(
    auto& streams,
    const VmObjectRef owner) {
  const auto found = streams.find(owner.Value());
  if (found == streams.end() || found->second.closed ||
      found->second.file->closed) {
    throw IoRuntimeError("file stream is closed or was never opened");
  }
  return *found->second.file;
}
}  // namespace

std::size_t IoRuntime::FileAvailable(const VmObjectRef owner) const {
  const auto found = file_streams_.find(owner.Value());
  if (found == file_streams_.end() || found->second.closed ||
      found->second.file->closed || !found->second.file->readable) {
    throw IoRuntimeError("file stream is closed or not readable");
  }
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  const auto size = file_system_->HandleInfo(found->second.file->handle).size;
  const auto offset = file_system_->SeekHandle(
      found->second.file->handle, 0, IoFileSystem::SeekWhence::current);
  return static_cast<std::size_t>(size > offset ? size - offset : 0U);
}

std::size_t IoRuntime::ReadFileStream(
    const VmObjectRef owner, const std::span<std::byte> destination) {
  auto& file = RequireFile(file_streams_, owner);
  if (!file.readable) throw IoRuntimeError("file descriptor is not readable");
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  return file_system_->ReadHandle(file.handle, destination);
}

std::uint64_t IoRuntime::SkipFileStream(const VmObjectRef owner,
                                        const std::uint64_t count) {
  auto& file = RequireFile(file_streams_, owner);
  if (!file.readable) throw IoRuntimeError("file descriptor is not readable");
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  const auto before = file_system_->SeekHandle(
      file.handle, 0, IoFileSystem::SeekWhence::current);
  const auto size = file_system_->HandleInfo(file.handle).size;
  const auto available = size > before ? size - before : 0U;
  const auto amount = std::min(count, available);
  static_cast<void>(file_system_->SeekHandle(
      file.handle, static_cast<std::int64_t>(amount),
      IoFileSystem::SeekWhence::current));
  return amount;
}

std::uint64_t IoRuntime::FileOffset(const VmObjectRef owner) const {
  const auto found = file_streams_.find(owner.Value());
  if (found == file_streams_.end() || found->second.closed ||
      found->second.file->closed || file_system_ == nullptr) {
    throw IoRuntimeError("file stream is closed or was never opened");
  }
  return file_system_->SeekHandle(found->second.file->handle, 0,
                                  IoFileSystem::SeekWhence::current);
}

std::uint64_t IoRuntime::FileSize(const VmObjectRef owner) const {
  const auto& file = RequireFile(file_streams_, owner);
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  return file_system_->HandleInfo(file.handle).size;
}

void IoRuntime::SetFileOffset(const VmObjectRef owner,
                              const std::uint64_t offset) {
  auto& file = RequireFile(file_streams_, owner);
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    throw IoRuntimeError("file offset is not representable");
  static_cast<void>(file_system_->SeekHandle(
      file.handle, static_cast<std::int64_t>(offset),
      IoFileSystem::SeekWhence::begin));
}

void IoRuntime::SetFileSize(const VmObjectRef owner, const std::uint64_t size) {
  auto& file = RequireFile(file_streams_, owner);
  if (!file.writable) throw IoRuntimeError("file descriptor is not writable");
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  file_system_->TruncateHandle(file.handle, size);
}

std::uint64_t IoRuntime::TransferFile(const VmObjectRef source,
                                      const std::uint64_t position,
                                      const std::uint64_t count,
                                      const VmObjectRef target) {
  auto& source_file = RequireFile(file_streams_, source);
  auto& target_file = RequireFile(file_streams_, target);
  if (!source_file.readable)
    throw IoRuntimeError("source file descriptor is not readable");
  if (!target_file.writable)
    throw IoRuntimeError("target file descriptor is not writable");
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  if (position > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    throw IoRuntimeError("file position is not representable");
  const auto original = file_system_->SeekHandle(
      source_file.handle, 0, IoFileSystem::SeekWhence::current);
  const auto restore = [&] {
    static_cast<void>(file_system_->SeekHandle(
        source_file.handle, static_cast<std::int64_t>(original),
        IoFileSystem::SeekWhence::begin));
  };
  try {
    const auto size = file_system_->HandleInfo(source_file.handle).size;
    if (position >= size || count == 0) return 0;
    static_cast<void>(file_system_->SeekHandle(
        source_file.handle, static_cast<std::int64_t>(position),
        IoFileSystem::SeekWhence::begin));
    auto remaining = std::min(count, size - position);
    std::uint64_t transferred{};
    std::array<std::byte, 64U * 1024U> buffer{};
    while (remaining != 0) {
      const auto requested = static_cast<std::size_t>(
          std::min<std::uint64_t>(remaining, buffer.size()));
      const auto read = file_system_->ReadHandle(
          source_file.handle, std::span(buffer).first(requested));
      if (read == 0) break;
      if (target_file.append)
        static_cast<void>(file_system_->SeekHandle(
            target_file.handle, 0, IoFileSystem::SeekWhence::end));
      std::size_t written{};
      while (written < read) {
        const auto amount = file_system_->WriteHandle(
            target_file.handle,
            std::span<const std::byte>(buffer).subspan(written, read - written));
        if (amount == 0) throw IoRuntimeError("file transfer made no progress");
        written += amount;
      }
      transferred += read;
      remaining -= read;
    }
    restore();
    return transferred;
  } catch (...) {
    try { restore(); } catch (const IoRuntimeError&) {}
    throw;
  }
}

void IoRuntime::WriteFileStream(
    const VmObjectRef owner, const std::span<const std::byte> source) {
  auto& file = RequireFile(file_streams_, owner);
  if (!file.writable) throw IoRuntimeError("file descriptor is not writable");
  if (file_system_ == nullptr)
    throw IoRuntimeError("guest filesystem is unavailable");
  if (file.append)
    static_cast<void>(file_system_->SeekHandle(
        file.handle, 0, IoFileSystem::SeekWhence::end));
  std::size_t cursor = 0;
  while (cursor < source.size()) {
    const auto amount = file_system_->WriteHandle(
        file.handle, source.subspan(cursor));
    if (amount == 0) throw IoRuntimeError("file write made no progress");
    cursor += amount;
  }
}

void IoRuntime::FlushFileStream(const VmObjectRef owner) {
  auto& file = RequireFile(file_streams_, owner);
  if (file.writable) file_system_->FlushHandle(file.handle);
}

void IoRuntime::CloseFileStream(const VmObjectRef owner) {
  const auto found = file_streams_.find(owner.Value());
  if (found == file_streams_.end() || found->second.closed) return;
  found->second.closed = true;
  if (found->second.close_underlying && !found->second.file->closed) {
    if (file_system_ == nullptr)
      throw IoRuntimeError("guest filesystem is unavailable");
    file_system_->CloseHandle(found->second.file->handle);
    found->second.file->closed = true;
  }
}

std::optional<IoFileInfo> IoRuntime::Stat(const std::string_view path) const {
  return file_system_ != nullptr ? file_system_->Stat(path) : std::nullopt;
}

std::optional<std::vector<std::string>>
IoRuntime::List(const std::string_view path) const {
  return file_system_ != nullptr ? file_system_->List(path) : std::nullopt;
}

std::optional<std::string> IoRuntime::WorkingDirectory() const {
  return file_system_ != nullptr ? file_system_->WorkingDirectory()
                                 : std::nullopt;
}

void IoRuntime::MakeDirectory(const std::string_view path) {
  if (file_system_ == nullptr) {
    throw IoRuntimeError("guest filesystem is unavailable");
  }
  file_system_->MakeDirectory(path);
}

bool IoRuntime::MakeDirectories(const std::string_view path) {
  return file_system_ != nullptr && file_system_->MakeDirectories(path);
}

bool IoRuntime::CreateFile(const std::string_view path) {
  if (file_system_ == nullptr) {
    throw IoRuntimeError("guest filesystem is unavailable");
  }
  return file_system_->CreateFile(path);
}

void IoRuntime::Delete(const std::string_view path) {
  if (file_system_ == nullptr) {
    throw IoRuntimeError("guest filesystem is unavailable");
  }
  file_system_->Delete(path);
}

void IoRuntime::Rename(const std::string_view from,
                       const std::string_view to) {
  if (file_system_ == nullptr) {
    throw IoRuntimeError("guest filesystem is unavailable");
  }
  file_system_->Rename(from, to);
}

std::optional<std::vector<std::byte>>
IoRuntime::ReadFile(const std::string_view path) const {
  return file_system_ != nullptr ? file_system_->ReadFile(path) : std::nullopt;
}

void IoRuntime::WriteFile(const std::string_view path,
                          const std::span<const std::byte> bytes) {
  if (file_system_ == nullptr) {
    throw IoRuntimeError("guest filesystem is unavailable");
  }
  file_system_->WriteFile(path, bytes);
}

void IoRuntime::Sweep(const VmObjectRef owner) {
  decoders_.erase(owner.Value());
  inputs_.erase(owner.Value());
  outputs_.erase(owner.Value());
  file_streams_.erase(owner.Value());
  descriptors_.erase(owner.Value());
}

} // namespace ogplay::runtime::dexvm
