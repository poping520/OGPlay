#include "ogplay/runtime/integration/android_boundary_gles.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/cpu/cpu.h"
#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_call_preparation.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/gles_transfer_state.h"
#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {

class AndroidBoundaryGles::Impl final {
public:
    explicit Impl(memory::AddressSpace& address_space)
        : address_space_(address_space) {}

    std::optional<std::uint32_t> Dispatch(
        const std::string_view symbol,
        const std::array<std::uint32_t, 4>& args,
        const cpu::A32State& state, gles::AngleFrame* const frame) {
        const auto tid = state.ThreadId();
        if (symbol == "glGenBuffers" || symbol == "glGenTextures") {
            auto call = PrepareCall(symbol, std::span(args).first<2>(), tid);
            auto names = symbol == "glGenBuffers"
                ? RequireFrame(frame, symbol).GenerateBuffers(args[0])
                : RequireFrame(frame, symbol).GenerateTextures(args[0]);
            WriteWords(Pointer(call), names);
            return 0;
        }
        if (symbol == "glDeleteBuffers" || symbol == "glDeleteTextures") {
            auto call = PrepareCall(symbol, std::span(args).first<2>(), tid);
            const auto names = ReadWords(Pointer(call));
            if (symbol == "glDeleteBuffers") {
                RequireFrame(frame, symbol).DeleteBuffers(names);
                const auto bound = transfer_state_.Snapshot();
                if (std::ranges::find(names, bound.array_buffer) != names.end()) {
                    transfer_state_.BindBuffer(0x8892U, 0);
                }
                if (std::ranges::find(names, bound.element_array_buffer) != names.end()) {
                    transfer_state_.BindBuffer(0x8893U, 0);
                }
            } else {
                RequireFrame(frame, symbol).DeleteTextures(names);
            }
            return 0;
        }
        if (symbol == "glBindBuffer") {
            auto next = transfer_state_;
            next.BindBuffer(args[0], args[1]);
            RequireFrame(frame, symbol).BindBuffer(args[0], args[1]);
            transfer_state_ = std::move(next);
            return 0;
        }
        if (symbol == "glBufferData") {
            auto call = PrepareCall(symbol, args, tid);
            const auto& data = Pointer(call);
            RequireFrame(frame, symbol).BufferData(
                args[0], args[1], OptionalBytes(data), args[3]);
            return 0;
        }
        if (symbol == "glActiveTexture") {
            RequireFrame(frame, symbol).ActiveTexture(args[0]);
            return 0;
        }
        if (symbol == "glBindTexture") {
            RequireFrame(frame, symbol).BindTexture(args[0], args[1]);
            return 0;
        }
        if (symbol == "glPixelStorei") {
            const auto value = std::bit_cast<std::int32_t>(args[1]);
            auto next = transfer_state_;
            next.PixelStore(args[0], value);
            RequireFrame(frame, symbol).PixelStore(args[0], value);
            transfer_state_ = std::move(next);
            return 0;
        }
        if (symbol == "glTexParameteri") {
            RequireFrame(frame, symbol).TextureParameter(
                args[0], args[1], std::bit_cast<std::int32_t>(args[2]));
            return 0;
        }
        if (symbol == "glTexImage2D") {
            std::array<std::uint32_t, 9> all{args[0], args[1], args[2], args[3]};
            for (std::size_t index = 4; index < all.size(); ++index) {
                all[index] = StackWord(state,
                    static_cast<std::uint32_t>((index - 4U) * 4U));
            }
            auto call = PrepareCall(symbol, all, tid);
            const auto& pixels = Pointer(call);
            RequireFrame(frame, symbol).TextureImage2D(
                all[0], std::bit_cast<std::int32_t>(all[1]),
                std::bit_cast<std::int32_t>(all[2]),
                std::bit_cast<std::int32_t>(all[3]),
                std::bit_cast<std::int32_t>(all[4]),
                std::bit_cast<std::int32_t>(all[5]), all[6], all[7],
                OptionalBytes(pixels));
            return 0;
        }
        if (symbol == "glEnableVertexAttribArray" ||
            symbol == "glDisableVertexAttribArray") {
            RequireFrame(frame, symbol).SetVertexAttributeEnabled(
                args[0], symbol == "glEnableVertexAttribArray");
            return 0;
        }
        if (symbol == "glVertexAttribPointer") {
            if (transfer_state_.Snapshot().array_buffer == 0) {
                throw std::runtime_error(
                    "glVertexAttribPointer client arrays are not implemented");
            }
            std::array<std::uint32_t, 6> all{
                args[0], args[1], args[2], args[3],
                StackWord(state, 0), StackWord(state, 4)};
            auto call = PrepareCall(symbol, all, tid);
            if (call.pointers.size() != 1 || !call.pointers.front().deferred) {
                throw std::logic_error(
                    "glVertexAttribPointer expected a deferred VBO offset");
            }
            RequireFrame(frame, symbol).VertexAttributePointer(
                all[0], std::bit_cast<std::int32_t>(all[1]), all[2],
                all[3] != 0, std::bit_cast<std::int32_t>(all[4]), all[5]);
            return 0;
        }
        if (symbol == "glUniform1f") {
            RequireFrame(frame, symbol).Uniform1f(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<float>(args[1]));
            return 0;
        }
        if (symbol == "glUniform1i") {
            RequireFrame(frame, symbol).Uniform1i(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]));
            return 0;
        }
        if (symbol == "glUniform4f") {
            RequireFrame(frame, symbol).Uniform4f(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<float>(args[1]), std::bit_cast<float>(args[2]),
                std::bit_cast<float>(args[3]),
                std::bit_cast<float>(StackWord(state, 0)));
            return 0;
        }
        if (symbol == "glUniformMatrix3fv") {
            auto call = PrepareCall(symbol, args, tid);
            const auto values = ReadFloats(Pointer(call));
            RequireFrame(frame, symbol).UniformMatrix3(
                std::bit_cast<std::int32_t>(args[0]),
                std::bit_cast<std::int32_t>(args[1]), args[2] != 0, values);
            return 0;
        }
        return std::nullopt;
    }

    void Reset() noexcept { transfer_state_ = {}; }

