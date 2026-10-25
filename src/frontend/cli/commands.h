#pragma once
#include <exception>
namespace ogplay::cli { int RunCli(int argc, const char* const argv[]); }

namespace ogplay::cli {
int ReportCliFailure(std::exception_ptr failure);
}
