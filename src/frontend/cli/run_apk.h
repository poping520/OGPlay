#pragma once

namespace ogplay::core {
class Logger;
}

namespace ogplay::frontend {

int RunApkCommand(int argc, const char* const argv[], core::Logger& logger);

}  // namespace ogplay::frontend