private:
    std::uint32_t StackWord(const cpu::A32State& state,
                            const std::uint32_t offset) const {
        std::array<std::byte, 4> bytes{};
        address_space_.Read(memory::GuestAddress{
            state.Register(cpu::CoreRegister::sp) + offset}, bytes,
            state.ThreadId());
        std::uint32_t value{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            value |= static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes[index])) << (index * 8U);
        }
        return value;
    }

    gles::PreparedGlesCall PrepareCall(
        const std::string_view symbol, const std::span<const std::uint32_t> args,
        const std::uint64_t tid) {
        const auto id = gles::GlesDispatchTable::Find(symbol);
        if (!id.has_value()) {
            throw std::logic_error("GLES resource handler is not cataloged");
        }
        return gles::PrepareGles2Call(
            address_space_, *id, args, tid, &transfer_state_);
    }

    static gles::GuestBuffer& Pointer(gles::PreparedGlesCall& call) {
        if (call.pointers.size() != 1 ||
            !call.pointers.front().transfer.has_value()) {
            throw std::logic_error(
                "GLES resource handler expected one transferred pointer");
        }
        return *call.pointers.front().transfer;
    }

    static std::vector<std::uint32_t> ReadWords(
        const gles::GuestBuffer& transfer) {
        const auto bytes = transfer.Bytes();
        if (bytes.size() % 4U != 0) {
            throw std::logic_error("GLES name array is misaligned");
        }
        std::vector<std::uint32_t> words(bytes.size() / 4U);
        for (std::size_t word = 0; word < words.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                words[word] |= static_cast<std::uint32_t>(
                    std::to_integer<std::uint8_t>(bytes[word * 4U + byte]))
                    << (byte * 8U);
            }
        }
        return words;
    }

    static std::vector<float> ReadFloats(const gles::GuestBuffer& transfer) {
        const auto words = ReadWords(transfer);
        std::vector<float> values;
        values.reserve(words.size());
        for (const auto word : words) {
            values.push_back(std::bit_cast<float>(word));
        }
        return values;
    }

    static void WriteWords(gles::GuestBuffer& transfer,
                           const std::span<const std::uint32_t> words) {
        auto bytes = transfer.WritableBytes();
        if (bytes.size() != words.size() * 4U) {
            throw std::logic_error("GLES name output size differs");
        }
        for (std::size_t word = 0; word < words.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                bytes[word * 4U + byte] =
                    static_cast<std::byte>(words[word] >> (byte * 8U));
            }
        }
        transfer.Commit();
    }

    static std::optional<std::span<const std::byte>> OptionalBytes(
        const gles::GuestBuffer& transfer) {
        if (transfer.IsNull()) return std::nullopt;
        return transfer.Bytes();
    }

    static gles::AngleFrame& RequireFrame(
        gles::AngleFrame* const frame, const std::string_view operation) {
        if (frame == nullptr) {
            throw std::runtime_error(
                std::string(operation) + " has no current ANGLE frame");
        }
        return *frame;
    }

    memory::AddressSpace& address_space_;
    gles::GlesTransferState transfer_state_;
};

AndroidBoundaryGles::AndroidBoundaryGles(memory::AddressSpace& address_space)
    : impl_(std::make_unique<Impl>(address_space)) {}
AndroidBoundaryGles::~AndroidBoundaryGles() = default;

std::optional<std::uint32_t> AndroidBoundaryGles::Dispatch(
    const std::string_view symbol,
    const std::array<std::uint32_t, 4>& arguments,
    const cpu::A32State& state, gles::AngleFrame* const frame) {
    return impl_->Dispatch(symbol, arguments, state, frame);
}

void AndroidBoundaryGles::Reset() noexcept { impl_->Reset(); }

}  // namespace ogplay::runtime
