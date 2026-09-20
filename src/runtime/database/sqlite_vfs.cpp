#include "sqlite_vfs.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "sqlite3.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace ogplay::runtime::database {
namespace {
int MapError(int e) {
  return e == 2    ? SQLITE_CANTOPEN
         : e == 13 ? SQLITE_READONLY
         : e == 28 ? SQLITE_FULL
                   : SQLITE_IOERR;
}
struct Context {
  VirtualFileSystem *vfs{};
  std::string database_path;
  std::atomic_uint64_t temporary{};
  IoFaultInjector inject;
  CurrentTimeMillis current_time_millis;
  RandomBytes random_bytes;
  OpenOptions options;
};
int Inject(Context &context, const IoOperation operation,
           const std::string_view path) {
  if (context.inject) {
    if (const auto error = context.inject(operation, path))
      return MapError(*error);
  }
  return SQLITE_OK;
}
struct File {
  sqlite3_file base;
  Context *context;
  std::int32_t descriptor;
  int lock;
  int delete_on_close;
  char path[1024];
};
static_assert(std::is_trivial_v<File>);
struct LockKey {
  VirtualFileSystem *vfs{};
  std::string path;
  bool operator==(const LockKey &) const = default;
};
struct LockHash {
  std::size_t operator()(const LockKey &k) const noexcept {
    return std::hash<void *>{}(k.vfs) ^
           (std::hash<std::string>{}(k.path) << 1U);
  }
};
std::mutex lock_mutex;
std::unordered_map<LockKey, std::unordered_map<File *, int>, LockHash>
    lock_table;
LockKey Key(const File &f) { return {f.context->vfs, f.path}; }
int Close(sqlite3_file *b) {
  auto *f = reinterpret_cast<File *>(b);
  {
    std::scoped_lock g(lock_mutex);
    auto i = lock_table.find(Key(*f));
    if (i != lock_table.end()) {
      i->second.erase(f);
      if (i->second.empty())
        lock_table.erase(i);
    }
  }
  try {
    f->context->vfs->Close(f->descriptor);
    if (f->delete_on_close)
      try {
        f->context->vfs->RemoveFile(f->path);
      } catch (const VfsError &e) {
        if (e.ErrorNumber() != 2)
          throw;
      }
    return SQLITE_OK;
  } catch (const VfsError &e) {
    return MapError(e.ErrorNumber());
  }
}
int Read(sqlite3_file *b, void *out, int amount, sqlite3_int64 offset) {
  auto *f = reinterpret_cast<File *>(b);
  if (offset < 0)
    return SQLITE_IOERR;
  try {
    auto bytes =
        std::span(static_cast<std::byte *>(out), static_cast<size_t>(amount));
    size_t done{};
    while (done < bytes.size()) {
      auto n = f->context->vfs->ReadAt(
          f->descriptor, static_cast<std::uint64_t>(offset) + done,
          bytes.subspan(done));
      if (!n)
        break;
      done += n;
    }
    if (done == bytes.size())
      return SQLITE_OK;
    std::fill(bytes.begin() + static_cast<ptrdiff_t>(done), bytes.end(),
              std::byte{});
    return SQLITE_IOERR_SHORT_READ;
  } catch (const VfsError &e) {
    return MapError(e.ErrorNumber());
  }
}
int Write(sqlite3_file *b, const void *in, int amount, sqlite3_int64 offset) {
  auto *f = reinterpret_cast<File *>(b);
  if (offset < 0)
    return SQLITE_IOERR;
  if (const auto injected = Inject(*f->context, IoOperation::write, f->path);
      injected != SQLITE_OK)
    return injected;
  try {
    auto bytes = std::span(static_cast<const std::byte *>(in),
                           static_cast<size_t>(amount));
    size_t done{};
    while (done < bytes.size()) {
      auto n = f->context->vfs->WriteAt(
          f->descriptor, static_cast<std::uint64_t>(offset) + done,
          bytes.subspan(done));
      if (!n)
        return SQLITE_FULL;
      done += n;
    }
    return SQLITE_OK;
  } catch (const VfsError &e) {
    return MapError(e.ErrorNumber());
  }
}
int Truncate(sqlite3_file *b, sqlite3_int64 size) {
  auto *f = reinterpret_cast<File *>(b);
  if (const auto injected = Inject(*f->context, IoOperation::truncate, f->path);
      injected != SQLITE_OK)
    return injected;
  try {
    f->context->vfs->Truncate(f->descriptor, size);
    return SQLITE_OK;
  } catch (const VfsError &e) {
    return MapError(e.ErrorNumber());
  }
}
int Sync(sqlite3_file *b, int) {
  auto *f = reinterpret_cast<File *>(b);
  if (const auto injected = Inject(*f->context, IoOperation::sync, f->path);
      injected != SQLITE_OK)
    return injected;
  try {
    f->context->vfs->Flush(f->descriptor);
    return SQLITE_OK;
  } catch (const VfsError &e) {
    return MapError(e.ErrorNumber());
  }
}
int Size(sqlite3_file *b, sqlite3_int64 *out) {
  auto *f = reinterpret_cast<File *>(b);
  try {
    *out = static_cast<sqlite3_int64>(
        f->context->vfs->DescriptorInfo(f->descriptor).size);
    return SQLITE_OK;
  } catch (const VfsError &e) {
    return MapError(e.ErrorNumber());
  }
}
bool Other(const File &f, int level) {
  auto i = lock_table.find(Key(f));
  return i != lock_table.end() &&
         std::ranges::any_of(i->second, [&](const auto &e) {
           return e.first != &f && e.second >= level;
         });
}
int Lock(sqlite3_file *b, int wanted) {
  auto *f = reinterpret_cast<File *>(b);
  std::scoped_lock g(lock_mutex);
  if (wanted <= f->lock)
    return SQLITE_OK;
  if (wanted == SQLITE_LOCK_SHARED && Other(*f, SQLITE_LOCK_PENDING))
    return SQLITE_BUSY;
  if (wanted == SQLITE_LOCK_RESERVED && Other(*f, SQLITE_LOCK_RESERVED))
    return SQLITE_BUSY;
  if (wanted == SQLITE_LOCK_PENDING && Other(*f, SQLITE_LOCK_RESERVED))
    return SQLITE_BUSY;
  if (wanted == SQLITE_LOCK_EXCLUSIVE && Other(*f, SQLITE_LOCK_SHARED))
    return SQLITE_BUSY;
  f->lock = wanted;
  lock_table[Key(*f)][f] = wanted;
  return SQLITE_OK;
}
int Unlock(sqlite3_file *b, int wanted) {
  auto *f = reinterpret_cast<File *>(b);
  std::scoped_lock g(lock_mutex);
  f->lock = wanted;
  auto i = lock_table.find(Key(*f));
  if (i != lock_table.end()) {
    if (wanted)
      i->second[f] = wanted;
    else
      i->second.erase(f);
    if (i->second.empty())
      lock_table.erase(i);
  }
  return SQLITE_OK;
}
int Reserved(sqlite3_file *b, int *out) {
  auto *f = reinterpret_cast<File *>(b);
  std::scoped_lock g(lock_mutex);
  auto i = lock_table.find(Key(*f));
  *out = i != lock_table.end() &&
         std::ranges::any_of(i->second, [](const auto &e) {
           return e.second >= SQLITE_LOCK_RESERVED;
         });
  return SQLITE_OK;
}
int Control(sqlite3_file *b, int op, void *out) {
  if (op == SQLITE_FCNTL_LOCKSTATE) {
    *static_cast<int *>(out) = reinterpret_cast<File *>(b)->lock;
    return SQLITE_OK;
  }
  return SQLITE_NOTFOUND;
}
int Sector(sqlite3_file *) { return 4096; }
int Characteristics(sqlite3_file *) { return 0; }
constexpr sqlite3_io_methods methods{
    1,    Close,  Read,     Write,   Truncate, Sync,           Size,
    Lock, Unlock, Reserved, Control, Sector,   Characteristics};
int VfsOpen(sqlite3_vfs *v, const char *name, sqlite3_file *out, int flags,
            int *out_flags) {
  auto *c = static_cast<Context *>(v->pAppData);
  auto *f = reinterpret_cast<File *>(out);
  std::memset(f, 0, sizeof(*f));
  f->context = c;
  f->descriptor = -1;
  std::string path =
      name ? name : c->database_path + ".tmp-" + std::to_string(c->temporary++);
  try {
    path = c->vfs->CanonicalPath(path);
  } catch (const VfsError &e) {
    return MapError(e.ErrorNumber());
  }
  if (path.size() >= sizeof(f->path))
    return SQLITE_CANTOPEN;
  std::memcpy(f->path, path.c_str(), path.size() + 1);
  f->delete_on_close = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
  bool readonly = (flags & SQLITE_OPEN_READONLY) != 0;
  if (const auto injected = Inject(*c, IoOperation::open, path);
      injected != SQLITE_OK)
    return injected;
  try {
    f->descriptor =
        c->vfs->Open(path, {.read = true,
                            .write = !readonly,
                            .create = (flags & SQLITE_OPEN_CREATE) != 0});
    f->base.pMethods = &methods;
    if (out_flags)
      *out_flags = readonly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    return SQLITE_OK;
  } catch (const VfsError &e) {
    return MapError(e.ErrorNumber());
  }
}
int Delete(sqlite3_vfs *v, const char *name, int) {
  auto *c = static_cast<Context *>(v->pAppData);
  if (const auto injected = Inject(*c, IoOperation::remove, name);
      injected != SQLITE_OK)
    return injected;
  try {
    c->vfs->RemoveFile(name);
    return SQLITE_OK;
  } catch (const VfsError &e) {
    return e.ErrorNumber() == 2 ? SQLITE_OK : MapError(e.ErrorNumber());
  }
}
int Access(sqlite3_vfs *v, const char *name, int flags, int *out) {
  auto *c = static_cast<Context *>(v->pAppData);
  try {
    auto info = c->vfs->Stat(name);
    *out =
        flags == SQLITE_ACCESS_READWRITE ? info.writable : !info.is_directory;
  } catch (const VfsError &e) {
    if (e.ErrorNumber() != 2)
      return MapError(e.ErrorNumber());
    *out = 0;
  }
  return SQLITE_OK;
}
int Full(sqlite3_vfs *v, const char *name, int size, char *out) {
  if (!name)
    return SQLITE_CANTOPEN;
  auto *c = static_cast<Context *>(v->pAppData);
  std::string canonical;
  try {
    canonical = c->vfs->CanonicalPath(name);
  } catch (const VfsError &) {
    return SQLITE_CANTOPEN;
  }
  if (static_cast<int>(canonical.size()) >= size)
    return SQLITE_CANTOPEN;
  std::memcpy(out, canonical.c_str(), canonical.size() + 1);
  return SQLITE_OK;
}
void *DlOpen(sqlite3_vfs *, const char *) { return nullptr; }
void DlError(sqlite3_vfs *, int n, char *out) {
  sqlite3_snprintf(n, out, "extensions disabled");
}
void (*DlSym(sqlite3_vfs *, void *, const char *))(void) { return nullptr; }
void DlClose(sqlite3_vfs *, void *) {}
int Random(sqlite3_vfs *vfs, int n, char *out) {
  auto *context = static_cast<Context *>(vfs->pAppData);
  if (context->random_bytes) {
    try {
      context->random_bytes(std::span(reinterpret_cast<std::byte *>(out),
                                      static_cast<std::size_t>(n)));
      return n;
    } catch (...) {
      std::memset(out, 0, static_cast<std::size_t>(n));
      return 0;
    }
  }
  static std::atomic_uint64_t state{1};
  auto x = state.fetch_add(0x9e3779b97f4a7c15ULL);
  for (int i = 0; i < n; ++i) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    out[i] = static_cast<char>(x);
  }
  return n;
}
int Sleep(sqlite3_vfs *, int us) {
  std::this_thread::sleep_for(std::chrono::microseconds(us));
  return us;
}
int CurrentTime(sqlite3_vfs *vfs, double *out) {
  auto *context = static_cast<Context *>(vfs->pAppData);
  if (!context->current_time_millis)
    return SQLITE_IOERR;
  *out = 2440587.5 +
         static_cast<double>(context->current_time_millis()) / 86400000.;
  return SQLITE_OK;
}
int LastError(sqlite3_vfs *, int, char *) { return 0; }
} // namespace
struct SqliteVfs::Impl {
  Context context;
  std::string name;
  sqlite3_vfs api{};
  sqlite3 *database{};
};
SqliteVfs::SqliteVfs(VirtualFileSystem &vfs, std::string path,
                     IoFaultInjector injector, CurrentTimeMillis current_time,
                     RandomBytes random_bytes, OpenOptions options)
    : impl_(std::make_unique<Impl>()) {
  static std::atomic_uint64_t sequence{};
  impl_->context.vfs = &vfs;
  impl_->context.database_path = std::move(path);
  impl_->context.inject = std::move(injector);
  impl_->context.current_time_millis = std::move(current_time);
  impl_->context.random_bytes = std::move(random_bytes);
  impl_->context.options = options;
  impl_->name = "ogplay-" + std::to_string(sequence++);
  impl_->api = {1,
                static_cast<int>(sizeof(File)),
                1024,
                nullptr,
                impl_->name.c_str(),
                &impl_->context,
                VfsOpen,
                Delete,
                Access,
                Full,
                DlOpen,
                DlError,
                DlSym,
                DlClose,
                Random,
                Sleep,
                CurrentTime,
                LastError};
  if (sqlite3_vfs_register(&impl_->api, 0) != SQLITE_OK)
    throw std::runtime_error("SQLite VFS registration failed");
}
SqliteVfs::~SqliteVfs() {
  try {
    Close();
  } catch (...) {
  }
  sqlite3_vfs_unregister(&impl_->api);
}
sqlite3 *SqliteVfs::Open() {
  if (impl_->database)
    return impl_->database;
  const auto options = impl_->context.options;
  const int flags =
      (options.create ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                      : options.read_only ? SQLITE_OPEN_READONLY
                                          : SQLITE_OPEN_READWRITE) |
      SQLITE_OPEN_FULLMUTEX;
  int rc = sqlite3_open_v2(
      impl_->context.database_path.c_str(), &impl_->database, flags,
      impl_->name.c_str());
  if (rc != SQLITE_OK) {
    const auto message = impl_->database ? sqlite3_errmsg(impl_->database)
                                         : "SQLite open failed";
    throw Error(rc, message);
  }
  return impl_->database;
}
void SqliteVfs::Close() {
  if (!impl_->database)
    return;
  const auto result = sqlite3_close(impl_->database);
  if (result != SQLITE_OK)
    throw std::runtime_error("SQLite close failed with live resources: " +
                             std::to_string(result));
  impl_->database = nullptr;
}
} // namespace ogplay::runtime::database
