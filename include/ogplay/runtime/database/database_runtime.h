#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
namespace ogplay::runtime {
class VirtualFileSystem;
namespace database {
using Value = std::variant<std::monostate, std::int64_t, double, std::string,
                           std::vector<std::byte>>;
struct Result final {
  std::vector<std::string> columns;
  std::vector<std::vector<Value>> rows;
};
enum class IoOperation : std::uint8_t { open, write, truncate, sync, remove };
using IoFaultInjector =
    std::function<std::optional<int>(IoOperation, std::string_view)>;
using CurrentTimeMillis = std::function<std::int64_t()>;
using RandomBytes = std::function<void(std::span<std::byte>)>;
struct OpenOptions final {
  bool read_only{};
  bool create{};
};
class Error final : public std::runtime_error {
public:
  Error(std::int32_t code, std::string message)
      : std::runtime_error(std::move(message)), code_(code) {}
  [[nodiscard]] std::int32_t Code() const noexcept { return code_; }
private:
  std::int32_t code_{};
};
struct WindowResult final {
  std::int32_t start_position{};
  std::int32_t total_rows{};
  std::int32_t columns{};
  std::size_t used{};
  std::vector<std::vector<Value>> rows;
};
class Connection final {
public:
  static std::shared_ptr<Connection> Open(VirtualFileSystem &, std::string,
                                          IoFaultInjector = {},
                                          CurrentTimeMillis = {},
                                          RandomBytes = {},
                                          OpenOptions = {.create = true});
  ~Connection();
  Connection(const Connection &) = delete;
  Connection &operator=(const Connection &) = delete;
  void Execute(std::string_view, std::span<const Value> = {});
  [[nodiscard]] Result Query(std::string_view, std::span<const Value> = {});
  [[nodiscard]] std::uint32_t Prepare(std::string_view);
  void Finalize(std::uint32_t);
  [[nodiscard]] std::int32_t ParameterCount(std::uint32_t) const;
  [[nodiscard]] bool IsReadOnly(std::uint32_t) const;
  [[nodiscard]] std::int32_t ColumnCount(std::uint32_t) const;
  [[nodiscard]] std::string ColumnName(std::uint32_t, std::int32_t) const;
  void Bind(std::uint32_t, std::int32_t, Value);
  void Reset(std::uint32_t);
  void Execute(std::uint32_t);
  [[nodiscard]] Value ExecuteScalar(std::uint32_t);
  [[nodiscard]] Result Query(std::uint32_t);
  [[nodiscard]] WindowResult QueryWindow(std::uint32_t, std::int32_t start,
                                         std::int32_t required,
                                         bool count_all_rows,
                                         std::size_t capacity);
  void Cancel();
  void RegisterLocalizedCollators(std::string_view locale);
  [[nodiscard]] std::int64_t LastInsertRowId() const;
  [[nodiscard]] std::int32_t ChangedRows() const;
  [[nodiscard]] std::int32_t UserVersion();
  void SetUserVersion(std::int32_t);
  void Flush();
  void Close();
  [[nodiscard]] bool IsOpen() const noexcept;
  [[nodiscard]] const std::string &Path() const noexcept;

private:
  Connection(VirtualFileSystem &, std::string, IoFaultInjector,
             CurrentTimeMillis, RandomBytes, OpenOptions);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace database
} // namespace ogplay::runtime
