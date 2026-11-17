#include <doctest/doctest.h>
#include "frontend/app/runtime_failure.h"
#include "frontend/cli/commands.h"
#include "apk_fixture.h"
#include "ogplay/app/json.h"
#include "ogplay/app/library.h"
#include "ogplay/app/run_report.h"
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
        if (!temporary_ || original_ < 0 || dup2(fileno(temporary_), fileno(stream_)) < 0)
            throw std::runtime_error("capture failed");
    }

    ~Capture() {
        std::fflush(stream_);
        static_cast<void>(dup2(original_, fileno(stream_)));
        close(original_);
        std::fclose(temporary_);
    }

    std::string Read() {
        std::fflush(stream_);
        std::fseek(temporary_, 0, SEEK_END);
        std::string result(static_cast<std::size_t>(std::ftell(temporary_)), '\0');
        std::rewind(temporary_);
        static_cast<void>(std::fread(result.data(), 1, result.size(), temporary_));
        return result;
    }
};

struct Result {
    int code;
    std::string out, error;
};

struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 ("ogplay-cli-library-" + std::to_string(ogplay::app::MonotonicClock::NowNs()));

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
        std::vector<const char*> views;
        for (const auto& arg : args)
            views.push_back(arg.c_str());
        Capture out(stdout), error(stderr);
        const auto code = ogplay::cli::RunCli(static_cast<int>(views.size()), views.data());
        return {code, out.Read(), error.Read()};
    }
};

void JsonSuccess(const Result& r) {
    CHECK(r.code == 0);
    CHECK(r.error.empty());
    CHECK(std::count(r.out.begin(), r.out.end(), '\n') == 1);
    ogplay::app::JsonParseError e;
    CHECK(ogplay::app::JsonDocument::ParseStrict(r.out, e).has_value());
}
}  // namespace

TEST_CASE("CLI library inspect import duplicates and remove retain saves") {
    Fixture f;
    JsonSuccess(f.Run({"list"}));
    const auto inspect = f.Run({"inspect", f.Text("sample.apk")});
    JsonSuccess(inspect);
    CHECK(inspect.out.find("org.example.game") != std::string::npos);
    CHECK(inspect.out.find("\"profile_id\":null") != std::string::npos);
    JsonSuccess(f.Run({"import", f.Text("sample.apk")}));
    auto duplicate = f.Run({"import", f.Text("sample.apk")});
    CHECK(duplicate.code == 1);
    CHECK(duplicate.out.empty());
    CHECK(duplicate.error.find("error [ogplay]") != std::string::npos);
    CHECK(duplicate.error.find("--new-instance") != std::string::npos);
    JsonSuccess(f.Run({"import", f.Text("sample.apk"), "--new-instance"}));
    CHECK(f.Run({"remove", "org.example.game"}).code == 1);
    JsonSuccess(f.Run({"remove", "org.example.game", "--yes"}));
    CHECK(std::filesystem::exists(f.root / "sandbox/org.example.game/meta.toml"));
    CHECK(std::filesystem::exists(f.root / "library/org.example.game-2/base.apk"));
}

