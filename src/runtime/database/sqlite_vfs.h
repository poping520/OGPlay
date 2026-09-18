#pragma once
#include <memory>
#include <string>
#include "ogplay/runtime/database/database_runtime.h"
struct sqlite3;
namespace ogplay::runtime {
class VirtualFileSystem;
namespace database {
class SqliteVfs final {
public:
  SqliteVfs(VirtualFileSystem &, std::string database_path,
            IoFaultInjector = {}, CurrentTimeMillis = {}, RandomBytes = {},
            OpenOptions = {.create = true});
  ~SqliteVfs();
  SqliteVfs(const SqliteVfs &) = delete;
  SqliteVfs &operator=(const SqliteVfs &) = delete;
  [[nodiscard]] sqlite3 *Open();
  void Close();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace database
} // namespace ogplay::runtime
