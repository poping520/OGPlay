// Android launcher: the WebView page's rpc bridge into GuiRpcService, entered from LauncherBridge.java.
// Every Handle and Poll call arrives on the bridge's one worker thread; picker results arrive from
// the activity and only touch the pending pickers and labels under their lock. A picked file is a descriptor
// the activity holds (/proc/self/fd/<n>); a picked directory is a SAF handle whose files are listed and opened
// through the bridge, from whichever thread imports them.
#include <android/log.h>
#include <jni.h>
#include <unistd.h>

#include <cerrno>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "frontend/gui/launcher_services.h"
#include "frontend/gui/rpc.h"
#include "ogplay/app/archive_import.h"
#include "ogplay/app/host.h"
#include "ogplay/app/json.h"
#include "ogplay/app/library.h"
#include "ogplay/app/logging.h"
#include "ogplay/app/run_report.h"

namespace {
using namespace ogplay;
using namespace ogplay::frontend;

constexpr const char* kLogTag = "OGPlay";
// A game that never takes its run lock nor rewrites its report has failed before RunGame began.
constexpr auto kStartupWindow = std::chrono::seconds(30);

std::filesystem::path Utf8Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string FromJava(JNIEnv* env, jbyteArray value) {
    const auto length = env->GetArrayLength(value);
    std::string text(static_cast<std::size_t>(length), '\0');
    env->GetByteArrayRegion(value, 0, length, reinterpret_cast<jbyte*>(text.data()));
    return text;
}

// JNI strings are modified UTF-8; RPC text and paths are real UTF-8, so they cross as byte arrays.
jstring ToJava(JNIEnv* env, std::string_view text) {
    const auto bytes = env->NewByteArray(static_cast<jsize>(text.size()));
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte*>(text.data()));
    const auto string_class = env->FindClass("java/lang/String");
    const auto constructor = env->GetMethodID(string_class, "<init>", "([BLjava/lang/String;)V");
    const auto charset = env->NewStringUTF("UTF-8");
    const auto result = static_cast<jstring>(env->NewObject(string_class, constructor, bytes, charset));
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(string_class);
    env->DeleteLocalRef(bytes);
    return result;
}

std::string FailureResponse(std::string_view message) {
    app::JsonWriter writer;
    const auto root = writer.Object(), error = writer.Object();
    writer.AddString(root, "jsonrpc", "2.0");
    writer.AddNull(root, "id");
    writer.AddInteger(error, "code", -32000);
    writer.AddString(error, "message", message);
    writer.AddString(error, "next_step", "检查 GUI 日志和前端制品后重试。");
    writer.Add(root, "error", error);
    return writer.Serialize(root);
}

// Import threads are native: attached to the VM on first use and detached when they end.
struct ThreadAttachment final {
    JavaVM* vm{};
    ~ThreadAttachment() {
        if (vm) vm->DetachCurrentThread();
    }
};

class Launcher;

// A picked SAF directory as an import source: listed once, each file opened by the activity as a
// descriptor and copied from it.
class SafTreeSource final : public app::ImportSource {
public:
    SafTreeSource(Launcher& launcher, std::string handle, std::vector<app::ImportFile> files)
        : launcher_(launcher), handle_(std::move(handle)), files_(std::move(files)) {}
    const std::vector<app::ImportFile>& Files() const override { return files_; }
    void Extract(std::span<const app::ImportExtraction> files, app::ImportProgress* progress) override;

private:
    Launcher& launcher_;
    std::string handle_;
    std::vector<app::ImportFile> files_;
};

