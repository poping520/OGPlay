#include "ogplay/hal/diagnostic_trigger.h"

#include <csignal>
#include <fcntl.h>
#include <stdexcept>
#include <utility>
#include <sys/syscall.h>
#include <unistd.h>

namespace ogplay::hal {
namespace {

volatile std::sig_atomic_t g_diagnostic_pipe{-1};

extern "C" void HandleDiagnosticSignal(int) {
    const auto descriptor = g_diagnostic_pipe;
    if (descriptor < 0) return;
    const unsigned char byte = 1U;
    static_cast<void>(write(descriptor, &byte, 1U));
}

}  // namespace

class DiagnosticTrigger::Backend final {
public:
    explicit Backend(const std::uint64_t process_id)
        : name_("SIGUSR1:" + std::to_string(process_id)) {
        int descriptors[2]{};
        if (pipe(descriptors) != 0) {
            throw std::runtime_error("cannot create diagnostic signal pipe");
        }
        read_ = descriptors[0];
        write_ = descriptors[1];
        const auto flags = fcntl(write_, F_GETFL, 0);
        if (flags < 0 || fcntl(write_, F_SETFL, flags | O_NONBLOCK) != 0) {
            close(read_);
            close(write_);
            read_ = -1;
            write_ = -1;
            throw std::runtime_error("cannot configure diagnostic signal pipe");
        }
        if (g_diagnostic_pipe != -1) {
            close(read_);
            close(write_);
            read_ = -1;
            write_ = -1;
            throw std::runtime_error("diagnostic signal handler is already active");
        }
        g_diagnostic_pipe = write_;
        struct sigaction action{};
        action.sa_handler = &HandleDiagnosticSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        if (sigaction(SIGUSR1, &action, &previous_) != 0) {
            g_diagnostic_pipe = -1;
            close(read_);
            close(write_);
            read_ = -1;
            write_ = -1;
            throw std::runtime_error("cannot install diagnostic signal handler");
        }
        installed_ = true;
    }

    ~Backend() {
        g_diagnostic_pipe = -1;
        if (installed_) {
            static_cast<void>(sigaction(SIGUSR1, &previous_, nullptr));
        }
        if (read_ >= 0) close(read_);
        if (write_ >= 0) close(write_);
    }
    [[nodiscard]] std::string Name() const { return name_; }
    [[nodiscard]] DiagnosticWaitResult Wait() noexcept {
        unsigned char byte{};
        if (read(read_, &byte, 1U) != 1) return DiagnosticWaitResult::failed;
        return byte == 0U ? DiagnosticWaitResult::stopped
                          : DiagnosticWaitResult::triggered;
    }
    void Stop() noexcept {
        if (write_ < 0) return;
        const unsigned char byte{};
        static_cast<void>(write(write_, &byte, 1U));
    }

private:
    std::string name_;
    int read_{-1};
    int write_{-1};
    struct sigaction previous_{};
    bool installed_{};
};

DiagnosticTrigger::DiagnosticTrigger(std::unique_ptr<Backend> backend) noexcept
    : backend_(std::move(backend)) {}
DiagnosticTrigger::~DiagnosticTrigger() = default;
std::string DiagnosticTrigger::Name() const { return backend_->Name(); }
DiagnosticWaitResult DiagnosticTrigger::Wait() noexcept {
    return backend_->Wait();
}
void DiagnosticTrigger::Stop() noexcept { backend_->Stop(); }

std::unique_ptr<DiagnosticTrigger> CreateDiagnosticTrigger(
    const std::uint64_t process_id, const std::string_view) {
    return std::make_unique<DiagnosticTrigger>(
        std::make_unique<DiagnosticTrigger::Backend>(process_id));
}
void SignalDiagnosticTrigger(const std::uint64_t process_id,
                             const std::string_view) {
    if (kill(static_cast<pid_t>(process_id), SIGUSR1) != 0) {
        throw std::runtime_error("cannot signal diagnostic process");
    }
}
void RestrictDiagnosticFile(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::permissions(
        path, std::filesystem::perms::owner_read |
                  std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, error);
    if (error) {
        throw std::runtime_error("cannot restrict diagnostic output: " +
                                 error.message());
    }
}
std::uint64_t HostProcessId() noexcept {
    return static_cast<std::uint64_t>(getpid());
}
std::uint64_t CurrentNativeThreadId() noexcept {
    return static_cast<std::uint64_t>(syscall(SYS_gettid));
}

}  // namespace ogplay::hal
