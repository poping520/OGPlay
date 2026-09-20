#include "ogplay/audio/encoded_audio_stream.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "ogplay/core/byte_order.h"
#define STB_VORBIS_HEADER_ONLY
#include "../../third_party/stb/stb_vorbis.c"
#include "../../third_party/minimp3/minimp3.h"

namespace ogplay::audio {
namespace {

constexpr std::size_t kEncodedBlockBytes = 64U * 1024U;
constexpr std::size_t kMaximumVorbisHeaderBytes = 2U * 1024U * 1024U;

class MemorySource final : public EncodedAudioDataSource {
public:
    explicit MemorySource(EncodedAudioStream::Bytes bytes) : bytes_(std::move(bytes)) {}
    [[nodiscard]] std::uint64_t Size() const noexcept override {
        return bytes_ ? bytes_->size() : 0U;
    }
    [[nodiscard]] std::size_t ReadAt(
        const std::uint64_t offset, const std::span<std::byte> destination,
        const std::stop_token stop) const override {
        if (stop.stop_requested() || !bytes_ || offset >= bytes_->size()) return 0;
        const auto count = std::min<std::size_t>(
            destination.size(), bytes_->size() - static_cast<std::size_t>(offset));
        std::copy_n(bytes_->begin() + static_cast<std::ptrdiff_t>(offset), count,
                    destination.begin());
        return count;
    }
private:
    EncodedAudioStream::Bytes bytes_;
};

[[nodiscard]] std::uint16_t Read16(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    return core::ReadLittleEndian<std::uint16_t>(bytes, offset);
}
[[nodiscard]] std::uint32_t Read32(const std::span<const std::byte> bytes,
                                   const std::size_t offset) {
    return core::ReadLittleEndian<std::uint32_t>(bytes, offset);
}
[[nodiscard]] bool Tag(const std::span<const std::byte> bytes,
                       const std::size_t offset, const char (&tag)[5]) {
    return offset + 4U <= bytes.size() &&
           std::memcmp(bytes.data() + offset, tag, 4U) == 0;
}

[[nodiscard]] std::int16_t VorbisPcm16(const float value) noexcept {
    union FloatBits {
        float floating;
        int integer;
    } converted{};
    constexpr auto magic = 1.5F * static_cast<float>(1U << 8U) +
                           0.5F / static_cast<float>(1U << 15U);
    constexpr auto addend = ((150 - 15) << 23) + (1 << 22);
    converted.floating = value + magic;
    auto result = converted.integer - addend;
    if (static_cast<unsigned int>(result + 32768) > 65535U)
        result = result < 0 ? -32768 : 32767;
    return static_cast<std::int16_t>(result);
}

}  // namespace

class EncodedAudioStream::Impl final {
public:
    struct Close { void operator()(stb_vorbis* value) const { stb_vorbis_close(value); } };
    enum class Kind { wav, vorbis, mp3 };

    explicit Impl(std::shared_ptr<const EncodedAudioDataSource> input,
                  const std::stop_token stop)
        : source(std::move(input)) {
        if (!source || source->Size() < 4U ||
            source->Size() > kMaximumEncodedAudioBytes) {
            throw std::invalid_argument("invalid music source");
        }
        std::array<std::byte, 12> header{};
        ReadExact(0, header, stop);
        if (Tag(header, 0, "RIFF") && Tag(header, 8, "WAVE")) {
            kind = Kind::wav;
            OpenWav(stop);
        } else if (Tag(header, 0, "OggS")) {
            kind = Kind::vorbis;
            OpenVorbis(stop, true);
        } else {
            kind = Kind::mp3;
            ScanMp3(stop);
            mp3dec_init(&mp3);
        }
        if (!rate || !frames || stop.stop_requested()) {
            throw std::invalid_argument("empty or cancelled music stream");
        }
        static_cast<void>(Sample(0));
    }

