#include "ogplay/runtime/dexvm/zip_runtime.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "ogplay/loader/apk.h"

namespace ogplay::runtime::dexvm {

class ZipRuntime::Impl final {
public:
  struct Stream final {
    std::vector<std::byte> raw;
    loader::ApkArchive archive;
    std::size_t next_entry{};
    std::vector<std::byte> entry_bytes;
    std::size_t cursor{};
    bool entry_open{};
    bool closed{};
  };

  [[nodiscard]] Stream &OpenStream(const VmObjectRef owner) {
    const auto found = streams.find(owner.Value());
    if (found == streams.end() || found->second.closed) {
      throw ZipRuntimeError("zip stream is closed or was never opened");
    }
    return found->second;
  }

  std::unordered_map<std::uint32_t, Stream> streams;
};

ZipRuntime::ZipRuntime() : impl_(std::make_unique<Impl>()) {}
ZipRuntime::~ZipRuntime() = default;

void ZipRuntime::Open(const VmObjectRef owner, std::vector<std::byte> bytes) {
  Impl::Stream stream;
  stream.raw = std::move(bytes);
  try {
    stream.archive = loader::ParseApkArchive(stream.raw);
  } catch (const std::exception &error) {
    throw ZipRuntimeError(error.what());
  }
  impl_->streams[owner.Value()] = std::move(stream);
}

std::optional<std::string> ZipRuntime::NextEntry(const VmObjectRef owner) {
  auto &stream = impl_->OpenStream(owner);
  stream.entry_bytes.clear();
  stream.cursor = 0;
  stream.entry_open = false;
  if (stream.next_entry >= stream.archive.entries.size()) {
    return std::nullopt;
  }
  const auto &entry = stream.archive.entries[stream.next_entry++];
  try {
    stream.entry_bytes =
        loader::ReadApkEntry(stream.raw, stream.archive, entry.name);
  } catch (const std::exception &error) {
    throw ZipRuntimeError(error.what());
  }
  stream.entry_open = true;
  return entry.name;
}

std::optional<std::vector<std::byte>>
ZipRuntime::Read(const VmObjectRef owner, const std::size_t maximum) {
  auto &stream = impl_->OpenStream(owner);
  if (!stream.entry_open || stream.cursor == stream.entry_bytes.size()) {
    return std::nullopt;
  }
  const auto amount =
      std::min(maximum, stream.entry_bytes.size() - stream.cursor);
  std::vector<std::byte> result(
      stream.entry_bytes.begin() + static_cast<std::ptrdiff_t>(stream.cursor),
      stream.entry_bytes.begin() +
          static_cast<std::ptrdiff_t>(stream.cursor + amount));
  stream.cursor += amount;
  return result;
}

void ZipRuntime::CloseEntry(const VmObjectRef owner) {
  auto &stream = impl_->OpenStream(owner);
  stream.entry_bytes.clear();
  stream.cursor = 0;
  stream.entry_open = false;
}

void ZipRuntime::Close(const VmObjectRef owner) noexcept {
  const auto found = impl_->streams.find(owner.Value());
  if (found != impl_->streams.end()) {
    found->second.closed = true;
    found->second.entry_bytes.clear();
  }
}

bool ZipRuntime::Contains(const VmObjectRef owner) const noexcept {
  return impl_->streams.contains(owner.Value());
}

void ZipRuntime::Sweep(const VmObjectRef owner) noexcept {
  impl_->streams.erase(owner.Value());
}

} // namespace ogplay::runtime::dexvm