TEST_CASE("GUI-23 last run report round trips and drives the CLI attention filter") {
    using ogplay::app::LastRunReport;
    Fixture f;
    JsonSuccess(f.Run({"import", f.Text("sample.apk")}));
    JsonSuccess(f.Run({"import", f.Text("sample.apk"), "--new-instance"}));
    JsonSuccess(f.Run({"import", f.Text("sample.apk"), "--new-instance"}));
    const auto entries = f.root / "library";
    // A guest message with invalid UTF-8 is kept readable instead of failing the write.
    const LastRunReport fault{.finished = true,
                              .started_at = "2026-11-08T10:00:00Z",
                              .ended_at = "2026-11-08T10:01:00Z",
                              .end = "fault",
                              .presented = 12,
                              .error = std::string("java/lang/IllegalStateException: bad \xff byte")};
    ogplay::app::WriteLastRunReport(entries / "org.example.game" / "last-run.json", fault);
    const auto loaded = ogplay::app::LoadLastRunReport(entries / "org.example.game" / "last-run.json");
    REQUIRE(loaded.has_value());
    CHECK(loaded->error == "java/lang/IllegalStateException: bad ? byte");
    auto expected = fault;
    expected.error = loaded->error;
    CHECK(*loaded == expected);
    CHECK(ogplay::app::LastRunFailed(*loaded, false));
    // A clean close is not a failure; the same close with a teardown error is.
    LastRunReport closed{.finished = true, .started_at = "t", .end = "closed", .exit_code = 0, .presented = 50};
    CHECK_FALSE(ogplay::app::LastRunFailed(closed, false));
    closed.exit_error = "onDestroy failed";
    CHECK(ogplay::app::LastRunFailed(closed, false));
    // Unfinished: in progress while the entry runs, interrupted (a failure) once nothing runs it.
    const LastRunReport unfinished{.started_at = "2026-11-08T10:00:00Z"};
    CHECK(ogplay::app::LastRunOutcome(unfinished, true) == "running");
    CHECK_FALSE(ogplay::app::LastRunFailed(unfinished, true));
    CHECK(ogplay::app::LastRunOutcome(unfinished, false) == "interrupted");
    ogplay::app::WriteLastRunReport(entries / "org.example.game-2" / "last-run.json", unfinished);
    ogplay::app::WriteLastRunReport(
        entries / "org.example.game-3" / "last-run.json",
        LastRunReport{.finished = true, .started_at = "t", .end = "exited", .presented = 9});
    const auto attention = f.Run({"list", "--filter", "attention"});
    JsonSuccess(attention);
    CHECK(attention.out.find("\"installation_id\":\"org.example.game\"") != std::string::npos);
    CHECK(attention.out.find("\"status\":\"last_run_failed\"") != std::string::npos);
    CHECK(attention.out.find("org.example.game-3") == std::string::npos);
    // list stays a summary; the last run lives in show.
    CHECK(attention.out.find("\"last_run\"") == std::string::npos);
    const auto fault_detail = f.Run({"show", "org.example.game"});
    JsonSuccess(fault_detail);
    CHECK(fault_detail.out.find("\"outcome\":\"fault\"") != std::string::npos);
    const auto interrupted_detail = f.Run({"show", "org.example.game-2"});
    JsonSuccess(interrupted_detail);
    CHECK(interrupted_detail.out.find("\"outcome\":\"interrupted\"") != std::string::npos);
    const auto launchable = f.Run({"list", "--filter", "launchable"});
    JsonSuccess(launchable);
    CHECK(launchable.out.find("org.example.game-3") != std::string::npos);
    CHECK(launchable.out.find("\"status\":\"last_run_failed\"") != std::string::npos);
    // A report this build cannot read is no run information, not entry damage.
    std::ofstream(entries / "org.example.game-3" / "last-run.json") << "{\"schema\":99}";
    const auto all = f.Run({"list"});
    JsonSuccess(all);
    CHECK(all.out.find("\"damaged\":true") == std::string::npos);
    CHECK(f.Run({"list", "--filter", "broken"}).code == 1);
}

TEST_CASE("CLI library show resolves an installation id or a unique package") {
    Fixture f;
    JsonSuccess(f.Run({"import", f.Text("sample.apk")}));
    const auto list = f.Run({"list"});
    JsonSuccess(list);
    {
        // list is a summary: these keys only, in this order.
        ogplay::app::JsonParseError e;
        const auto document = ogplay::app::JsonDocument::ParseStrict(list.out, e);
        REQUIRE(document.has_value());
        REQUIRE(document->Root().Size() == 1);
        const auto item = document->Root().Element(0);
        REQUIRE(item.has_value());
        CHECK(item->Keys() ==
              std::vector<std::string_view>{"installation_id", "package", "display_name", "version_name", "status"});
        CHECK(item->Member("status")->String() == "ready");
    }
    const auto by_id = f.Run({"show", "org.example.game"});
    JsonSuccess(by_id);
    CHECK(by_id.out.find("\"directory\"") != std::string::npos);
    CHECK(by_id.out.find("\"last_run\":null") != std::string::npos);
    JsonSuccess(f.Run({"import", f.Text("sample.apk"), "--new-instance"}));
    // org.example.game is now both an id and a shared package: the id wins.
    const auto exact = f.Run({"show", "org.example.game"});
    JsonSuccess(exact);
    CHECK(exact.out.find("\"installation_id\":\"org.example.game\"") != std::string::npos);
    const auto copy = f.Run({"show", "org.example.game-2"});
    JsonSuccess(copy);
    CHECK(copy.out.find("\"package\":\"org.example.game\"") != std::string::npos);
    // With no installation id equal to the package, the package must be unique.
    JsonSuccess(f.Run({"import", f.Text("sample.apk"), "--new-instance"}));
    JsonSuccess(f.Run({"remove", "org.example.game", "--yes"}));
    const auto ambiguous = f.Run({"show", "org.example.game"});
    CHECK(ambiguous.code == 1);
    CHECK(ambiguous.out.empty());
    CHECK(ambiguous.error.find("several installations") != std::string::npos);
    CHECK(ambiguous.error.find("org.example.game-3") != std::string::npos);
    // remove deletes data, so it never resolves a package.
    CHECK(f.Run({"settings", "get", "org.example.game"}).code == 1);
    JsonSuccess(f.Run({"remove", "org.example.game-3", "--yes"}));
    const auto by_package = f.Run({"show", "org.example.game"});
    JsonSuccess(by_package);
    CHECK(by_package.out.find("\"installation_id\":\"org.example.game-2\"") != std::string::npos);
    // settings resolve the same way and write to the resolved installation.
    JsonSuccess(f.Run({"settings", "set", "org.example.game", "cpu_cores=3"}));
    CHECK(std::get<std::uint32_t>(
              ogplay::app::LoadGameSettings(f.root / "library/org.example.game-2").values.at("cpu_cores")) == 3);
    JsonSuccess(f.Run({"settings", "get", "org.example.game"}));
    CHECK(f.Run({"remove", "org.example.game", "--yes"}).code == 1);
    CHECK(std::filesystem::exists(f.root / "library/org.example.game-2"));
    // `library --help` prints only the library section, even with other arguments around it.
    const auto help = f.Run({"show", "--help"});
    CHECK(help.code == 0);
    CHECK(help.out.find("Library subcommands:") == 0);
    CHECK(help.out.find("run-apk") == std::string::npos);
    const auto missing = f.Run({"show", "org.example.absent"});
    CHECK(missing.code == 1);
    CHECK(missing.out.empty());
    CHECK(f.Run({"show"}).code == 1);
    CHECK(f.Run({"show", "org.example.game-2", "extra"}).code == 1);
    // --human/-h indents the same document wherever it appears after `library`.
    for (const auto& command : std::vector<std::vector<std::string>>{{"show", "org.example.game-2", "--human"},
                                                                     {"-h", "show", "org.example.game-2"}}) {
        const auto human = f.Run(command);
        CHECK(human.code == 0);
        CHECK(std::count(human.out.begin(), human.out.end(), '\n') > 5);
        CHECK(human.out.find("\n  \"installation_id\": \"org.example.game-2\"") != std::string::npos);
        ogplay::app::JsonParseError e;
        CHECK(ogplay::app::JsonDocument::ParseStrict(human.out, e).has_value());
    }
}