    void ReadExact(const std::uint64_t offset, const std::span<std::byte> target,
                   const std::stop_token stop = {}) const {
        std::size_t done{};
        while (done < target.size()) {
            if (stop.stop_requested()) throw std::runtime_error("music read cancelled");
            const auto got = source->ReadAt(offset + done, target.subspan(done), stop);
            if (got == 0 || got > target.size() - done)
                throw std::runtime_error("truncated music source");
            done += got;
        }
    }

    void Fill(const std::uint64_t offset, std::vector<std::byte>& target,
              const std::stop_token stop = {}) const {
        if (offset >= source->Size()) { target.clear(); return; }
        target.resize(static_cast<std::size_t>(std::min<std::uint64_t>(
            kEncodedBlockBytes, source->Size() - offset)));
        ReadExact(offset, target, stop);
    }

    void OpenWav(const std::stop_token stop) {
        std::uint64_t cursor{12U};
        bool have_format{};
        while (cursor + 8U <= source->Size()) {
            std::array<std::byte, 8> chunk{};
            ReadExact(cursor, chunk, stop);
            const auto size = Read32(chunk, 4);
            const auto payload = cursor + 8U;
            if (size > source->Size() - payload)
                throw std::invalid_argument("truncated WAV chunk");
            if (Tag(chunk, 0, "fmt ")) {
                if (size < 16U) throw std::invalid_argument("short WAV format chunk");
                std::array<std::byte, 16> format{};
                ReadExact(payload, format, stop);
                if (Read16(format, 0) != 1U)
                    throw std::invalid_argument("WAV is not PCM");
                channels = static_cast<std::uint8_t>(Read16(format, 2));
                rate = Read32(format, 4);
                bits = static_cast<std::uint8_t>(Read16(format, 14));
                if ((channels != 1U && channels != 2U) ||
                    (bits != 8U && bits != 16U) || rate == 0U)
                    throw std::invalid_argument("unsupported WAV format");
                have_format = true;
            } else if (Tag(chunk, 0, "data")) {
                pcm_offset = payload;
                pcm_bytes = size;
            }
            cursor = payload + size + (size & 1U);
        }
        if (!have_format || pcm_bytes == 0U)
            throw std::invalid_argument("WAV lacks format or PCM data");
        const auto frame_bytes = static_cast<std::size_t>(channels) * (bits / 8U);
        if (pcm_bytes % frame_bytes != 0U)
            throw std::invalid_argument("WAV PCM data is not frame aligned");
        frames = pcm_bytes / frame_bytes;
    }

    void ScanMp3(const std::stop_token stop) {
        mp3dec_t decoder{};
        mp3dec_init(&decoder);
        std::vector<std::byte> buffer;
        std::uint64_t offset{};
        while (offset < source->Size()) {
            if (stop.stop_requested()) throw std::runtime_error("music prepare cancelled");
            Fill(offset, buffer, stop);
            mp3dec_frame_info_t info{};
            const auto decoded = mp3dec_decode_frame(
                &decoder, reinterpret_cast<const std::uint8_t*>(buffer.data()),
                static_cast<int>(buffer.size()), nullptr, &info);
            if (info.frame_bytes <= 0) break;
            offset += static_cast<std::size_t>(info.frame_bytes);
            if (decoded == 0) continue;
            if ((info.channels != 1 && info.channels != 2) || info.hz <= 0 ||
                (rate && (rate != static_cast<std::uint32_t>(info.hz) ||
                          channels != info.channels)))
                throw std::invalid_argument("MP3 changes PCM format");
            rate = static_cast<std::uint32_t>(info.hz);
            channels = static_cast<std::uint8_t>(info.channels);
            frames += static_cast<std::size_t>(decoded);
        }
    }

