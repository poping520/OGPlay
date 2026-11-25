#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "ogplay/memory/address.h"

namespace ogplay::runtime {

// Shared guest layout values only. Registration/lifetime belong to the process owner.
inline constexpr std::uint32_t kBionicHleThunkBegin = 0x70000000U;
// RX thunks and fixed data reservations are disjoint, independent of catalog size.
inline constexpr std::uint32_t kBionicHleThunkEnd = 0x70010000U;
inline constexpr std::uint32_t kBoundaryThunkCapacity = (kBionicHleThunkEnd - kBionicHleThunkBegin) / 4U;
inline constexpr memory::GuestRange kBoundaryGles1Strings{memory::GuestAddress{0x70010000U}, 0x10000U};
inline constexpr memory::GuestRange kBoundaryGles2Strings{memory::GuestAddress{0x70020000U}, 0x10000U};
inline constexpr memory::GuestRange kBoundaryEventTags{memory::GuestAddress{0x70F00000U}, 0x100000U};
inline constexpr memory::GuestRange kBoundaryOpenSlesAbi{memory::GuestAddress{0x71800000U}, 0x3000U};
inline constexpr memory::GuestRange kBoundaryOpenSlesObjects{memory::GuestAddress{0x71900000U}, 0x100000U};
inline constexpr memory::GuestRange kBoundaryEglStrings{memory::GuestAddress{0x71C00000U}, 0x10000U};
inline constexpr memory::GuestRange kBoundaryGles3Strings{memory::GuestAddress{0x71E00000U}, 0x10000U};
inline constexpr memory::GuestRange kBoundaryBufferMaps{memory::GuestAddress{0x72000000U}, 0x2000000U};
inline constexpr std::array kBoundaryFixedRegions{kBoundaryGles1Strings, kBoundaryGles2Strings,    kBoundaryEventTags,
                                                  kBoundaryOpenSlesAbi,  kBoundaryOpenSlesObjects, kBoundaryEglStrings,
                                                  kBoundaryGles3Strings, kBoundaryBufferMaps};
inline constexpr memory::GuestAddress kApi19GuestTlsAddress{0x6a000000U};
inline constexpr memory::GuestAddress kApi19GuestThreadInfoAddress{0x6a001000U};
inline constexpr memory::GuestAddress kApi19GuestPreinitAddress{0x6a002000U};
inline constexpr memory::GuestAddress kApi19GuestEnvironmentAddress{0x6a003000U};
inline constexpr memory::GuestAddress kApi19GuestStackAddress{0x6b000000U};
inline constexpr std::uint64_t kApi19GuestStackSize = UINT64_C(4) * 1024U * 1024U;
inline constexpr memory::GuestAddress kApi19GuestReturnAddress{0x6f001000U};
inline constexpr memory::GuestAddress kApi19GuestPropertyAreaAddress{0x6f002000U};

inline constexpr memory::GuestAddress kJniGuestAbiDataBegin{0x71200000U};
inline constexpr memory::GuestAddress kJniGuestEnvironmentTable{kJniGuestAbiDataBegin};
inline constexpr memory::GuestAddress kJniGuestInvokeTable{0x71200400U};
inline constexpr memory::GuestAddress kJniGuestEnvironment{0x71200420U};
inline constexpr memory::GuestAddress kJniGuestJavaVm{0x71200424U};

inline constexpr std::uint32_t kJniThunkBegin = 0x71000000U;
inline constexpr std::uint32_t kJniInvokeThunkBegin = 0x71100000U;

inline constexpr memory::GuestAddress kNativeActivityWindowHandleBegin{0x6e400000U};
inline constexpr memory::GuestAddress kNativeActivityWindowHandleEnd{0x6e800000U};

inline constexpr std::uint32_t kDexVmThreadMaximum = 32;
inline constexpr std::uint32_t kDexVmTlsBase = 0x6a100000U;
inline constexpr std::uint32_t kDexVmStackBase = 0x6c000000U;
inline constexpr std::uint32_t kDexVmStackSize = 1024U * 1024U;
inline constexpr std::uint32_t kOpenSlesCallbackTls = 0x71a00000U;
inline constexpr std::uint32_t kOpenSlesCallbackThreadInfo = 0x71a01000U;
inline constexpr std::uint32_t kOpenSlesCallbackStack = 0x71b00000U;
inline constexpr std::uint32_t kTemporaryArenaBegin = 0x74000000U;
inline constexpr std::uint32_t kTemporaryArenaEnd = 0x78000000U;

namespace guest_layout {
inline constexpr memory::GuestRange kHostWindow{memory::GuestAddress{0x6a000000U}, 0x15000000U};
inline constexpr memory::GuestRange kBrkWindow{memory::GuestAddress{0x50000000U}, 0x08000000U};
inline constexpr std::array kReservedWindows{kBrkWindow, kHostWindow};
// mmap placement: ARM TASK_SIZE 0xbf000000 minus the kernel's 128 MiB minimum stack gap.
inline constexpr memory::GuestRange kMmapArea{memory::GuestAddress{0x60000000U}, 0xb7000000U - 0x60000000U};
inline constexpr memory::GuestRange kMmapFallback{memory::GuestAddress{0x60000000U},
                                                  UINT64_C(0x100000000) - 0x60000000U};
inline constexpr memory::GuestRange kOpaqueHandles{memory::GuestAddress{0x6e000000U}, 0x01000000U};
inline constexpr std::uint32_t kConfigurationHandleBegin = 0x6e003000U;
inline constexpr std::uint32_t kLogFormatHandleBegin = 0x6e004000U;
inline constexpr std::uint32_t kSensorManagerHandle = 0x6e008000U;
inline constexpr std::uint32_t kLooperHandleBegin = 0x6e010000U;
inline constexpr std::uint32_t kStandaloneInputQueue = 0x6e020000U;
inline constexpr std::uint32_t kStandaloneAssetManager = 0x6e030000U;
inline constexpr std::uint32_t kAssetHandleBegin = 0x6e100000U;
inline constexpr std::uint32_t kInputHandleBegin = 0x6e200000U;
inline constexpr std::uint32_t kAssetManagerHandleBegin = 0x6e300000U;
inline constexpr std::uint32_t kSensorQueueHandleBegin = 0x6e900000U;
inline constexpr std::uint32_t kAssetDirectoryHandleBegin = 0x6ea00000U;
inline constexpr std::uint32_t kObbInfoHandleBegin = 0x6eb00000U;
inline constexpr std::uint32_t kStorageManagerHandleBegin = 0x6ec00000U;
inline constexpr std::uint32_t kStorageManagerHandleEnd = 0x6ed00000U;
// Existing opaque handle families use typed registries and may share numeric
// subranges. They jointly occupy one address-exclusion region, never backing.
inline constexpr memory::GuestAddress kNativeActivityPage{0x6f000000U};
inline constexpr memory::GuestAddress kLibdlErrorBegin{0x71d00000U};
inline constexpr std::uint32_t kLibdlErrorBytes = 256U * 4096U;
inline constexpr memory::GuestRange kLinkerViews{memory::GuestAddress{0x79000000U}, 0x01000000U};
inline constexpr memory::GuestRange kCpuEnvironment{memory::GuestAddress{0x7d000000U}, 0x01000000U};
inline constexpr memory::GuestRange kSignalTrampolines{memory::GuestAddress{0x7e000000U}, 0x01000000U};

inline constexpr std::array kFixedRegions{
    kBrkWindow,
    memory::GuestRange{kApi19GuestTlsAddress, 4U * 4096U},
    memory::GuestRange{memory::GuestAddress{kDexVmTlsBase}, kDexVmThreadMaximum * 2U * 4096U},
    memory::GuestRange{kApi19GuestStackAddress, kApi19GuestStackSize},
    memory::GuestRange{memory::GuestAddress{kDexVmStackBase}, kDexVmThreadMaximum * kDexVmStackSize},
    kOpaqueHandles,
    memory::GuestRange{kNativeActivityPage, 4096U},
    memory::GuestRange{kApi19GuestReturnAddress, 4096U},
    memory::GuestRange{kApi19GuestPropertyAreaAddress, 4096U},
    memory::GuestRange{memory::GuestAddress{kBionicHleThunkBegin}, kBionicHleThunkEnd - kBionicHleThunkBegin},
    kBoundaryGles1Strings,
    kBoundaryGles2Strings,
    kBoundaryEventTags,
    memory::GuestRange{memory::GuestAddress{kJniThunkBegin}, 4096U},
    memory::GuestRange{memory::GuestAddress{kJniInvokeThunkBegin}, 4096U},
    memory::GuestRange{kJniGuestAbiDataBegin, 4096U},
    kBoundaryOpenSlesAbi,
    kBoundaryOpenSlesObjects,
    memory::GuestRange{memory::GuestAddress{kOpenSlesCallbackTls}, 2U * 4096U},
    memory::GuestRange{memory::GuestAddress{kOpenSlesCallbackStack}, kDexVmStackSize},
    kBoundaryEglStrings,
    memory::GuestRange{kLibdlErrorBegin, kLibdlErrorBytes},
    kBoundaryGles3Strings,
    kBoundaryBufferMaps,
    memory::GuestRange{memory::GuestAddress{kTemporaryArenaBegin}, kTemporaryArenaEnd - kTemporaryArenaBegin},
    kLinkerViews,
    kCpuEnvironment,
    kSignalTrampolines};
static_assert([] {
    for (std::size_t i = 0; i < kFixedRegions.size(); ++i) {
        if (!kHostWindow.Contains(kFixedRegions[i]) && !kBrkWindow.Contains(kFixedRegions[i])) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (kFixedRegions[i].Overlaps(kFixedRegions[j])) return false;
    }
    for (const auto address :
         {kConfigurationHandleBegin, kLogFormatHandleBegin, kSensorManagerHandle, kLooperHandleBegin, kAssetHandleBegin,
          kInputHandleBegin, kAssetManagerHandleBegin, kSensorQueueHandleBegin, kAssetDirectoryHandleBegin,
          kObbInfoHandleBegin, kStorageManagerHandleBegin, kStorageManagerHandleEnd - 1U})
        if (!kOpaqueHandles.Contains(memory::GuestAddress{address})) return false;
    return kOpaqueHandles.Contains(
               memory::GuestRange{kNativeActivityWindowHandleBegin,
                                  kNativeActivityWindowHandleEnd.Value() - kNativeActivityWindowHandleBegin.Value()}) &&
           kApi19GuestThreadInfoAddress.Value() == kApi19GuestTlsAddress.Value() + 4096U &&
           kApi19GuestPreinitAddress.Value() == kApi19GuestTlsAddress.Value() + 8192U &&
           kApi19GuestEnvironmentAddress.Value() == kApi19GuestTlsAddress.Value() + 12288U &&
           kOpenSlesCallbackThreadInfo == kOpenSlesCallbackTls + 4096U;
}());
}  // namespace guest_layout
}  // namespace ogplay::runtime
