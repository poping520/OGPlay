#include "ogplay/audio/encoded_audio_stream.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include "ogplay/audio/wav.h"
#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb/stb_vorbis.c"
#include "../../third_party/minimp3/minimp3.h"

namespace ogplay::audio {
class EncodedAudioStream::Impl final {
public:
    struct Close { void operator()(stb_vorbis* p) const { stb_vorbis_close(p); } };
    explicit Impl(Bytes input, std::stop_token stop) : bytes(std::move(input)) {
        if (!bytes || bytes->empty() || bytes->size() > kMaximumEncodedAudioBytes)
            throw std::invalid_argument("invalid music window");
        const std::span<const std::byte> data(*bytes);
        if (LooksLikeWav(data)) {
            wav = ParseWav(data);
            rate = wav.sample_rate; channels = wav.channels;
            frames = wav.pcm.size() / (channels * (wav.bits / 8U));
        } else if (data.size() >= 4 && data[0] == std::byte{'O'} &&
                   data[1] == std::byte{'g'} && data[2] == std::byte{'g'} &&
                   data[3] == std::byte{'S'}) {
            int error{};
            // Fixed codec arena also bounds malicious Vorbis setup headers.
            vorbis_memory.resize(2U * 1024U * 1024U);
            stb_vorbis_alloc allocator{vorbis_memory.data(), static_cast<int>(vorbis_memory.size())};
            vorbis.reset(stb_vorbis_open_memory(
                reinterpret_cast<const unsigned char*>(data.data()),
                static_cast<int>(data.size()), &error, &allocator));
            if (!vorbis) throw std::invalid_argument("invalid Vorbis music");
            const auto info = stb_vorbis_get_info(vorbis.get());
            if (info.channels != 1 && info.channels != 2)
                throw std::invalid_argument("unsupported Vorbis channels");
            rate = info.sample_rate; channels = static_cast<std::uint8_t>(info.channels);
            frames = stb_vorbis_stream_length_in_samples(vorbis.get());
        } else {
            // Header-only scan: duration/format validation without allocating or
            // synthesizing song PCM. Decode resumes from the original bitstream.
            mp3dec_t scan{}; mp3dec_init(&scan);
            std::size_t offset{};
            while (offset < data.size()) {
                if (stop.stop_requested()) throw std::runtime_error("music prepare cancelled");
                mp3dec_frame_info_t info{};
                const auto decoded_frames = mp3dec_decode_frame(&scan,
                    reinterpret_cast<const std::uint8_t*>(data.data() + offset),
                    static_cast<int>(data.size() - offset), nullptr, &info);
                if (info.frame_bytes <= 0) break;
                offset += static_cast<std::size_t>(info.frame_bytes);
                if (decoded_frames == 0) continue;
                if ((info.channels != 1 && info.channels != 2) || info.hz <= 0 ||
                    (rate && (rate != static_cast<std::uint32_t>(info.hz) || channels != info.channels)))
                    throw std::invalid_argument("MP3 changes PCM format");
                rate = static_cast<std::uint32_t>(info.hz);
                channels = static_cast<std::uint8_t>(info.channels);
                frames += static_cast<std::size_t>(decoded_frames);
            }
            mp3dec_init(&mp3);
        }
        if (!rate || !frames || stop.stop_requested())
            throw std::invalid_argument("empty or cancelled music stream");
        // Validate and prime one block only, not the complete decoded song.
        static_cast<void>(Sample(0));
    }

    std::array<std::int16_t, 2> Sample(std::size_t frame) {
        if (frame >= frames) throw std::out_of_range("music frame past EOF");
        if (!wav.pcm.empty()) {
            const auto read = [&](std::size_t channel) -> std::int16_t {
                const auto n = frame * channels + channel;
                if (wav.bits == 8) return static_cast<std::int16_t>(
                    (std::to_integer<int>(wav.pcm[n]) - 128) * 256);
                return static_cast<std::int16_t>(std::to_integer<unsigned>(wav.pcm[n * 2]) |
                    (std::to_integer<unsigned>(wav.pcm[n * 2 + 1]) << 8U));
            };
            return {read(0), read(channels == 1 ? 0 : 1)};
        }
        if (frame < begin || frame >= begin + count) {
            if (vorbis) {
                if (!stb_vorbis_seek(vorbis.get(), static_cast<unsigned>(frame)))
                    throw std::runtime_error("Vorbis seek failed");
                begin = frame;
                count = static_cast<std::size_t>(stb_vorbis_get_samples_short_interleaved(
                    vorbis.get(), channels, pcm.data(), static_cast<int>(pcm.size())));
            } else {
                if (frame < begin) { cursor = 0; next_frame = 0; mp3dec_init(&mp3); }
                do {
                    bool decoded{};
                    while (cursor < bytes->size()) {
                        mp3dec_frame_info_t info{};
                        const auto n = mp3dec_decode_frame(&mp3,
                            reinterpret_cast<const std::uint8_t*>(bytes->data() + cursor),
                            static_cast<int>(bytes->size() - cursor), pcm.data(), &info);
                        if (info.frame_bytes <= 0) break;
                        cursor += static_cast<std::size_t>(info.frame_bytes);
                        if (n == 0) continue;
                        if (info.hz != static_cast<int>(rate) || info.channels != channels)
                            throw std::runtime_error("MP3 format changed while decoding");
                        begin = next_frame; count = static_cast<std::size_t>(n);
                        next_frame += count; decoded = true; break;
                    }
                    if (!decoded) throw std::runtime_error("truncated MP3 music");
                } while (frame >= begin + count);
            }
        }
        if (frame >= begin + count) throw std::runtime_error("truncated music block");
        const auto i = (frame - begin) * channels;
        return {pcm[i], pcm[i + (channels == 1 ? 0 : 1)]};
    }

    Bytes bytes;
    WavPcmView wav;
    std::vector<char> vorbis_memory;
    std::unique_ptr<stb_vorbis, Close> vorbis;
    mp3dec_t mp3{};
    std::array<std::int16_t, 8192> pcm{};
    std::uint32_t rate{};
    std::uint8_t channels{};
    std::size_t frames{}, begin{}, count{}, cursor{}, next_frame{};
};
EncodedAudioStream::EncodedAudioStream(Bytes bytes, std::stop_token stop)
    : impl_(std::make_unique<Impl>(std::move(bytes), stop)) {}
EncodedAudioStream::~EncodedAudioStream() = default;
std::uint32_t EncodedAudioStream::Rate() const { return impl_->rate; }
std::uint8_t EncodedAudioStream::Channels() const { return impl_->channels; }
std::size_t EncodedAudioStream::Frames() const { return impl_->frames; }
std::array<std::int16_t, 2> EncodedAudioStream::Sample(std::size_t frame) { return impl_->Sample(frame); }
std::size_t EncodedAudioStream::BufferedPcmBytes() const { return impl_->pcm.size() * sizeof(std::int16_t); }
}  // namespace ogplay::audio
