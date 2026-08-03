#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <system_error>

#include "ogplay/hal/audio.h"
#include "ogplay/hal/fs.h"
#include "ogplay/hal/gfx.h"

namespace {

class RecordingPresenter final : public ogplay::hal::GraphicsPresenter {
public:
    ogplay::hal::DrawableSize Size() const override { return size; }
    void Resize(const ogplay::hal::DrawableSize value) override { size = value; }
    void Present() override { ++present_count; }

    ogplay::hal::DrawableSize size{};
    std::uint32_t present_count{};
};

class RecordingAudioOutput final : public ogplay::hal::AudioOutput {
public:
    ogplay::hal::AudioStreamConfig Config() const override { return config; }
    bool IsStarted() const override { return started; }
    std::uint64_t QueuedFrames() const override { return queued_frames; }
    void Start() override { started = true; }
    void Stop() override { started = false; }
    void Submit(const std::span<const std::byte> samples) override {
        queued_frames += samples.size() / 4U;
    }

    ogplay::hal::AudioStreamConfig config{48'000, 2,
                                          ogplay::hal::AudioSampleFormat::signed_16_le};
    bool started{};
    std::uint64_t queued_frames{};
};

class TemporaryDirectory final {
public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() /
               "ogplay-hal-service-contract") {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error) throw std::filesystem::filesystem_error("remove_all", path, error);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

}  // namespace

TEST_CASE("gfx and audio HAL contracts do not expose backend types") {
    RecordingPresenter presenter;
    ogplay::hal::GraphicsPresenter& gfx = presenter;
    gfx.Resize({640, 360});
    gfx.Present();
    CHECK(gfx.Size() == ogplay::hal::DrawableSize{640, 360});
    CHECK(presenter.present_count == 1);

    RecordingAudioOutput output;
    ogplay::hal::AudioOutput& audio = output;
    const std::array<std::byte, 16> samples{};
    audio.Start();
    audio.Submit(samples);
    CHECK(audio.Config().sample_rate == 48'000);
    CHECK(audio.IsStarted());
    CHECK(audio.QueuedFrames() == 4);
    audio.Stop();
    CHECK_FALSE(audio.IsStarted());
}

TEST_CASE("standard host filesystem round trips bytes and reports failures") {
    TemporaryDirectory temporary;
    auto file_system = ogplay::hal::CreateStandardHostFileSystem();
    const auto directory = temporary.path / "nested";
    const auto file = directory / "payload.bin";
    const std::array contents{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};

    CHECK_FALSE(file_system->Status(file).has_value());
    file_system->CreateDirectories(directory);
    file_system->WriteFile(file, contents);
    CHECK(file_system->Status(directory) ==
          ogplay::hal::HostFileInfo{ogplay::hal::HostFileType::directory, 0});
    CHECK(file_system->Status(file) ==
          ogplay::hal::HostFileInfo{ogplay::hal::HostFileType::regular, 3});
    CHECK(file_system->ReadFile(file) ==
          std::vector<std::byte>(contents.begin(), contents.end()));

    const auto read_missing = [&] {
        static_cast<void>(file_system->ReadFile(directory / "missing.bin"));
    };
    CHECK_THROWS_AS(read_missing(), std::runtime_error);
}
