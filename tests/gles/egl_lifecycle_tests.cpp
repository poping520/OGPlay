#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/gles/egl_lifecycle.h"

namespace {

class FakeEglApi final : public ogplay::gles::EglApi {
public:
    enum class FailAt {
        none,
        display,
        initialize,
        config,
        bind,
        context,
        surface,
        current,
    };

    explicit FakeEglApi(const FailAt fail_at = FailAt::none)
        : fail_at_(fail_at) {}

    ogplay::gles::EglHandle GetPlatformDisplay(
        const ogplay::gles::AngleBackend) override {
        calls.emplace_back("display");
        return fail_at_ == FailAt::display ? 0 : kDisplay;
    }

    bool Initialize(const ogplay::gles::EglHandle, int& major,
                    int& minor) override {
        calls.emplace_back("initialize");
        major = 1;
        minor = 5;
        return fail_at_ != FailAt::initialize;
    }

    bool ChoosePbufferConfig(const ogplay::gles::EglHandle,
                             ogplay::gles::EglHandle& config) override {
        calls.emplace_back("config");
        config = fail_at_ == FailAt::config ? 0 : kConfig;
        return fail_at_ != FailAt::config;
    }

    bool BindOpenGlesApi() override {
        calls.emplace_back("bind");
        return fail_at_ != FailAt::bind;
    }

    ogplay::gles::EglHandle CreateContext(
        const ogplay::gles::EglHandle, const ogplay::gles::EglHandle,
        const int) override {
        calls.emplace_back("context");
        return fail_at_ == FailAt::context ? 0 : kContext;
    }

    ogplay::gles::EglHandle CreatePbufferSurface(
        const ogplay::gles::EglHandle, const ogplay::gles::EglHandle,
        const std::uint32_t, const std::uint32_t) override {
        calls.emplace_back("surface");
        return fail_at_ == FailAt::surface ? 0 : kSurface;
    }

    bool MakeCurrent(const ogplay::gles::EglHandle,
                     const ogplay::gles::EglHandle draw,
                     const ogplay::gles::EglHandle,
                     const ogplay::gles::EglHandle) override {
        calls.emplace_back(draw == 0 ? "unbind" : "current");
        return draw == 0 || fail_at_ != FailAt::current;
    }

    bool DestroySurface(const ogplay::gles::EglHandle,
                        const ogplay::gles::EglHandle) override {
        calls.emplace_back("destroy_surface");
        return true;
    }

    bool DestroyContext(const ogplay::gles::EglHandle,
                        const ogplay::gles::EglHandle) override {
        calls.emplace_back("destroy_context");
        return true;
    }

    bool Terminate(const ogplay::gles::EglHandle) override {
        calls.emplace_back("terminate");
        return true;
    }

    std::uint32_t GetError() override {
        calls.emplace_back("error");
        return kError;
    }

    static constexpr ogplay::gles::EglHandle kDisplay = 10;
    static constexpr ogplay::gles::EglHandle kConfig = 20;
    static constexpr ogplay::gles::EglHandle kContext = 30;
    static constexpr ogplay::gles::EglHandle kSurface = 40;
    static constexpr std::uint32_t kError = 0x3001;
    std::vector<std::string> calls;

private:
    FailAt fail_at_;
};

constexpr ogplay::gles::AngleBackend kBackend{
    ogplay::gles::AngleRenderer::d3d11,
    ogplay::gles::AngleDevice::hardware};

#if defined(_WIN32)
constexpr auto kNativeBackend = kBackend;
#elif defined(__APPLE__)
constexpr ogplay::gles::AngleBackend kNativeBackend{
    ogplay::gles::AngleRenderer::metal,
    ogplay::gles::AngleDevice::hardware};
#else
constexpr ogplay::gles::AngleBackend kNativeBackend{
    ogplay::gles::AngleRenderer::vulkan,
    ogplay::gles::AngleDevice::hardware};
#endif

}  // namespace

