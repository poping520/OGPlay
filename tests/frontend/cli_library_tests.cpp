#include <doctest/doctest.h>
#include "frontend/cli/commands.h"
#include "apk_fixture.h"
#include "ogplay/app/json.h"
#include "ogplay/app/library.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>
#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#define fileno _fileno
#else
#include <unistd.h>
#endif
namespace {
class Capture final {
    FILE* stream_;
    FILE* temporary_{};
    int original_;
public:
    explicit Capture(FILE* stream) : stream_(stream), original_(dup(fileno(stream))) {
        std::fflush(stream_);
#ifdef _WIN32
        static_cast<void>(tmpfile_s(&temporary_));
#else
        temporary_ = std::tmpfile();
#endif
        if (!temporary_ || original_ < 0 || dup2(fileno(temporary_), fileno(stream_)) < 0) throw std::runtime_error("capture failed");
    }
    ~Capture() { std::fflush(stream_); static_cast<void>(dup2(original_, fileno(stream_))); close(original_); std::fclose(temporary_); }
    std::string Read() {
        std::fflush(stream_); std::fseek(temporary_, 0, SEEK_END);
        std::string result(static_cast<std::size_t>(std::ftell(temporary_)), '\0');
        std::rewind(temporary_); static_cast<void>(std::fread(result.data(), 1, result.size(), temporary_));
        return result;
    }
};
struct Result { int code; std::string out, error; };
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("ogplay-cli-library-" + std::to_string(ogplay::app::MonotonicClock::NowNs()));
    Fixture() {
        std::filesystem::create_directories(root);
        const auto bytes = ogplay::test::MinimalApk();
        std::ofstream output(root / "sample.apk", std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        std::filesystem::create_directory(root / "empty-profiles");
        ogplay::app::LibraryConfig config;
        config.profiles_dir = root / "empty-profiles";
        ogplay::app::SaveLibraryConfig(root, config);
    }
    ~Fixture() { std::filesystem::remove_all(root); }
    std::string Text(const char* name = "") const {
        const auto path = (root / name).generic_u8string();
        return {reinterpret_cast<const char*>(path.data()), path.size()};
    }
    Result Run(std::vector<std::string> tail) {
        std::vector<std::string> args{"ogplay-cli", "library", "--library-root", Text()};
        args.insert(args.end(), tail.begin(), tail.end());
        std::vector<const char*> views; for (const auto& arg : args) views.push_back(arg.c_str());
        Capture out(stdout), error(stderr);
        const auto code = ogplay::cli::RunCli(static_cast<int>(views.size()), views.data());
        return {code, out.Read(), error.Read()};
    }
};
void JsonSuccess(const Result& r) {
    CHECK(r.code == 0); CHECK(r.error.empty());
    CHECK(std::count(r.out.begin(), r.out.end(), '\n') == 1);
    ogplay::app::JsonParseError e;
    CHECK(ogplay::app::JsonDocument::ParseStrict(r.out, e).has_value());
}
}
TEST_CASE("CLI library inspect import duplicates and remove retain saves") {
    Fixture f;
    JsonSuccess(f.Run({"list"}));
    const auto inspect = f.Run({"inspect", f.Text("sample.apk")});
    JsonSuccess(inspect); CHECK(inspect.out.find("org.example.game") != std::string::npos);
    CHECK(inspect.out.find("\"profile_id\":null") != std::string::npos);
    JsonSuccess(f.Run({"import", f.Text("sample.apk")}));
    auto duplicate = f.Run({"import", f.Text("sample.apk")});
    CHECK(duplicate.code == 1); CHECK(duplicate.out.empty());
    CHECK(duplicate.error.find("error [ogplay]") != std::string::npos);
    CHECK(duplicate.error.find("--new-instance") != std::string::npos);
    JsonSuccess(f.Run({"import", f.Text("sample.apk"), "--new-instance"}));
    CHECK(f.Run({"remove", "org.example.game"}).code == 1);
    JsonSuccess(f.Run({"remove", "org.example.game", "--yes"}));
    CHECK(std::filesystem::exists(f.root / "sandbox/org.example.game/meta.toml"));
    CHECK(std::filesystem::exists(f.root / "library/org.example.game-2/game.apk"));
}
TEST_CASE("CLI library settings reuse strict schema inheritance and reserved values") {
    Fixture f;
    JsonSuccess(f.Run({"import", f.Text("sample.apk")}));
    JsonSuccess(f.Run({"settings", "get", "org.example.game"}));
    JsonSuccess(f.Run({"settings", "set", "org.example.game", "cpu_cores=4", "brand=reserved", "ephemeral_sandbox=true"}));
    auto settings = ogplay::app::LoadGameSettings(f.root / "library/org.example.game");
    CHECK(std::get<std::uint32_t>(settings.values.at("cpu_cores")) == 4);
    for (const auto& key : {"unknown=true", "cpu_cores=0", "cpu_cores=2junk", "mcp_enabled=1", "external_dir=/path-that-is-unavailable"}) {
        const auto failure = f.Run({"settings", "set", "org.example.game", key});
        CHECK(failure.code == 1); CHECK(failure.out.empty());
        CHECK(ogplay::app::LoadGameSettings(f.root / "library/org.example.game") == settings);
    }
    JsonSuccess(f.Run({"settings", "set", "org.example.game", "--unset", "cpu_cores"}));
    CHECK_FALSE(ogplay::app::LoadGameSettings(f.root / "library/org.example.game").values.contains("cpu_cores"));
}
TEST_CASE("CLI library running lock and malformed options fail explicitly") {
    Fixture f;
    JsonSuccess(f.Run({"import", f.Text("sample.apk")}));
    {
        ogplay::app::RunningLease active(f.root, "org.example.game");
        const auto rejected = f.Run({"remove", "org.example.game", "--yes"});
        CHECK(rejected.code == 1); CHECK(rejected.error.find("instance_running") != std::string::npos);
        const auto run = f.Run({"run", "org.example.game", "--mode", "preflight"});
        CHECK(run.code == 1); CHECK(run.error.find("instance_running") != std::string::npos);
    }
    for (const auto& command : std::vector<std::vector<std::string>>{{}, {"unknown"}, {"list", "extra"}, {"run", "missing"}, {"run", "org.example.game", "--mode", "bad"}, {"settings", "set", "org.example.game"}})
        CHECK(f.Run(command).code == 1);
}