class Launcher final {
public:
    Launcher(JNIEnv* env, jobject bridge)
        : bridge_(env->NewGlobalRef(bridge)), logger_(app::LogConfig{.stderr_enabled = false}) {
        env->GetJavaVM(&vm_);
        const auto bridge_class = env->GetObjectClass(bridge);
        launch_method_ = env->GetMethodID(bridge_class, "launchGame", "([Ljava/lang/String;)V");
        pick_method_ = env->GetMethodID(bridge_class, "pickPath", "(JZ)V");
        list_tree_method_ = env->GetMethodID(bridge_class, "listTree", "([B)[B");
        open_tree_file_method_ = env->GetMethodID(bridge_class, "openTreeFile", "([B[B)I");
        resume_method_ = env->GetMethodID(bridge_class, "resumeGame", "()V");
        stop_method_ = env->GetMethodID(bridge_class, "stopGame", "()V");
        open_url_method_ = env->GetMethodID(bridge_class, "openUrl", "(Ljava/lang/String;)V");
        env->DeleteLocalRef(bridge_class);
        if (!launch_method_ || !pick_method_ || !list_tree_method_ || !open_tree_file_method_ || !resume_method_ ||
            !stop_method_ || !open_url_method_)
            throw std::runtime_error("LauncherBridge callbacks are missing");

        const auto root = app::DefaultLibraryRoot();
        std::filesystem::create_directories(root);
        std::ofstream(root / "gui.log", std::ios::binary | std::ios::trunc);
        logger_.SetFile(root / "gui.log");
        store_ = std::make_unique<app::LibraryStore>(root);
        services_ = std::make_unique<LauncherServices>(*store_, logger_);
        auto host = services_->Host([this] {
            std::vector<std::string> running;
            for (const auto& game : tracked_)
                running.push_back(game.package);
            return running;
        });
        host.launch = [this](const LaunchPlan& plan) { Launch(plan); };
        // The game runs in its own task: back to its screen, or asked to quit as its close confirmation would.
        host.resume = [this](std::string_view) { CallGame(resume_method_, "无法返回游戏界面。"); };
        host.stop = [this](std::string_view package) {
            CallGame(stop_method_, "无法关闭游戏。");
            logger_.Write(app::LogLevel::info, "frontend.android.launcher", "game asked to quit", {},
                          {{"installation_id", std::string(package)}});
        };
        host.open_url = [this](std::string_view url) {
            JNIEnv* env = Env();
            const auto text = env->NewStringUTF(std::string(url).c_str());
            env->CallVoidMethod(bridge_, open_url_method_, text);
            env->DeleteLocalRef(text);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                throw std::runtime_error("无法打开网页。");
            }
        };
        // Never offered here: open_directory (no file manager), minimize, Dashboard and key editor windows.
        host.open_directory = [](const std::filesystem::path&) {
            throw std::runtime_error("Android 启动器不打开宿主目录。");
        };
        host.pick = [this](bool directory) { return Pick(directory); };
        host.label = [this](const std::filesystem::path& path) {
            std::lock_guard lock(picks_mutex_);
            const auto found = labels_.find(PathUtf8(path));
            return found == labels_.end() ? std::string{} : found->second;
        };
        host.open_tree = [this](const std::filesystem::path& path) -> std::unique_ptr<app::ImportSource> {
            const auto handle = PathUtf8(path);
            if (!handle.starts_with("saf-tree:")) return nullptr;
            return std::make_unique<SafTreeSource>(*this, handle, ListTree(handle));
        };
        rpc_ =
            std::make_unique<GuiRpcService>(*store_, std::filesystem::path{}, std::move(host), GuiPlatform::Android());
        logger_.Write(app::LogLevel::info, "frontend.android.launcher", "launcher ready", {},
                      {{"library_root", PathUtf8(root)}});
    }

    ~Launcher() {
        {
            std::lock_guard lock(picks_mutex_);
            for (auto& [token, promise] : picks_)
                promise.set_value(std::nullopt);
            picks_.clear();
        }
        rpc_.reset();
        services_.reset();
        store_.reset();
        if (JNIEnv* env = Env()) env->DeleteGlobalRef(bridge_);
    }

    std::string Handle(std::string_view request) {
        const auto response = rpc_->Handle(request);
        app::JsonParseError error;
        const auto decoded =
            app::JsonDocument::ParseStrict(response, error, app::JsonRpcDispatcher::kMaximumResponseBytes);
        if (decoded && decoded->Root().Member("error"))
            logger_.Write(app::LogLevel::error, "frontend.gui.rpc", "RPC request failed", {}, {{"response", response}});
        return response;
    }

    // Games that ended since the last call, as the page's ogplay-exit event details.
    std::string Poll() {
        app::JsonWriter writer;
        const auto events = writer.Array();
        const auto now = std::chrono::steady_clock::now();
        for (auto game = tracked_.begin(); game != tracked_.end();) {
            const bool running = store_->IsRunning(game->package);
            game->seen_running = game->seen_running || running;
            const auto report = app::LoadLastRunReport(game->report_path);
            const bool rewritten = report != game->report_before;
            if (running || (!game->seen_running && !rewritten && now - game->launched < kStartupWindow)) {
                ++game;
                continue;
            }
            // An unfinished rewritten report while nothing runs: the process died mid-run.
            const auto code = rewritten && report ? report->exit_code.value_or(report->finished ? 0 : 1) : 1;
            const auto event = writer.Object();
            writer.AddString(event, "installation_id", game->package);
            writer.AddInteger(event, "exit_code", code);
            writer.AddString(event, "log_tail", code == 0 ? "" : ReadLogTail(game->log_path));
            writer.Append(events, event);
            logger_.Write(app::LogLevel::info, "frontend.android.launcher", "game ended", {},
                          {{"installation_id", game->package}, {"exit_code", static_cast<std::int64_t>(code)}});
            game = tracked_.erase(game);
        }
        return writer.Serialize(events);
    }

    void PickResult(jlong token, std::optional<std::filesystem::path> path, std::string label = {}) {
        std::lock_guard lock(picks_mutex_);
        if (path && !label.empty()) labels_[PathUtf8(*path)] = std::move(label);
        const auto found = picks_.find(token);
        if (found == picks_.end()) return;
        found->second.set_value(std::move(path));
        picks_.erase(found);
    }

    // A detached descriptor for one file of a picked directory, owned by the caller; -1 when it cannot be opened.
    int OpenTreeFile(const std::string& handle, const std::string& path) {
        JNIEnv* env = AttachedEnv();
        const auto handle_bytes = Bytes(env, handle), path_bytes = Bytes(env, path);
        const auto descriptor = env->CallIntMethod(bridge_, open_tree_file_method_, handle_bytes, path_bytes);
        env->DeleteLocalRef(handle_bytes);
        env->DeleteLocalRef(path_bytes);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return -1;
        }
        return descriptor;
    }

