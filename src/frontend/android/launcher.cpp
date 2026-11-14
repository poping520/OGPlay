// Android launcher: the WebView page's rpc bridge into GuiRpcService, entered from LauncherBridge.java.
// Every Handle and Poll call arrives on the bridge's one worker thread; picker results arrive from
// the copy thread and only touch the pending pickers under their lock.
#include <android/log.h>
#include <jni.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "frontend/gui/launcher_services.h"
#include "frontend/gui/rpc.h"
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

class Launcher final {
public:
    Launcher(JNIEnv* env, jobject bridge)
        : bridge_(env->NewGlobalRef(bridge)), logger_(app::LogConfig{.stderr_enabled = false}) {
        env->GetJavaVM(&vm_);
        const auto bridge_class = env->GetObjectClass(bridge);
        launch_method_ = env->GetMethodID(bridge_class, "launchGame", "([Ljava/lang/String;)V");
        pick_method_ = env->GetMethodID(bridge_class, "pickPath", "(JZ)V");
        env->DeleteLocalRef(bridge_class);
        if (!launch_method_ || !pick_method_) throw std::runtime_error("LauncherBridge callbacks are missing");

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
        // Never offered here: open_directory (no file manager), minimize, Dashboard and key editor windows.
        host.open_directory = [](const std::filesystem::path&) {
            throw std::runtime_error("Android 启动器不打开宿主目录。");
        };
        host.pick = [this](bool directory) { return Pick(directory); };
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

    void PickResult(jlong token, std::optional<std::filesystem::path> path) {
        std::lock_guard lock(picks_mutex_);
        const auto found = picks_.find(token);
        if (found == picks_.end()) return;
        found->second.set_value(std::move(path));
        picks_.erase(found);
    }

private:
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

    void Launch(const LaunchPlan& plan) {
        for (const auto& game : tracked_)
            if (game.package == plan.package) throw std::runtime_error("游戏正在运行，退出后再启动。");
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
    app::Logger logger_;
    std::unique_ptr<app::LibraryStore> store_;
    std::unique_ptr<LauncherServices> services_;
    std::unique_ptr<GuiRpcService> rpc_;
    std::vector<Tracked> tracked_;
    std::mutex picks_mutex_;
    std::map<jlong, std::promise<std::optional<std::filesystem::path>>> picks_;
    jlong next_pick_{};
};

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

JNIEXPORT jlong JNICALL Java_io_github_poping520_ogplay_launcher_LauncherBridge_nativeCreate(JNIEnv* env, jclass,
                                                                                             jobject bridge) {
    try {
        return static_cast<jlong>(reinterpret_cast<std::intptr_t>(new Launcher(env, bridge)));
    } catch (const std::exception& error) {
        __android_log_write(ANDROID_LOG_ERROR, kLogTag, error.what());
        ThrowJava(env, error.what());
    }
    return 0;
}

JNIEXPORT jstring JNICALL Java_io_github_poping520_ogplay_launcher_LauncherBridge_nativeHandle(JNIEnv* env, jclass,
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

JNIEXPORT jstring JNICALL Java_io_github_poping520_ogplay_launcher_LauncherBridge_nativePoll(JNIEnv* env, jclass,
                                                                                             jlong handle) {
    try {
        return ToJava(env, FromHandle(handle)->Poll());
    } catch (const std::exception& error) {
        __android_log_write(ANDROID_LOG_ERROR, kLogTag, error.what());
        return ToJava(env, "[]");
    }
}

JNIEXPORT void JNICALL Java_io_github_poping520_ogplay_launcher_LauncherBridge_nativePickResult(JNIEnv* env, jclass,
                                                                                                jlong handle,
                                                                                                jlong token,
                                                                                                jbyteArray path) {
    std::optional<std::filesystem::path> picked;
    if (path) picked = Utf8Path(FromJava(env, path));
    FromHandle(handle)->PickResult(token, std::move(picked));
}

JNIEXPORT void JNICALL Java_io_github_poping520_ogplay_launcher_LauncherBridge_nativeDestroy(JNIEnv*, jclass,
                                                                                             jlong handle) {
    delete FromHandle(handle);
}

}  // extern "C"
