#pragma once

namespace ogplay::core {
class Logger;
}

namespace ogplay::frontend {

int RunGuiCommand(int argc, const char* const argv[], core::Logger& logger);
int RunGuiStandalone();

}  // namespace ogplay::frontend
