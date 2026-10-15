#pragma once

namespace ogplay::app {
class Logger;
}

namespace ogplay::frontend {

int RunGuiCommand(int argc, const char* const argv[], app::Logger& logger);

}  // namespace ogplay::frontend
