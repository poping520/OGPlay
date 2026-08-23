#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/runtime/boundary/opensles_callback.h"
#include "runtime/boundary/modules/opensles/opensles_exports.h"

namespace ogplay::runtime {

class A32CallFrame;
struct BoundaryCallServices;

class OpenSlesModule final {
public:
    OpenSlesModule(BoundaryCallServices& calls,
                   audio::OpenSlesPcmMixer& mixer,
                   OpenSlesCallbackSink callbacks);
    ~OpenSlesModule();
    OpenSlesModule(const OpenSlesModule&) = delete;
    OpenSlesModule& operator=(const OpenSlesModule&) = delete;

    [[nodiscard]] BoundaryCallServices& CallServices() noexcept;
    void MapGuestObjectArena();
    [[nodiscard]] std::vector<audio::OpenSlesConsumedBuffer> MixAdditiveStereoPcm16(
        std::span<std::int16_t> output, std::uint32_t output_rate);

#define OGPLAY_DECLARE_OPENSLES(name, id, count, kind, method) \
    std::uint32_t method(const A32CallFrame& call);
    OGPLAY_OPENSLES_BOUNDARY_EXPORTS(OGPLAY_DECLARE_OPENSLES)
#undef OGPLAY_DECLARE_OPENSLES

private:
    class Impl;
    BoundaryCallServices& calls_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
