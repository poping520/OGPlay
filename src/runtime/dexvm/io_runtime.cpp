#include "ogplay/runtime/dexvm/io_runtime.h"

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

void IoRuntime::AdoptInput(const VmObjectRef source, const VmObjectRef target) {
  const auto found = inputs_.find(source.Value());
  if (found == inputs_.end() || found->second.closed ||
      found->second.state->closed) {
    throw IoRuntimeError("input stream is closed or was never opened");
  }
  inputs_[target.Value()] = std::move(found->second);
  inputs_.erase(source.Value());
}

std::vector<std::byte> IoRuntime::TakeRemainingInput(const VmObjectRef owner) {
  auto &state = Input(owner);
  std::vector<std::byte> result(state.bytes.begin() +
                                    static_cast<std::ptrdiff_t>(state.cursor),
                                state.bytes.end());
  state.cursor = state.bytes.size();
  inputs_.erase(owner.Value());
  return result;
}

void IoRuntime::CloseInput(const VmObjectRef owner) {
  const auto found = inputs_.find(owner.Value());
  if (found == inputs_.end() || found->second.closed)
    return;
  found->second.closed = true;
  if (found->second.close_underlying)
    found->second.state->closed = true;
}

void IoRuntime::BeginObjectInput(const VmObjectRef owner) {
  static_cast<void>(Input(owner));
  object_inputs_[owner.Value()] = {};
}

IoRuntime::ObjectInputState &IoRuntime::ObjectInput(const VmObjectRef owner) {
  static_cast<void>(Input(owner));
  const auto found = object_inputs_.find(owner.Value());
  if (found == object_inputs_.end()) {
    throw IoRuntimeError("object input stream was never opened");
  }
  return found->second;
}

void IoRuntime::BeginObjectOutput(const VmObjectRef owner) {
  static_cast<void>(Output(owner));
  object_outputs_[owner.Value()] = {};
}

IoRuntime::ObjectOutputState &IoRuntime::ObjectOutput(const VmObjectRef owner) {
  static_cast<void>(Output(owner));
  const auto found = object_outputs_.find(owner.Value());
  if (found == object_outputs_.end()) {
    throw IoRuntimeError("object output stream was never opened");
  }
  return found->second;
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

void IoRuntime::AdoptOutput(const VmObjectRef source,
                            const VmObjectRef target) {
  const auto found = outputs_.find(source.Value());
  if (found == outputs_.end() || found->second.closed ||
      found->second.state->closed) {
    throw IoRuntimeError("output stream is closed or was never opened");
  }
  outputs_[target.Value()] = std::move(found->second);
  outputs_.erase(source.Value());
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

bool IoRuntime::MakeDirectory(const std::string_view path) {
  return file_system_ != nullptr && file_system_->MakeDirectory(path);
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

bool IoRuntime::Rename(const std::string_view from,
                       const std::string_view to) {
  return file_system_ != nullptr && file_system_->Rename(from, to);
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
  object_inputs_.erase(owner.Value());
  outputs_.erase(owner.Value());
  object_outputs_.erase(owner.Value());
  descriptors_.erase(owner.Value());
}

void IoRuntime::Trace(
    const VmObjectRef owner,
    const std::function<void(VmObjectRef)> &visitor) const {
  const auto input = object_inputs_.find(owner.Value());
  if (input != object_inputs_.end()) {
    for (const auto &handle : input->second.handles) {
      if (handle.object.IsValid()) visitor(handle.object);
    }
  }
  const auto output = object_outputs_.find(owner.Value());
  if (output != object_outputs_.end()) {
    for (const auto object : output->second.handle_objects) {
      if (object.IsValid()) visitor(object);
    }
  }
}

} // namespace ogplay::runtime::dexvm