TEST_CASE("CLI library settings reuse strict schema inheritance and reserved values") {
    Fixture f;
    JsonSuccess(f.Run({"import", f.Text("sample.apk")}));
    JsonSuccess(f.Run({"settings", "get", "org.example.game"}));
    JsonSuccess(
        f.Run({"settings", "set", "org.example.game", "cpu_cores=4", "brand=reserved", "ephemeral_sandbox=true"}));
    auto settings = ogplay::app::LoadGameSettings(f.root / "library/org.example.game");
    CHECK(std::get<std::uint32_t>(settings.values.at("cpu_cores")) == 4);
    for (const auto& key : {"unknown=true", "cpu_cores=0", "cpu_cores=2junk", "mcp_enabled=1",
                            "external_dir=/path-that-is-unavailable"}) {
        const auto failure = f.Run({"settings", "set", "org.example.game", key});
        CHECK(failure.code == 1);
        CHECK(failure.out.empty());
        CHECK(ogplay::app::LoadGameSettings(f.root / "library/org.example.game") == settings);
    }
    JsonSuccess(f.Run({"settings", "set", "org.example.game", "--unset", "cpu_cores"}));
    CHECK_FALSE(ogplay::app::LoadGameSettings(f.root / "library/org.example.game").values.contains("cpu_cores"));
    // A linked pack that went away must not block unrelated edits.
    const auto pack = f.root / "pack";
    std::filesystem::create_directories(pack);
    JsonSuccess(f.Run({"settings", "set", "org.example.game", "external_dir=" + pack.generic_string()}));
    std::filesystem::remove(pack);
    JsonSuccess(f.Run({"settings", "set", "org.example.game", "cpu_cores=4"}));
}

TEST_CASE("CLI library running lock and malformed options fail explicitly") {
    Fixture f;
    JsonSuccess(f.Run({"import", f.Text("sample.apk")}));
    {
        ogplay::app::RunningLease active(f.root, "org.example.game");
        const auto rejected = f.Run({"remove", "org.example.game", "--yes"});
        CHECK(rejected.code == 1);
        CHECK(rejected.error.find("instance_running") != std::string::npos);
        const auto run = f.Run({"run", "org.example.game", "--mode", "preflight"});
        CHECK(run.code == 1);
        CHECK(run.error.find("instance_running") != std::string::npos);
    }
    for (const auto& command : std::vector<std::vector<std::string>>{{},
                                                                     {"unknown"},
                                                                     {"list", "extra"},
                                                                     {"run", "missing"},
                                                                     {"run", "org.example.game", "--mode", "bad"},
                                                                     {"settings", "set", "org.example.game"}})
        CHECK(f.Run(command).code == 1);
}

TEST_CASE("DVM-315 CLI reports known non standard runtime failures and preserves exit one") {
    const auto report = [](std::exception_ptr failure) {
        Capture error(stderr);
        CHECK(ogplay::cli::ReportCliFailure(failure) == 1);
        return error.Read();
    };
    std::exception_ptr java_failure;
    try {
        ogplay::app::detail::RethrowRuntimeFailure(std::make_exception_ptr(
            ogplay::runtime::dexvm::VmJavaThrow{"Ljava/lang/IllegalStateException;", "startup callback failed"}));
    } catch (const std::runtime_error&) {
        java_failure = std::current_exception();
    }
    REQUIRE(java_failure);
    CHECK(
        report(java_failure) ==
        "\nerror [ogplay]\n  dexvm::VmJavaThrow\n    exception: Ljava/lang/IllegalStateException;\n    message: startup callback failed\n");
    CHECK(report(std::make_exception_ptr(std::runtime_error("old failure"))) == "\nerror [ogplay]\n  old failure\n");
    CHECK(report(std::make_exception_ptr(7)) == "\nerror [ogplay]\n  unknown CLI failure\n");
}