private:
    static jbyteArray Bytes(JNIEnv* env, std::string_view text) {
        const auto bytes = env->NewByteArray(static_cast<jsize>(text.size()));
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(text.size()), reinterpret_cast<const jbyte*>(text.data()));
        return bytes;
    }

    // The calling thread's JNI environment, attaching a native import thread on first use.
    JNIEnv* AttachedEnv() const {
        JNIEnv* env{};
        if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
        if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) throw std::runtime_error("cannot attach import thread");
        thread_local ThreadAttachment attachment;
        attachment.vm = vm_;
        return env;
    }

    std::vector<app::ImportFile> ListTree(const std::string& handle) {
        JNIEnv* env = AttachedEnv();
        const auto handle_bytes = Bytes(env, handle);
        const auto listing = static_cast<jbyteArray>(env->CallObjectMethod(bridge_, list_tree_method_, handle_bytes));
        env->DeleteLocalRef(handle_bytes);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            throw std::runtime_error("无法读取所选目录。");
        }
        if (!listing) throw std::runtime_error("无法读取所选目录。");
        const auto text = FromJava(env, listing);
        env->DeleteLocalRef(listing);
        std::vector<app::ImportFile> files;
        for (std::size_t start = 0; start < text.size();) {
            const auto end = std::min(text.find('\n', start), text.size());
            const auto line = std::string_view(text).substr(start, end - start);
            start = end + 1;
            const auto tab = line.find('\t');
            if (tab == std::string_view::npos) continue;
            const auto path = app::NormalizeArchivePath(line.substr(tab + 1));
            if (!path || path->empty()) continue;
            files.push_back({*path, std::stoull(std::string(line.substr(0, tab)))});
        }
        return files;
    }

    struct Tracked final {
        std::string package;
        std::filesystem::path log_path;
        std::filesystem::path report_path;
        std::optional<app::LastRunReport> report_before;
        std::chrono::steady_clock::time_point launched;
        bool seen_running{};
    };

    JNIEnv* Env() const {
        JNIEnv* env{};
        if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return nullptr;
        return env;
    }

    void CallGame(jmethodID method, const char* failure) {
        JNIEnv* env = Env();
        env->CallVoidMethod(bridge_, method);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            throw std::runtime_error(failure);
        }
    }

    void Launch(const LaunchPlan& plan) {
        // One :game process: starting its activity again would only bring the running game back, whichever was asked
        // for. The store's running lease also covers a game this launcher process did not start.
        for (const auto& game : tracked_)
            if (game.package == plan.package) throw std::runtime_error("游戏正在运行，退出后再启动。");
        for (const auto& entry : store_->LoadEntries())
            if (store_->IsRunning(entry.key)) throw std::runtime_error("已有游戏在运行，返回该游戏或先关闭它。");
        JNIEnv* env = Env();
        const auto string_class = env->FindClass("java/lang/String");
        const auto argv = env->NewObjectArray(static_cast<jsize>(plan.argv.size()), string_class, nullptr);
        for (std::size_t index = 0; index < plan.argv.size(); ++index) {
            const auto value = ToJava(env, plan.argv[index]);
            env->SetObjectArrayElement(argv, static_cast<jsize>(index), value);
            env->DeleteLocalRef(value);
        }
        const auto report_path = plan.log_path.parent_path() / "last-run.json";
        Tracked game{plan.package, plan.log_path, report_path, app::LoadLastRunReport(report_path),
                     std::chrono::steady_clock::now()};
        env->CallVoidMethod(bridge_, launch_method_, argv);
        env->DeleteLocalRef(argv);
        env->DeleteLocalRef(string_class);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            throw std::runtime_error("无法启动游戏界面。");
        }
        tracked_.push_back(std::move(game));
        logger_.Write(app::LogLevel::info, "frontend.android.launcher", "game launched", {},
                      {{"installation_id", plan.package}});
    }

    std::future<std::optional<std::filesystem::path>> Pick(bool directory) {
        std::future<std::optional<std::filesystem::path>> result;
        jlong token{};
        {
            std::lock_guard lock(picks_mutex_);
            token = ++next_pick_;
            result = picks_[token].get_future();
        }
        JNIEnv* env = Env();
        env->CallVoidMethod(bridge_, pick_method_, token, static_cast<jboolean>(directory));
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            PickResult(token, std::nullopt);
        }
        return result;
    }

    JavaVM* vm_{};
    jobject bridge_{};
    jmethodID launch_method_{};
    jmethodID pick_method_{};
    jmethodID list_tree_method_{};
    jmethodID open_tree_file_method_{};
    jmethodID resume_method_{};
    jmethodID stop_method_{};
    jmethodID open_url_method_{};
    app::Logger logger_;
    std::unique_ptr<app::LibraryStore> store_;
    std::unique_ptr<LauncherServices> services_;
    std::unique_ptr<GuiRpcService> rpc_;
    std::vector<Tracked> tracked_;
    std::mutex picks_mutex_;
    std::map<jlong, std::promise<std::optional<std::filesystem::path>>> picks_;
    std::map<std::string, std::string> labels_;   // picked path or handle -> what the page shows
    jlong next_pick_{};
};

