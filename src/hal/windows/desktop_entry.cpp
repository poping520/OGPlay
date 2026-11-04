#include "app_entry_args.h"
#include "launcher_instance.h"
#include <string_view>

namespace {
class LauncherInstance final {
public:
    LauncherInstance() {
        mutex_ = CreateMutexW(nullptr, TRUE, ogplay::hal::windows::kLauncherMutex);
        if (!mutex_) throw std::runtime_error("cannot create OGPlay launcher instance mutex");
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            owned_ = true;
            return;
        }
        // A previous owner may have exited while this process was starting.
        const auto result = WaitForSingleObject(mutex_, 0);
        owned_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        if (result == WAIT_FAILED) {
            CloseHandle(mutex_);
            throw std::runtime_error("cannot acquire OGPlay launcher instance mutex");
        }
    }

    ~LauncherInstance() {
        if (owned_) ReleaseMutex(mutex_);
        CloseHandle(mutex_);
    }

    LauncherInstance(const LauncherInstance&) = delete;
    LauncherInstance& operator=(const LauncherInstance&) = delete;

    bool IsOwner() const { return owned_; }

    static void ActivateExisting() {
        HWND launcher{};
        EnumWindows(
            [](HWND window, LPARAM context) -> BOOL {
                if (!GetPropW(window, ogplay::hal::windows::kLauncherWindowProperty)) return TRUE;
                *reinterpret_cast<HWND*>(context) = window;
                return FALSE;
            },
            reinterpret_cast<LPARAM>(&launcher));
        // During initial startup there may be no window yet; the owner will reveal it after navigation.
        if (!launcher) return;
        DWORD process{};
        GetWindowThreadProcessId(launcher, &process);
        AllowSetForegroundWindow(process);
        const auto message = RegisterWindowMessageW(ogplay::hal::windows::kLauncherActivateMessage);
        if (!message || !PostMessageW(launcher, message, 0, 0))
            throw std::runtime_error("cannot activate the existing OGPlay launcher");
    }

private:
    HANDLE mutex_{};
    bool owned_{};
};
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return ForwardApplication([](int argc, const char* const argv[]) {
        if ((argc >= 2 && std::string_view(argv[1]) == "--run-game") ||
            (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "--version")))
            return OgplayMain(argc, argv);
        const LauncherInstance instance;
        if (!instance.IsOwner()) {
            LauncherInstance::ActivateExisting();
            return 0;
        }
        return OgplayMain(argc, argv);
    });
}