    void RefillVorbis(const std::stop_token stop) {
        if (vorbis_cursor >= source->Size()) return;
        const auto remaining = vorbis_buffer.size() - vorbis_begin;
        if (remaining != 0U && vorbis_begin != 0U) {
            std::memmove(vorbis_buffer.data(),
                         vorbis_buffer.data() + vorbis_begin, remaining);
        }
        const auto added = static_cast<std::size_t>(std::min<std::uint64_t>(
            kEncodedBlockBytes, source->Size() - vorbis_cursor));
        vorbis_buffer.resize(remaining + added);
        ReadExact(vorbis_cursor,
                  std::span<std::byte>(vorbis_buffer).subspan(remaining), stop);
        vorbis_cursor += added;
        vorbis_begin = 0U;
    }

    void OpenVorbis(const std::stop_token stop, const bool scan) {
        vorbis.reset();
        vorbis_buffer.clear();
        vorbis_begin = 0U;
        vorbis_cursor = 0U;
        int error = VORBIS_need_more_data;
        int consumed{};
        while (!vorbis && error == VORBIS_need_more_data) {
            if (stop.stop_requested()) throw std::runtime_error("music prepare cancelled");
            RefillVorbis(stop);
            if (vorbis_buffer.empty() ||
                vorbis_buffer.size() > kMaximumVorbisHeaderBytes)
                throw std::invalid_argument("invalid Vorbis headers");
            vorbis.reset(stb_vorbis_open_pushdata(
                reinterpret_cast<const unsigned char*>(vorbis_buffer.data()),
                static_cast<int>(vorbis_buffer.size()), &consumed, &error, nullptr));
        }
        if (!vorbis) throw std::invalid_argument("invalid Vorbis music");
        vorbis_begin = static_cast<std::size_t>(consumed);
        const auto info = stb_vorbis_get_info(vorbis.get());
        if (info.channels != 1 && info.channels != 2)
            throw std::invalid_argument("unsupported Vorbis channels");
        rate = info.sample_rate;
        channels = static_cast<std::uint8_t>(info.channels);
        if (!scan) return;
        frames = 0U;
        for (;;) {
            if (stop.stop_requested()) throw std::runtime_error("music prepare cancelled");
            int decoded_channels{}, samples{};
            float** output{};
            const auto used = stb_vorbis_decode_frame_pushdata(
                vorbis.get(),
                reinterpret_cast<const unsigned char*>(vorbis_buffer.data() + vorbis_begin),
                static_cast<int>(vorbis_buffer.size() - vorbis_begin),
                &decoded_channels, &output, &samples);
            vorbis_begin += static_cast<std::size_t>(used);
            frames += static_cast<std::size_t>(samples);
            if (used == 0) {
                if (vorbis_cursor >= source->Size()) break;
                RefillVorbis(stop);
            }
        }
        OpenVorbis(stop, false);
    }

    void DecodeVorbisBlock() {
        for (;;) {
            int decoded_channels{}, samples{};
            float** output{};
            const auto used = stb_vorbis_decode_frame_pushdata(
                vorbis.get(),
                reinterpret_cast<const unsigned char*>(vorbis_buffer.data() + vorbis_begin),
                static_cast<int>(vorbis_buffer.size() - vorbis_begin),
                &decoded_channels, &output, &samples);
            vorbis_begin += static_cast<std::size_t>(used);
            if (samples > 0) {
                begin = next_frame;
                count = std::min<std::size_t>(static_cast<std::size_t>(samples),
                                              pcm.size() / channels);
                for (std::size_t frame = 0; frame < count; ++frame) {
                    for (std::size_t channel = 0; channel < channels; ++channel) {
                        pcm[frame * channels + channel] =
                            VorbisPcm16(output[channel][frame]);
                    }
                }
                next_frame += count;
                return;
            }
            if (used == 0) {
                if (vorbis_cursor >= source->Size())
                    throw std::runtime_error("truncated Vorbis music");
                RefillVorbis({});
            }
        }
    }

