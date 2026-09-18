#include "ogplay/runtime/database/database_runtime.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "sqlite3.h"
#include "sqlite_vfs.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
namespace ogplay::runtime::database {
namespace {
int authorize(void *, int action, const char *first, const char *second,
              const char *, const char *) {
  if (action == SQLITE_ATTACH || action == SQLITE_DETACH)
    return SQLITE_DENY;
  if (action == SQLITE_PRAGMA && first && second) {
    auto pragma = std::string(first);
    auto value = std::string(second);
    std::ranges::transform(pragma, pragma.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (pragma == "journal_mode" && value == "wal")
      return SQLITE_DENY;
  }
  return SQLITE_OK;
}

[[noreturn]] void fail(sqlite3 *d, const char *m) {
  throw Error(sqlite3_extended_errcode(d),
              std::string(m) + ": " + sqlite3_errmsg(d));
}
const void *blob_data(const std::vector<std::byte> &value) {
  static constexpr std::byte empty{};
  return value.empty() ? &empty : value.data();
}
void bind(sqlite3 *d, sqlite3_stmt *s, std::span<const Value> a) {
  if (sqlite3_bind_parameter_count(s) != a.size())
    throw std::runtime_error("SQLite argument count mismatch");
  for (size_t i = 0; i < a.size(); ++i) {
    int n = int(i + 1), r;
    if (std::holds_alternative<std::monostate>(a[i]))
      r = sqlite3_bind_null(s, n);
    else if (auto *pi = std::get_if<int64_t>(&a[i]))
      r = sqlite3_bind_int64(s, n, *pi);
    else if (auto *pd = std::get_if<double>(&a[i]))
      r = sqlite3_bind_double(s, n, *pd);
    else if (auto *pt = std::get_if<std::string>(&a[i]))
      r = sqlite3_bind_text64(s, n, pt->data(), pt->size(), SQLITE_TRANSIENT,
                              SQLITE_UTF8);
    else {
      auto &v = std::get<std::vector<std::byte>>(a[i]);
      r = sqlite3_bind_blob64(s, n, blob_data(v), v.size(), SQLITE_TRANSIENT);
    }
    if (r != SQLITE_OK)
      fail(d, "bind");
  }
}
} // namespace
struct Connection::Impl {
  VirtualFileSystem *v{};
  std::string p;
  std::unique_ptr<SqliteVfs> backend;
  sqlite3 *d{};
  std::atomic_bool open{true};
  std::atomic_bool closing{};
  mutable std::shared_mutex lifecycle;
  mutable std::recursive_mutex execution;
  std::uint32_t next_statement{1};
  std::unordered_map<std::uint32_t, sqlite3_stmt *> statements;
  struct Guard final {
    std::shared_lock<std::shared_mutex> lifecycle;
    std::unique_lock<std::recursive_mutex> execution;
    explicit Guard(const Impl &impl)
        : lifecycle(impl.lifecycle), execution(impl.execution) {
      if (!impl.open || impl.closing)
        throw std::runtime_error("SQLite connection is closed");
    }
  };
  [[nodiscard]] Guard Use() const { return Guard(*this); }
};

namespace {
sqlite3_stmt *statement(auto &impl, std::uint32_t token) {
  auto found = impl.statements.find(token);
  if (found == impl.statements.end())
    throw std::runtime_error("stale SQLite statement token");
  return found->second;
}

Value column(sqlite3_stmt *s, int i) {
  switch (sqlite3_column_type(s, i)) {
  case SQLITE_NULL:
    return std::monostate{};
  case SQLITE_INTEGER:
    return sqlite3_column_int64(s, i);
  case SQLITE_FLOAT:
    return sqlite3_column_double(s, i);
  case SQLITE_TEXT:
    return std::string(
        reinterpret_cast<const char *>(sqlite3_column_text(s, i)),
        sqlite3_column_bytes(s, i));
  default: {
    auto *p = static_cast<const std::byte *>(sqlite3_column_blob(s, i));
    const auto size = sqlite3_column_bytes(s, i);
    return size == 0 ? std::vector<std::byte>{}
                     : std::vector<std::byte>(p, p + size);
  }
  }
}
struct StatementGuard final {
  sqlite3_stmt *value{};
  ~StatementGuard() {
    if (value)
      sqlite3_finalize(value);
  }
};
} // namespace
Connection::Connection(VirtualFileSystem &v, std::string p,
                       IoFaultInjector injector, CurrentTimeMillis current_time,
                       RandomBytes random_bytes, OpenOptions options)
    : impl_(std::make_unique<Impl>()) {
  impl_->v = &v;
  impl_->p = std::move(p);
  impl_->backend =
      std::make_unique<SqliteVfs>(v, impl_->p, std::move(injector),
                                  std::move(current_time),
                                  std::move(random_bytes), options);
}
std::shared_ptr<Connection> Connection::Open(VirtualFileSystem &v,
                                             std::string p,
                                             IoFaultInjector injector,
                                             CurrentTimeMillis current_time,
                                             RandomBytes random_bytes,
                                             OpenOptions options) {
  auto c = std::shared_ptr<Connection>(
      new Connection(v, std::move(p), std::move(injector),
                     std::move(current_time), std::move(random_bytes),
                     options));
  c->impl_->d = c->impl_->backend->Open();
  if (sqlite3_set_authorizer(c->impl_->d, authorize, nullptr) != SQLITE_OK)
    fail(c->impl_->d, "install authorizer");
  c->Execute("PRAGMA journal_mode=DELETE");
  c->Execute("PRAGMA mmap_size=0");
  return c;
}
Connection::~Connection() {
  try {
    Close();
  } catch (...) {
  }
}
void Connection::Execute(std::string_view q, std::span<const Value> a) {
  auto use = impl_->Use();
  sqlite3_stmt *s{};
  if (sqlite3_prepare_v2(impl_->d, q.data(), int(q.size()), &s, nullptr) !=
      SQLITE_OK)
    fail(impl_->d, "prepare");
  StatementGuard guard{s};
  bind(impl_->d, s, a);
  int r = sqlite3_step(s);
  if (r != SQLITE_DONE && r != SQLITE_ROW)
    fail(impl_->d, "execute");
}
Result Connection::Query(std::string_view q, std::span<const Value> a) {
  auto use = impl_->Use();
  sqlite3_stmt *s{};
  if (sqlite3_prepare_v2(impl_->d, q.data(), int(q.size()), &s, nullptr) !=
      SQLITE_OK)
    fail(impl_->d, "prepare");
  StatementGuard guard{s};
  bind(impl_->d, s, a);
  Result z;
  int n = sqlite3_column_count(s);
  for (int i = 0; i < n; ++i)
    z.columns.emplace_back(sqlite3_column_name(s, i));
  int rc;
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    auto &r = z.rows.emplace_back();
    for (int i = 0; i < n; ++i) {
      r.emplace_back(column(s, i));
    }
  }
  if (rc != SQLITE_DONE)
    fail(impl_->d, "query");
  return z;
}
std::uint32_t Connection::Prepare(std::string_view sql) {
  auto use = impl_->Use();
  sqlite3_stmt *s{};
  if (sqlite3_prepare_v2(impl_->d, sql.data(), int(sql.size()), &s, nullptr) !=
      SQLITE_OK)
    fail(impl_->d, "prepare");
  auto token = impl_->next_statement++;
  if (!token)
    token = impl_->next_statement++;
  impl_->statements.emplace(token, s);
  return token;
}
void Connection::Finalize(std::uint32_t token) {
  auto use = impl_->Use();
  auto *s = statement(*impl_, token);
  impl_->statements.erase(token);
  sqlite3_finalize(s);
}
std::int32_t Connection::ParameterCount(std::uint32_t token) const {
  auto use = impl_->Use();
  return sqlite3_bind_parameter_count(statement(*impl_, token));
}
bool Connection::IsReadOnly(std::uint32_t token) const {
  auto use = impl_->Use();
  return sqlite3_stmt_readonly(statement(*impl_, token)) != 0;
}
std::int32_t Connection::ColumnCount(std::uint32_t token) const {
  auto use = impl_->Use();
  return sqlite3_column_count(statement(*impl_, token));
}
std::string Connection::ColumnName(std::uint32_t token,
                                   std::int32_t index) const {
  auto use = impl_->Use();
  auto *s = statement(*impl_, token);
  if (index < 0 || index >= sqlite3_column_count(s))
    throw std::runtime_error("SQLite column index out of range");
  return sqlite3_column_name(s, index);
}
void Connection::Bind(std::uint32_t token, std::int32_t index, Value value) {
  auto use = impl_->Use();
  auto *s = statement(*impl_, token);
  if (index <= 0 || index > sqlite3_bind_parameter_count(s))
    throw std::runtime_error("SQLite bind index out of range");
  std::array<Value, 1> one{std::move(value)};
  int r = SQLITE_ERROR;
  const auto &v = one[0];
  if (std::holds_alternative<std::monostate>(v))
    r = sqlite3_bind_null(s, index);
  else if (auto *integer = std::get_if<std::int64_t>(&v))
    r = sqlite3_bind_int64(s, index, *integer);
  else if (auto *real = std::get_if<double>(&v))
    r = sqlite3_bind_double(s, index, *real);
  else if (auto *text = std::get_if<std::string>(&v))
    r = sqlite3_bind_text64(s, index, text->data(), text->size(),
                            SQLITE_TRANSIENT, SQLITE_UTF8);
  else {
    const auto &blob = std::get<std::vector<std::byte>>(v);
    r = sqlite3_bind_blob64(s, index, blob_data(blob), blob.size(),
                            SQLITE_TRANSIENT);
  }
  if (r != SQLITE_OK)
    fail(impl_->d, "bind");
}
void Connection::Reset(std::uint32_t token) {
  auto use = impl_->Use();
  auto *s = statement(*impl_, token);
  sqlite3_reset(s);
  sqlite3_clear_bindings(s);
}
void Connection::Execute(std::uint32_t token) {
  auto use = impl_->Use();
  auto r = sqlite3_step(statement(*impl_, token));
  if (r != SQLITE_DONE && r != SQLITE_ROW)
    fail(impl_->d, "execute");
}
Value Connection::ExecuteScalar(std::uint32_t token) {
  auto use = impl_->Use();
  auto *s = statement(*impl_, token);
  auto r = sqlite3_step(s);
  if (r != SQLITE_ROW)
    throw std::runtime_error(std::string("execute scalar returned ") +
                             std::to_string(r) + " for " + sqlite3_sql(s) +
                             ": " + sqlite3_errmsg(impl_->d));
  return column(s, 0);
}
Result Connection::Query(std::uint32_t token) {
  auto use = impl_->Use();
  auto *s = statement(*impl_, token);
  Result out;
  const int count = sqlite3_column_count(s);
  for (int i = 0; i < count; ++i)
    out.columns.emplace_back(sqlite3_column_name(s, i));
  int rc;
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    auto &row = out.rows.emplace_back();
    for (int i = 0; i < count; ++i)
      row.emplace_back(column(s, i));
  }
  if (rc != SQLITE_DONE)
    fail(impl_->d, "query");
  return out;
}
WindowResult Connection::QueryWindow(std::uint32_t token, std::int32_t start,
                                     std::int32_t required,
                                     bool count_all_rows,
                                     std::size_t capacity) {
  auto use = impl_->Use();
  auto *s = statement(*impl_, token);
  struct Reset final {
    sqlite3_stmt *statement;
    ~Reset() { sqlite3_reset(statement); }
  } reset{s};
  WindowResult out;
  out.start_position = std::max(0, start);
  out.columns = sqlite3_column_count(s);
  bool full = false;
  int rc = SQLITE_OK;
  while (!full || count_all_rows) {
    rc = sqlite3_step(s);
    if (rc == SQLITE_DONE)
      break;
    if (rc != SQLITE_ROW)
      fail(impl_->d, "query cursor window");
    const auto position = out.total_rows++;
    if (position < out.start_position || full)
      continue;
    std::vector<Value> row;
    row.reserve(static_cast<std::size_t>(out.columns));
    std::size_t cost = static_cast<std::size_t>(out.columns) * 16U;
    for (int column_index = 0; column_index < out.columns; ++column_index) {
      row.emplace_back(column(s, column_index));
      if (const auto *text = std::get_if<std::string>(&row.back()))
        cost += text->size() + 1U;
      else if (const auto *blob =
                   std::get_if<std::vector<std::byte>>(&row.back()))
        cost += blob->size();
    }
    if (out.used + cost > capacity && !out.rows.empty() &&
        out.start_position + static_cast<std::int32_t>(out.rows.size()) <=
            required) {
      out.rows.clear();
      out.used = 0;
      out.start_position = position;
    }
    if (out.used + cost > capacity) {
      full = true;
      continue;
    }
    out.used += cost;
    out.rows.emplace_back(std::move(row));
  }
  return out;
}
void Connection::Cancel() {
  std::shared_lock lifecycle(impl_->lifecycle);
  if (impl_->open)
    sqlite3_interrupt(impl_->d);
}
void Connection::RegisterLocalizedCollators(std::string_view locale) {
  auto use = impl_->Use();
  if (!(locale.empty() || locale == "en" || locale == "en_US" ||
        locale == "zh" || locale == "zh_CN"))
    throw std::runtime_error("unsupported SQLite collation locale: " +
                             std::string(locale));
  const auto compare = [](void *, int left_size, const void *left_data,
                          int right_size, const void *right_data) {
    const auto left = std::string_view(static_cast<const char *>(left_data),
                                       static_cast<std::size_t>(left_size));
    const auto right = std::string_view(static_cast<const char *>(right_data),
                                        static_cast<std::size_t>(right_size));
    const auto fold = [](unsigned char value) {
      return value >= 'A' && value <= 'Z'
                 ? static_cast<unsigned char>(value - 'A' + 'a')
                 : value;
    };
    const auto count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
      const auto a = fold(static_cast<unsigned char>(left[i]));
      const auto b = fold(static_cast<unsigned char>(right[i]));
      if (a != b)
        return a < b ? -1 : 1;
    }
    return left.size() == right.size() ? 0 : left.size() < right.size() ? -1 : 1;
  };
  for (const auto *name : {"LOCALIZED", "UNICODE"}) {
    if (sqlite3_create_collation_v2(impl_->d, name, SQLITE_UTF8, nullptr,
                                    compare, nullptr) != SQLITE_OK)
      fail(impl_->d, "register collation");
  }
}
int64_t Connection::LastInsertRowId() const {
  auto use = impl_->Use();
  return sqlite3_last_insert_rowid(impl_->d);
}
int32_t Connection::ChangedRows() const {
  auto use = impl_->Use();
  return sqlite3_changes(impl_->d);
}
int32_t Connection::UserVersion() {
  return int32_t(std::get<int64_t>(Query("PRAGMA user_version").rows[0][0]));
}
void Connection::SetUserVersion(int32_t v) {
  Execute("PRAGMA user_version=" + std::to_string(v));
}
void Connection::Flush() {
  auto use = impl_->Use();
  impl_->v->FlushAll();
}
void Connection::Close() {
  if (!impl_->open.exchange(false))
    return;
  impl_->closing = true;
  if (impl_->d)
    sqlite3_interrupt(impl_->d);
  std::unique_lock lifecycle(impl_->lifecycle);
  std::scoped_lock execution(impl_->execution);
  {
    for (auto &[token, s] : impl_->statements)
      sqlite3_finalize(s);
    impl_->statements.clear();
    impl_->v->FlushAll();
    impl_->backend->Close();
    impl_->d = nullptr;
  }
}
bool Connection::IsOpen() const noexcept { return impl_->open; }
const std::string &Connection::Path() const noexcept { return impl_->p; }
} // namespace ogplay::runtime::database
