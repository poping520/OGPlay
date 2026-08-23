#include "runtime/boundary/modules/opensles/opensles_abi.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace ogplay::runtime {
namespace {

struct NamedIid final {
    std::string_view name;
    OpenSlesIidValue value;
};

constexpr std::array kNamedIids{
    NamedIid{"SL_IID_3DCOMMIT", {0x3564ad80, 0xdd0f, 0x11db, 0x9e19, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_3DDOPPLER", {0xb45c9a80, 0xddd2, 0x11db, 0xb028, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_3DGROUPING", {0xebe844e0, 0xddd2, 0x11db, 0xb510, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_3DLOCATION", {0x2b878020, 0xddd3, 0x11db, 0x8a01, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_3DMACROSCOPIC", {0x5089aec0, 0xddd3, 0x11db, 0x9ad3, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_3DSOURCE", {0x70bc7b00, 0xddd3, 0x11db, 0xa873, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_AUDIODECODERCAPABILITIES", {0x3fe5a3a0, 0xfcc6, 0x11db, 0x94ac, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_AUDIOENCODER", {0xd7d5af7a, 0x351c, 0x41a6, 0x94ec, {0x1a, 0xc9, 0x5c, 0x71, 0x82, 0x2c}}},
    NamedIid{"SL_IID_AUDIOENCODERCAPABILITIES", {0x0f52a340, 0xfcd1, 0x11db, 0xa993, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_AUDIOIODEVICECAPABILITIES", {0xb2564dc0, 0xddd3, 0x11db, 0xbd62, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_BASSBOOST", {0x0634f220, 0xddd4, 0x11db, 0xa0fc, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_BUFFERQUEUE", {0x2bc99cc0, 0xddd4, 0x11db, 0x8d99, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_DEVICEVOLUME", {0xe1634760, 0xf3e2, 0x11db, 0x9ca9, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_DYNAMICINTERFACEMANAGEMENT", {0x63936540, 0xf775, 0x11db, 0x9cc4, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_DYNAMICSOURCE", {0xc55cc100, 0x038b, 0x11dc, 0xbb45, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_EFFECTSEND", {0x56e7d200, 0xddd4, 0x11db, 0xaefb, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ENGINE", {0x8d97c260, 0xddd4, 0x11db, 0x958f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ENGINECAPABILITIES", {0x8320d0a0, 0xddd5, 0x11db, 0xa1b1, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ENVIRONMENTALREVERB", {0xc2e5d5f0, 0x94bd, 0x4763, 0x9cac, {0x4e, 0x23, 0x4d, 0x06, 0x83, 0x9e}}},
    NamedIid{"SL_IID_EQUALIZER", {0x0bed4300, 0xddd6, 0x11db, 0x8f34, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_LED", {0x2cc1cd80, 0xddd6, 0x11db, 0x807e, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_METADATAEXTRACTION", {0xaa5b1f80, 0xddd6, 0x11db, 0xac8e, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_METADATATRAVERSAL", {0xc43662c0, 0xddd6, 0x11db, 0xa7ab, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_MIDIMESSAGE", {0xddf4a820, 0xddd6, 0x11db, 0xb174, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_MIDIMUTESOLO", {0x039eaf80, 0xddd7, 0x11db, 0x9a02, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_MIDITEMPO", {0x1f347400, 0xddd7, 0x11db, 0xa7ce, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_MIDITIME", {0x3da51de0, 0xddd7, 0x11db, 0xaf70, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_MUTESOLO", {0x5a28ebe0, 0xddd7, 0x11db, 0x8220, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_NULL", {0xec7178ec, 0xe5e1, 0x4432, 0xa3f4, {0x46, 0x57, 0xe6, 0x79, 0x52, 0x10}}},
    NamedIid{"SL_IID_OBJECT", {0x79216360, 0xddd7, 0x11db, 0xac16, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_OUTPUTMIX", {0x97750f60, 0xddd7, 0x11db, 0x92b1, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_PITCH", {0xc7e8ee00, 0xddd7, 0x11db, 0xa42c, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_PLAY", {0xef0bd9c0, 0xddd7, 0x11db, 0xbf49, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_PLAYBACKRATE", {0x2e3b2a40, 0xddda, 0x11db, 0xa349, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_PREFETCHSTATUS", {0x2a41ee80, 0xddd8, 0x11db, 0xa41f, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_PRESETREVERB", {0x47382d60, 0xddd8, 0x11db, 0xbf3a, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_RATEPITCH", {0x61b62e60, 0xddda, 0x11db, 0x9eb8, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_RECORD", {0xc5657aa0, 0xdddb, 0x11db, 0x82f7, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_SEEK", {0xd43135a0, 0xdddc, 0x11db, 0xb458, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_THREADSYNC", {0xf6ac6b40, 0xdddc, 0x11db, 0xa62e, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_VIBRA", {0x169a8d60, 0xdddd, 0x11db, 0x923d, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_VIRTUALIZER", {0x37cc2c00, 0xdddd, 0x11db, 0x8577, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_VISUALIZATION", {0xe46b26a0, 0xdddd, 0x11db, 0x8afd, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_VOLUME", {0x09e8ede0, 0xddde, 0x11db, 0xb4f6, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_OUTPUTMIXEXT", {0xfe5cce00, 0x57bb, 0x11df, 0x951c, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ANDROIDEFFECT", {0xae12da60, 0x99ac, 0x11df, 0xb456, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ANDROIDEFFECTCAPABILITIES", {0x6a4f6d60, 0xb5e6, 0x11df, 0xbb3b, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ANDROIDEFFECTSEND", {0x7be462c0, 0xbc43, 0x11df, 0x8670, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ANDROIDCONFIGURATION", {0x89f6a7e0, 0xbeac, 0x11df, 0x8b5c, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ANDROIDSIMPLEBUFFERQUEUE", {0x198e4940, 0xc5d7, 0x11df, 0xa2a6, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
    NamedIid{"SL_IID_ANDROIDBUFFERQUEUESOURCE", {0x7fc1a460, 0xeec1, 0x11df, 0xa306, {0x00, 0x02, 0xa5, 0xd5, 0xc5, 0x1b}}},
};

std::array<OpenSlesIidDescriptor, kNamedIids.size()> BuildIids() {
    std::array<OpenSlesIidDescriptor, kNamedIids.size()> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = {
            kNamedIids[index].name, kNamedIids[index].value,
            kOpenSlesAbiBegin.Add(index * 4U),
            kOpenSlesIidValuesBegin.Add(index * 16U)};
    }
    return result;
}

const auto kIids = BuildIids();

std::array<BoundaryExportDefinition, kNamedIids.size()> BuildExports() {
    std::array<BoundaryExportDefinition, kNamedIids.size()> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = {kIids[index].name,
                         static_cast<std::uint16_t>(200U + index), 0U, {},
                         BoundaryExportKind::public_data,
                         kIids[index].variable_address, 4U};
    }
    return result;
}

const auto kExports = BuildExports();

void Append16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value));
    bytes.push_back(static_cast<std::byte>(value >> 8U));
}
void Append32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (std::size_t byte = 0; byte < 4U; ++byte) {
        bytes.push_back(static_cast<std::byte>(value >> (byte * 8U)));
    }
}

template <typename Name, std::size_t Size>
void WriteVtable(memory::AddressSpace& address_space,
                 const BoundaryModuleDescriptor& module,
                 const memory::GuestAddress address,
                 const std::array<Name, Size>& names) {
    std::vector<std::byte> bytes;
    bytes.reserve(Size * 4U);
    for (const std::string_view name : names) {
        const auto found = std::find_if(
            module.exports.begin(), module.exports.end(),
            [&](const auto& export_) { return export_.name == name; });
        if (found == module.exports.end() ||
            found->kind != BoundaryExportKind::private_callable) {
            throw std::logic_error("OpenSL vtable callable metadata is missing");
        }
        Append32(bytes, found->address.Value());
    }
    address_space.Write(address, bytes);
}

}  // namespace

std::span<const OpenSlesIidDescriptor> OpenSlesIids() noexcept { return kIids; }

std::span<const BoundaryExportDefinition> OpenSlesDataExports() noexcept {
    return kExports;
}

void MapOpenSlesStaticAbi(memory::AddressSpace& address_space,
                          const BoundaryModuleDescriptor& module) {
    address_space.Map({kOpenSlesAbiBegin, kOpenSlesStaticAbiBytes},
                      memory::PageProtection::read |
                          memory::PageProtection::write);
    std::vector<std::byte> pointer_data(kIids.size() * 4U);
    for (std::size_t index = 0; index < kIids.size(); ++index) {
        const auto value = kIids[index].value_address.Value();
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            pointer_data[index * 4U + byte] =
                static_cast<std::byte>(value >> (byte * 8U));
        }
    }
    address_space.Write(kOpenSlesAbiBegin, pointer_data);

    std::vector<std::byte> iid_data;
    iid_data.reserve(kIids.size() * 16U);
    for (const auto& iid : kIids) {
        Append32(iid_data, iid.value.time_low);
        Append16(iid_data, iid.value.time_mid);
        Append16(iid_data, iid.value.time_hi_and_version);
        Append16(iid_data, iid.value.clock_seq);
        for (const auto value : iid.value.node) {
            iid_data.push_back(static_cast<std::byte>(value));
        }
    }
    address_space.Write(kOpenSlesIidValuesBegin, iid_data);
    static constexpr std::array object_methods{
        "$Object.Realize", "$Object.Resume", "$Object.GetState",
        "$Object.GetInterface", "$Object.RegisterCallback",
        "$Object.AbortAsyncOperation", "$Object.Destroy",
        "$Object.SetPriority", "$Object.GetPriority",
        "$Object.SetLossOfControlInterfaces"};
    WriteVtable(address_space, module, kOpenSlesObjectVtable, object_methods);
    static constexpr std::array engine_methods{
        "$Engine.CreateLEDDevice", "$Engine.CreateVibraDevice",
        "$Engine.CreateAudioPlayer", "$Engine.CreateAudioRecorder",
        "$Engine.CreateMidiPlayer", "$Engine.CreateListener",
        "$Engine.Create3DGroup", "$Engine.CreateOutputMix",
        "$Engine.CreateMetadataExtractor", "$Engine.CreateExtensionObject",
        "$Engine.QueryNumSupportedInterfaces",
        "$Engine.QuerySupportedInterfaces",
        "$Engine.QueryNumSupportedExtensions",
        "$Engine.QuerySupportedExtension", "$Engine.IsExtensionSupported"};
    WriteVtable(address_space, module, kOpenSlesEngineVtable, engine_methods);
    static constexpr std::array output_mix_methods{
        "$OutputMix.GetDestinationOutputDeviceIDs",
        "$OutputMix.RegisterDeviceChangeCallback", "$OutputMix.ReRoute"};
    WriteVtable(address_space, module, kOpenSlesOutputMixVtable,
                output_mix_methods);
    static constexpr std::array play_methods{
        "$Play.SetPlayState", "$Play.GetPlayState", "$Play.GetDuration",
        "$Play.GetPosition", "$Play.RegisterCallback",
        "$Play.SetCallbackEventsMask", "$Play.GetCallbackEventsMask",
        "$Play.SetMarkerPosition", "$Play.ClearMarkerPosition",
        "$Play.GetMarkerPosition", "$Play.SetPositionUpdatePeriod",
        "$Play.GetPositionUpdatePeriod"};
    WriteVtable(address_space, module, kOpenSlesPlayVtable, play_methods);
    static constexpr std::array queue_methods{
        "$BufferQueue.Enqueue", "$BufferQueue.Clear",
        "$BufferQueue.GetState", "$BufferQueue.RegisterCallback"};
    WriteVtable(address_space, module, kOpenSlesBufferQueueVtable,
                queue_methods);
    static constexpr std::array volume_methods{
        "$Volume.SetVolumeLevel", "$Volume.GetVolumeLevel",
        "$Volume.GetMaxVolumeLevel", "$Volume.SetMute", "$Volume.GetMute",
        "$Volume.EnableStereoPosition", "$Volume.IsEnabledStereoPosition",
        "$Volume.SetStereoPosition", "$Volume.GetStereoPosition"};
    WriteVtable(address_space, module, kOpenSlesVolumeVtable, volume_methods);
    address_space.Protect({kOpenSlesAbiBegin, kOpenSlesStaticAbiBytes},
                          memory::PageProtection::read);
}

}  // namespace ogplay::runtime
