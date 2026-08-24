#include "ogplay/runtime/dexvm/io_runtime.h"

#include <utility>

namespace ogplay::runtime::dexvm {

void IoRuntime::SetFileSystem(IoFileSystem *file_system) noexcept {
  file_system_ = file_system;
}

void IoRuntime::SetInput(const VmObjectRef owner, InputState state) {
  inputs_[owner.Value()] = std::move(state);
}

IoRuntime::InputState &IoRuntime::Input(const VmObjectRef owner) {
  const auto found = inputs_.find(owner.Value());
  if (found == inputs_.end() || found->second.closed) {
    throw IoRuntimeError("input stream is closed or was never opened");
  }
  return found->second;
}

IoRuntime::InputState *IoRuntime::FindInput(const VmObjectRef owner) noexcept {
  const auto found = inputs_.find(owner.Value());
  return found == inputs_.end() ? nullptr : &found->second;
}

void IoRuntime::AdoptInput(const VmObjectRef source, const VmObjectRef target) {
  auto &state = Input(source);
  inputs_[target.Value()] = std::move(state);
  inputs_.erase(source.Value());
}

std::vector<std::byte> IoRuntime::TakeRemainingInput(const VmObjectRef owner) {
  auto &state = Input(owner);
  std::vector<std::byte> result(state.bytes.begin() +
                                    static_cast<std::ptrdiff_t>(state.cursor),
                                state.bytes.end());
  inputs_.erase(owner.Value());
  return result;
}

void IoRuntime::CloseInput(const VmObjectRef owner) {
  if (auto *state = FindInput(owner); state != nullptr)
    state->closed = true;
}

void IoRuntime::SetOutput(const VmObjectRef owner, OutputState state) {
  outputs_[owner.Value()] = std::move(state);
}

IoRuntime::OutputState &IoRuntime::Output(const VmObjectRef owner) {
  const auto found = outputs_.find(owner.Value());
  if (found == outputs_.end() || found->second.closed) {
    throw IoRuntimeError("output stream is closed or was never opened");
  }
  return found->second;
}

IoRuntime::OutputState *
IoRuntime::FindOutput(const VmObjectRef owner) noexcept {
  const auto found = outputs_.find(owner.Value());
  return found == outputs_.end() ? nullptr : &found->second;
}

void IoRuntime::AdoptOutput(const VmObjectRef source,
                            const VmObjectRef target) {
  auto &state = Output(source);
  outputs_[target.Value()] = std::move(state);
  outputs_.erase(source.Value());
}

void IoRuntime::FlushOutput(const VmObjectRef owner, const bool close) {
  auto *state = FindOutput(owner);
  if (state == nullptr || state->closed)
    return;
  if (!state->path.empty())
    WriteFile(state->path, state->bytes);
  state->closed = close;
}

std::optional<IoFileInfo> IoRuntime::Stat(const std::string_view path) const {
  return file_system_ != nullptr ? file_system_->Stat(path) : std::nullopt;
}

std::optional<std::vector<std::string>>
IoRuntime::List(const std::string_view path) const {
  return file_system_ != nullptr ? file_system_->List(path) : std::nullopt;
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

bool IoRuntime::Delete(const std::string_view path) {
  return file_system_ != nullptr && file_system_->Delete(path);
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
  inputs_.erase(owner.Value());
  outputs_.erase(owner.Value());
}

} // namespace ogplay::runtime::dexvm
