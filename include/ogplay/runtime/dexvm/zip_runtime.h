#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/dexvm_types.h"

namespace ogplay::runtime::dexvm {

class ZipRuntimeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Per-VM java.util.zip state. Archive parsing and inflation reuse the strict
// loader implementation, while guest object ownership stays in DexVM core.
class ZipRuntime final {
public:
  ZipRuntime();
  ~ZipRuntime();
  ZipRuntime(const ZipRuntime &) = delete;
  ZipRuntime &operator=(const ZipRuntime &) = delete;

  void Open(VmObjectRef owner, std::vector<std::byte> bytes);
  [[nodiscard]] std::optional<std::string> NextEntry(VmObjectRef owner);
  [[nodiscard]] std::optional<std::vector<std::byte>>
  Read(VmObjectRef owner, std::size_t maximum);
  void CloseEntry(VmObjectRef owner);
  void Close(VmObjectRef owner) noexcept;
  [[nodiscard]] bool Contains(VmObjectRef owner) const noexcept;
  void Sweep(VmObjectRef owner) noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ogplay::runtime::dexvm
