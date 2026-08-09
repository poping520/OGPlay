#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "ogplay/agent/control_service.h"
#include "ogplay/agent/json_rpc.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/hal/clock.h"
#include "ogplay/session/session.h"

#include "run_apk.h"

namespace {

void Write(FILE* stream, const std::string_view text) {
    static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stream));
}

int Usage() {
    Write(stderr, "usage: ogplay --version | capabilities [path] | agent <method> | agent-stdio\n"
                  "       ogplay run-apk <apk> --system-dir <api19-lib-dir> "
                  "[--profiles-dir <dir>] [--external-dir <host-dir>] "
                  "[--preflight] [--supersample <1..4>] "
                  "[--exit-after-frames <count>]\n");
    return 2;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        Write(stdout, "OGPlay " OGPLAY_VERSION "\n");
        return 0;
    }
    if (argc < 2) return Usage();

    try {
        if (std::string_view(argv[1]) == "run-apk") {
            return ogplay::frontend::RunApkCommand(argc, argv);
        }
        const auto ledger_path = argc >= 3 && std::string_view(argv[1]) == "capabilities"
                                     ? std::filesystem::path(argv[2])
                                     : std::filesystem::path(OGPLAY_SOURCE_DIR) / "capabilities.toml";
        auto ledger = ogplay::core::CapabilityLedger::Load(ledger_path);
        ogplay::core::Logger logger;
        ogplay::hal::FixedStepClock clock(1'000, 60'000);
        ogplay::session::Session session(clock);
        ogplay::agent::ControlService service(ledger, logger, session);

        if (std::string_view(argv[1]) == "capabilities") {
            Write(stdout, service.Request("hle.capabilities").json + "\n");
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "agent") {
            const auto response = service.Request(argv[2]);
            Write(response.ok ? stdout : stderr, response.json + "\n");
            return response.ok ? 0 : 1;
        }
        if (argc == 2 && std::string_view(argv[1]) == "agent-stdio") {
            ogplay::agent::JsonRpcAdapter adapter(service);
            std::string line;
            while (std::getline(std::cin, line)) Write(stdout, adapter.Handle(line) + "\n");
            return 0;
        }
    } catch (const std::exception& error) {
        Write(stderr, std::string("ogplay: ") + error.what() + "\n");
        return 1;
    }
    return Usage();
}