void SafTreeSource::Extract(std::span<const app::ImportExtraction> files, app::ImportProgress* progress) {
    for (const auto& item : files) {
        const auto& file = files_.at(item.index);
        const int descriptor = launcher_.OpenTreeFile(handle_, file.path);
        if (descriptor < 0)
            throw app::LibraryError(app::LibraryErrorCode::io_error, "无法读取所选目录中的文件：" + file.path);
        struct Closer final {
            int fd;
            ~Closer() { ::close(fd); }
        } closer{descriptor};
        app::WriteImportFile(item.destination, file.size, [&](std::span<std::byte> buffer) -> std::size_t {
            for (;;) {
                const auto count = ::read(descriptor, buffer.data(), buffer.size());
                if (count >= 0) return static_cast<std::size_t>(count);
                if (errno != EINTR)
                    throw app::LibraryError(app::LibraryErrorCode::io_error, "无法读取所选目录中的文件：" + file.path);
            }
        }, progress);
    }
}

Launcher* FromHandle(jlong handle) {
    return reinterpret_cast<Launcher*>(static_cast<std::intptr_t>(handle));
}

void ThrowJava(JNIEnv* env, const char* message) {
    const auto type = env->FindClass("java/lang/IllegalStateException");
    env->ThrowNew(type, message);
    env->DeleteLocalRef(type);
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_org_poping520_ogplay_launcher_LauncherBridge_nativeCreate(JNIEnv* env, jclass,
                                                                                             jobject bridge) {
    try {
        return static_cast<jlong>(reinterpret_cast<std::intptr_t>(new Launcher(env, bridge)));
    } catch (const std::exception& error) {
        __android_log_write(ANDROID_LOG_ERROR, kLogTag, error.what());
        ThrowJava(env, error.what());
    }
    return 0;
}

JNIEXPORT jstring JNICALL Java_org_poping520_ogplay_launcher_LauncherBridge_nativeHandle(JNIEnv* env, jclass,
                                                                                               jlong handle,
                                                                                               jbyteArray request) {
    const auto text = FromJava(env, request);
    try {
        return ToJava(env, FromHandle(handle)->Handle(text));
    } catch (const std::exception& error) {
        __android_log_write(ANDROID_LOG_ERROR, kLogTag, error.what());
        return ToJava(env, FailureResponse(error.what()));
    }
}

JNIEXPORT jstring JNICALL Java_org_poping520_ogplay_launcher_LauncherBridge_nativePoll(JNIEnv* env, jclass,
                                                                                             jlong handle) {
    try {
        return ToJava(env, FromHandle(handle)->Poll());
    } catch (const std::exception& error) {
        __android_log_write(ANDROID_LOG_ERROR, kLogTag, error.what());
        return ToJava(env, "[]");
    }
}

JNIEXPORT void JNICALL Java_org_poping520_ogplay_launcher_LauncherBridge_nativePickResult(JNIEnv* env, jclass,
                                                                                                jlong handle,
                                                                                                jlong token,
                                                                                                jbyteArray path,
                                                                                                jbyteArray label) {
    std::optional<std::filesystem::path> picked;
    if (path) picked = Utf8Path(FromJava(env, path));
    FromHandle(handle)->PickResult(token, std::move(picked), label ? FromJava(env, label) : std::string{});
}

JNIEXPORT void JNICALL Java_org_poping520_ogplay_launcher_LauncherBridge_nativeDestroy(JNIEnv*, jclass,
                                                                                             jlong handle) {
    delete FromHandle(handle);
}

}  // extern "C"
