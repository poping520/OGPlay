#pragma once
#include "ogplay/app/export.h"

#ifdef __cplusplus
extern "C" {
#define OGPLAYCORE_NOEXCEPT noexcept
#else
#define OGPLAYCORE_NOEXCEPT
#endif

// UTF-8 arguments are borrowed for the duration of this blocking call.
// No C++ exceptions or owned objects cross this same-build application boundary.
OGPLAYCORE_API int ogplay_desktop_main(int argc, const char* const argv[]) OGPLAYCORE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#undef OGPLAYCORE_NOEXCEPT