TEST_CASE("EGL lifecycle creates and destroys a current GLES2 pbuffer") {
    FakeEglApi api;
    {
        auto lifecycle = ogplay::gles::EglLifecycle::CreatePbuffer(
            api, kBackend, 64, 32);
        CHECK(lifecycle.IsCurrent());
        CHECK(lifecycle.Info() == ogplay::gles::EglContextInfo{
                                      kBackend, 1, 5, 2, 64, 32});
        CHECK(api.calls == std::vector<std::string>{
                               "display", "initialize", "config", "bind",
                               "context", "surface", "current"});
    }
    CHECK(api.calls == std::vector<std::string>{
                           "display", "initialize", "config", "bind",
                           "context", "surface", "current", "unbind",
                           "destroy_surface", "destroy_context", "terminate"});
}

TEST_CASE("EGL lifecycle move transfers sole cleanup responsibility") {
    FakeEglApi api;
    {
        auto first = ogplay::gles::EglLifecycle::CreatePbuffer(
            api, kBackend, 4, 4);
        auto second = std::move(first);
        CHECK_FALSE(first.IsCurrent());
        CHECK(second.IsCurrent());
    }
    CHECK(std::count(api.calls.begin(), api.calls.end(), "terminate") == 1);
}

TEST_CASE("EGL lifecycle rejects invalid dimensions before native calls") {
    FakeEglApi api;
    CHECK_THROWS_AS(ogplay::gles::EglLifecycle::CreatePbuffer(
                        api, kBackend, 0, 32),
                    std::invalid_argument);
    CHECK_THROWS_AS(ogplay::gles::EglLifecycle::CreatePbuffer(
                        api, kBackend,
                        static_cast<std::uint32_t>(
                            std::numeric_limits<int>::max()) +
                            1U,
                        32),
                    std::invalid_argument);
    CHECK(api.calls.empty());
}

TEST_CASE("EGL lifecycle reports native failure and unwinds partial state") {
    using FailAt = FakeEglApi::FailAt;
    using Operation = ogplay::gles::EglOperation;
    const std::vector<std::pair<FailAt, Operation>> failures{
        {FailAt::display, Operation::get_platform_display},
        {FailAt::initialize, Operation::initialize},
        {FailAt::config, Operation::choose_config},
        {FailAt::bind, Operation::bind_api},
        {FailAt::context, Operation::create_context},
        {FailAt::surface, Operation::create_surface},
        {FailAt::current, Operation::make_current},
    };

    for (const auto [fail_at, operation] : failures) {
        FakeEglApi api(fail_at);
        try {
            static_cast<void>(ogplay::gles::EglLifecycle::CreatePbuffer(
                api, kBackend, 8, 8));
            FAIL("expected EGL lifecycle failure");
        } catch (const ogplay::gles::EglLifecycleError& error) {
            CHECK(error.Operation() == operation);
            CHECK(error.NativeError() == FakeEglApi::kError);
        }
        const bool cleanup_finished = api.calls.back() == "error" ||
                                      api.calls.back() == "terminate";
        CHECK(cleanup_finished);
        CHECK(std::count(api.calls.begin(), api.calls.end(), "terminate") ==
              (fail_at == FailAt::display || fail_at == FailAt::initialize ? 0
                                                                           : 1));
        CHECK(std::count(api.calls.begin(), api.calls.end(), "destroy_context") ==
              (fail_at == FailAt::surface || fail_at == FailAt::current ? 1
                                                                         : 0));
        CHECK(std::count(api.calls.begin(), api.calls.end(), "destroy_surface") ==
              (fail_at == FailAt::current ? 1 : 0));
    }
}

TEST_CASE("native ANGLE EGL factory matches the build capability") {
    if (ogplay::gles::IsNativeAngleEglAvailable()) {
        auto api = ogplay::gles::CreateNativeAngleEglApi();
        REQUIRE(api != nullptr);
        auto lifecycle = ogplay::gles::EglLifecycle::CreatePbuffer(
            *api, kNativeBackend, 16, 16);
        CHECK(lifecycle.IsCurrent());
        CHECK(lifecycle.Info().egl_major >= 1);
    } else {
        const auto create_api = [] {
            static_cast<void>(ogplay::gles::CreateNativeAngleEglApi());
        };
        CHECK_THROWS_AS(create_api(), ogplay::gles::EglLifecycleError);
    }
}