    void DecodeMp3Block() {
        for (;;) {
            if (mp3_cursor >= source->Size())
                throw std::runtime_error("truncated MP3 music");
            Fill(mp3_cursor, codec_buffer);
            mp3dec_frame_info_t info{};
            const auto decoded = mp3dec_decode_frame(
                &mp3, reinterpret_cast<const std::uint8_t*>(codec_buffer.data()),
                static_cast<int>(codec_buffer.size()), pcm.data(), &info);
            if (info.frame_bytes <= 0)
                throw std::runtime_error("truncated MP3 music");
            mp3_cursor += static_cast<std::size_t>(info.frame_bytes);
            if (decoded == 0) continue;
            if (info.hz != static_cast<int>(rate) || info.channels != channels)
                throw std::runtime_error("MP3 format changed while decoding");
            begin = next_frame;
            count = static_cast<std::size_t>(decoded);
            next_frame += count;
            return;
        }
    }

    [[nodiscard]] std::array<std::int16_t, 2> Sample(const std::size_t frame) {
        if (frame >= frames) throw std::out_of_range("music frame past EOF");
        if (kind == Kind::wav) {
            std::array<std::byte, 4> raw{};
            const auto frame_bytes = static_cast<std::size_t>(channels) * (bits / 8U);
            ReadExact(pcm_offset + frame * frame_bytes,
                      std::span<std::byte>(raw).first(frame_bytes));
            const auto read = [&](const std::size_t channel) -> std::int16_t {
                if (bits == 8U)
                    return static_cast<std::int16_t>(
                        (std::to_integer<int>(raw[channel]) - 128) * 256);
                return static_cast<std::int16_t>(
                    std::to_integer<unsigned>(raw[channel * 2U]) |
                    (std::to_integer<unsigned>(raw[channel * 2U + 1U]) << 8U));
            };
            return {read(0), read(channels == 1U ? 0U : 1U)};
        }
        if (frame < begin || frame >= begin + count) {
            if (frame < begin) {
                begin = count = next_frame = 0U;
                if (kind == Kind::vorbis) OpenVorbis({}, false);
                else { mp3_cursor = 0U; mp3dec_init(&mp3); }
            }
            do {
                if (kind == Kind::vorbis) DecodeVorbisBlock();
                else DecodeMp3Block();
            } while (frame >= begin + count);
        }
        const auto index = (frame - begin) * channels;
        return {pcm[index], pcm[index + (channels == 1U ? 0U : 1U)]};
    }

    std::shared_ptr<const EncodedAudioDataSource> source;
    Kind kind{Kind::mp3};
    std::unique_ptr<stb_vorbis, Close> vorbis;
    std::vector<std::byte> vorbis_buffer;
    std::vector<std::byte> codec_buffer;
    std::size_t vorbis_begin{};
    std::uint64_t vorbis_cursor{};
    mp3dec_t mp3{};
    std::uint64_t mp3_cursor{};
    std::array<std::int16_t, 8192> pcm{};
    std::uint64_t pcm_offset{};
    std::size_t pcm_bytes{};
    std::uint32_t rate{};
    std::uint8_t channels{};
    std::uint8_t bits{};
    std::size_t frames{}, begin{}, count{}, next_frame{};
};

EncodedAudioStream::EncodedAudioStream(Bytes bytes, const std::stop_token stop)
    : EncodedAudioStream(std::make_shared<MemorySource>(std::move(bytes)), stop) {}
EncodedAudioStream::EncodedAudioStream(
    std::shared_ptr<const EncodedAudioDataSource> source, const std::stop_token stop)
    : impl_(std::make_unique<Impl>(std::move(source), stop)) {}
EncodedAudioStream::~EncodedAudioStream() = default;
std::uint32_t EncodedAudioStream::Rate() const { return impl_->rate; }
std::uint8_t EncodedAudioStream::Channels() const { return impl_->channels; }
std::size_t EncodedAudioStream::Frames() const { return impl_->frames; }
std::array<std::int16_t, 2> EncodedAudioStream::Sample(const std::size_t frame) {
    return impl_->Sample(frame);
}
std::size_t EncodedAudioStream::BufferedPcmBytes() const {
    return impl_->pcm.size() * sizeof(std::int16_t);
}
}  // namespace ogplay::audio
