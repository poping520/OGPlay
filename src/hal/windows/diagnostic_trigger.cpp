#define NOMINMAX
#include <Windows.h>
#include <Aclapi.h>

#include "ogplay/hal/diagnostic_trigger.h"

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ogplay::hal {
namespace {

class CurrentUserSecurity final {
public:
    CurrentUserSecurity() {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_)) {
            throw std::runtime_error("cannot open diagnostic security token");
        }
        DWORD bytes{};
        static_cast<void>(GetTokenInformation(
            token_, TokenUser, nullptr, 0U, &bytes));
        token_user_.resize(bytes);
        if (bytes == 0U || !GetTokenInformation(
                token_, TokenUser, token_user_.data(), bytes, &bytes)) {
            CloseHandle(token_);
            token_ = nullptr;
            throw std::runtime_error("cannot read diagnostic security token");
        }
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = GENERIC_ALL;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(Sid());
        if (SetEntriesInAclW(1U, &access, nullptr, &acl_) != ERROR_SUCCESS ||
            !InitializeSecurityDescriptor(&descriptor_,
                                          SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorDacl(&descriptor_, TRUE, acl_, FALSE)) {
            if (acl_ != nullptr) {
                LocalFree(acl_);
                acl_ = nullptr;
            }
            CloseHandle(token_);
            token_ = nullptr;
            throw std::runtime_error("cannot create diagnostic security ACL");
        }
        attributes_.nLength = sizeof(attributes_);
        attributes_.lpSecurityDescriptor = &descriptor_;
    }

    ~CurrentUserSecurity() {
        if (acl_ != nullptr) LocalFree(acl_);
        if (token_ != nullptr) CloseHandle(token_);
    }
    [[nodiscard]] SECURITY_ATTRIBUTES* Attributes() noexcept {
        return &attributes_;
    }
    [[nodiscard]] PSID Sid() noexcept {
        return reinterpret_cast<TOKEN_USER*>(token_user_.data())->User.Sid;
    }
    [[nodiscard]] PACL Acl() const noexcept { return acl_; }

private:
    HANDLE token_{};
    std::vector<std::byte> token_user_;
    PACL acl_{};
    SECURITY_DESCRIPTOR descriptor_{};
    SECURITY_ATTRIBUTES attributes_{};
};

[[nodiscard]] std::wstring Wide(const std::string_view text) {
    return {text.begin(), text.end()};
}

}  // namespace

class DiagnosticTrigger::Backend final {
public:
    Backend(const std::uint64_t process_id, const std::string_view nonce)
        : name_("Local\\ogplay-diag-" + std::to_string(process_id) + "-" +
                std::string(nonce)) {
        CurrentUserSecurity security;
        const auto name = Wide(name_);
        trigger_ = CreateEventW(
            security.Attributes(), FALSE, FALSE, name.c_str());
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (trigger_ == nullptr || stop_ == nullptr) {
            if (trigger_ != nullptr) CloseHandle(trigger_);
            if (stop_ != nullptr) CloseHandle(stop_);
            trigger_ = nullptr;
            stop_ = nullptr;
            throw std::runtime_error("cannot create diagnostic event");
        }
    }

    ~Backend() {
        if (trigger_ != nullptr) CloseHandle(trigger_);
        if (stop_ != nullptr) CloseHandle(stop_);
    }
    [[nodiscard]] std::string Name() const { return name_; }
    [[nodiscard]] DiagnosticWaitResult Wait() noexcept {
        const HANDLE events[]{stop_, trigger_};
        const auto result = WaitForMultipleObjects(2U, events, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) return DiagnosticWaitResult::stopped;
        if (result == WAIT_OBJECT_0 + 1U) {
            return DiagnosticWaitResult::triggered;
        }
        return DiagnosticWaitResult::failed;
    }
    void Stop() noexcept {
        if (stop_ != nullptr) static_cast<void>(SetEvent(stop_));
    }

private:
    std::string name_;
    HANDLE trigger_{};
    HANDLE stop_{};
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
    const std::uint64_t process_id, const std::string_view nonce) {
    return std::make_unique<DiagnosticTrigger>(
        std::make_unique<DiagnosticTrigger::Backend>(process_id, nonce));
}

void SignalDiagnosticTrigger(const std::uint64_t,
                             const std::string_view trigger_name) {
    const auto name = Wide(trigger_name);
    const auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (event == nullptr) throw std::runtime_error("cannot open diagnostic event");
    const auto signalled = SetEvent(event) != FALSE;
    CloseHandle(event);
    if (!signalled) throw std::runtime_error("cannot signal diagnostic event");
}

void RestrictDiagnosticFile(const std::filesystem::path& path) {
    CurrentUserSecurity security;
    const auto result = SetNamedSecurityInfoW(
        const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, security.Acl(), nullptr);
    if (result != ERROR_SUCCESS) {
        throw std::runtime_error("cannot restrict diagnostic output ACL");
    }
}

std::uint64_t HostProcessId() noexcept { return GetCurrentProcessId(); }
std::uint64_t CurrentNativeThreadId() noexcept { return GetCurrentThreadId(); }

}  // namespace ogplay::hal
