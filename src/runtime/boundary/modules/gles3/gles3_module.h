#pragma once

#include <array>
#include <algorithm>
#include <bit>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/gles/guest_transfer.h"
#include "runtime/boundary/core/a32_call_frame.h"
#include "runtime/boundary/core/boundary_binding.h"
#include "runtime/boundary/services/graphics_boundary_context.h"

namespace ogplay::runtime {

class Gles3Module final {
public:
    Gles3Module(BoundaryCallServices& calls,
                GraphicsBoundaryContext& graphics) noexcept
        : calls_(calls), graphics_(graphics) {}
    [[nodiscard]] BoundaryCallServices& CallServices() noexcept { return calls_; }
    void RetireShareGroup(const std::uint32_t group) noexcept {
        std::erase_if(mappings_, [group](const auto& entry) { return (entry.first >> 32U) == group; });
        std::erase_if(syncs_, [group](const auto& entry) { return entry.second.share_group == group; });
    }
    void MapGuestArena() {
        calls_.address_space.Map({memory::GuestAddress{kMapArenaBegin}, kMapArenaBytes},
            memory::PageProtection::read | memory::PageProtection::write);
        map_arena_mapped_ = true;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t Invoke(const A32CallFrame& call) {
        std::scoped_lock execution_lock(graphics_.execution_mutex);
        graphics_.ActivateCurrentContext();
        const auto version = graphics_.api_routing.CurrentVersion(call.ThreadId());
        if (!version.has_value() || *version != 3U) {
            graphics_.gl_context.Shared().SetGuestError(0x0502U);
            return 0U;
        }
        const auto symbol = gles::DescribeGlesFunction(
                                gles::GlesApi::gles3, FunctionId).name;
        if constexpr (FunctionId == 35U || FunctionId == 37U ||
                      FunctionId == 45U || FunctionId == 52U ||
                      FunctionId == 54U || FunctionId == 55U ||
                      FunctionId == 80U) {
            return InvokeComplex<FunctionId>(call, symbol);
        } else if constexpr (FunctionId == 39U || FunctionId == 65U ||
                             FunctionId == 96U || FunctionId == 29U) {
            return InvokeMapping<FunctionId>(call, symbol);
        } else if constexpr (FunctionId == 76U || FunctionId == 79U) {
            return InvokeTexture3D<FunctionId>(call, symbol);
        } else if constexpr (FunctionId == 24U || FunctionId == 25U ||
                      FunctionId == 102U) {
            return InvokeOffset<FunctionId>(call, symbol);
        } else if constexpr (FunctionId == 12U || FunctionId == 19U ||
                      FunctionId == 28U || FunctionId == 51U ||
                      FunctionId == 62U || FunctionId == 103U) {
            return InvokeSync<FunctionId>(call, symbol);
        } else if constexpr (FunctionId == 13U || FunctionId == 14U ||
                      FunctionId == 67U) {
            return InvokeBytes<FunctionId>(call, symbol);
        } else if constexpr (FunctionId == 38U || FunctionId == 41U ||
                      FunctionId == 42U) {
            return InvokeInteger64<FunctionId>(call, symbol);
        } else if constexpr (FunctionId == 40U || FunctionId == 50U ||
                      FunctionId == 53U) {
            return InvokeString<FunctionId>(call, symbol);
        } else if constexpr (IsNameOperation(FunctionId)) {
            return InvokeNames<FunctionId>(call, symbol);
        } else if constexpr (IsWordOperation(FunctionId)) {
            return InvokeWords<FunctionId>(call, symbol);
        } else if constexpr (!IsScalarImplemented(FunctionId)) {
            throw std::logic_error("GLES3 handler is not closed yet: " +
                                   std::string(symbol));
        }
        try {
            auto& frame = graphics_.RequireFrame(symbol);
            const auto result = frame.InvokeGles3Scalar(FunctionId, call.Arguments());
            if constexpr (FunctionId == 6U) {
                graphics_.gl_context.Programmable().BindVertexArray(call.Argument(0));
                graphics_.gl_context.Shared().transfer.BindBuffer(0x8893U, frame.BoundBuffer(0x8893U));
            } else if constexpr (FunctionId == 2U || FunctionId == 3U) {
                graphics_.gl_context.Shared().transfer.BindBuffer(call.Argument(0), call.Argument(2));
            } else if constexpr (FunctionId == 97U) {
                graphics_.gl_context.Programmable().attributes.at(call.Argument(0)).divisor = call.Argument(1);
            } else if constexpr (FunctionId == 98U || FunctionId == 100U) {
                auto& attribute = graphics_.gl_context.Programmable().attributes.at(call.Argument(0));
                attribute.current_kind = FunctionId == 98U ? 1U : 2U;
                for (std::size_t i = 0; i < 4; ++i) attribute.integer_current[i] = call.Argument(i + 1);
            }
            return result;
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
            return 0U;
        }
    }

private:
    static consteval bool IsScalarImplemented(const gles::GlesThunkId id) {
        constexpr std::array<gles::GlesThunkId, 38> ids{
            0, 1, 2, 3, 4, 5, 6, 7, 8, 15, 16, 22, 26, 27, 29, 30,
            60, 61, 63, 64, 66, 68, 69, 70, 71, 72, 74, 77, 78, 81,
            83, 85, 87, 89, 97, 98, 100};
        for (const auto candidate : ids) if (candidate == id) return true;
        return false;
    }

    static consteval bool IsNameOperation(const gles::GlesThunkId id) {
        return id == 17U || id == 18U || id == 20U || id == 21U ||
               id == 31U || id == 32U || id == 33U || id == 34U;
    }

    static consteval bool IsWordOperation(const gles::GlesThunkId id) {
        constexpr std::array<gles::GlesThunkId, 29> ids{
            9, 10, 11, 23, 36, 43, 44, 46, 47, 48, 49, 56, 57, 58, 59,
            73, 75, 82, 84, 86, 88, 90, 91, 92, 93, 94, 95, 99, 101};
        for (const auto candidate : ids) if (candidate == id) return true;
        return false;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeWords(const A32CallFrame& call,
                              const std::string_view symbol) {
        if constexpr (FunctionId == 48U || FunctionId == 49U || FunctionId == 73U || FunctionId == 75U) {
            constexpr std::array<std::uint32_t, 9> scalar_pnames{
                0x2800U, 0x2801U, 0x2802U, 0x2803U, 0x8072U, 0x813AU, 0x813BU, 0x884CU, 0x884DU};
            if (std::ranges::find(scalar_pnames, call.Argument(1)) == scalar_pnames.end()) {
                graphics_.gl_context.Shared().SetGuestError(0x0500U); return 0U;
            }
        }
        constexpr bool output = FunctionId == 36U || FunctionId == 43U ||
            FunctionId == 44U || FunctionId == 46U || FunctionId == 47U ||
            FunctionId == 48U || FunctionId == 49U || FunctionId == 56U ||
            FunctionId == 57U;
        std::size_t pointer_index{};
        std::size_t word_count{1U};
        if constexpr (FunctionId == 9U || FunctionId == 10U || FunctionId == 11U) {
            pointer_index = 2U; word_count = call.Argument(0) == 0x1800U ? 4U : 1U;
        } else if constexpr (FunctionId == 23U) {
            pointer_index = 1U; word_count = call.Argument(0);
        } else if constexpr (FunctionId == 36U) {
            pointer_index = 3U;
            if (call.Argument(2) == 0x8A43U) {
                std::array<std::uint32_t, 4> query{
                    call.Argument(0), call.Argument(1), 0x8A42U, 0U};
                std::array<std::uint32_t, 1> count{};
                try {
                    graphics_.RequireFrame(symbol).InvokeGles3Words(36U, query, count);
                } catch (const gles::GlesApiError& error) {
                    graphics_.gl_context.Shared().SetGuestError(error.Code());
                    return 0U;
                }
                word_count = count[0];
            }
        }
        else if constexpr (FunctionId == 43U) pointer_index = 2U;
        else if constexpr (FunctionId == 44U) {
            pointer_index = 4U; word_count = call.Argument(3);
        } else if constexpr (FunctionId >= 46U && FunctionId <= 49U) {
            pointer_index = 2U; word_count = 1U;
        } else if constexpr (FunctionId == 56U || FunctionId == 57U) {
            pointer_index = 2U; word_count = call.Argument(1) == 0x8626U ? 4U : 1U;
        } else if constexpr (FunctionId == 58U || FunctionId == 59U) {
            pointer_index = 2U; word_count = call.Argument(1);
        } else if constexpr (FunctionId == 73U || FunctionId == 75U) {
            pointer_index = 2U;
        } else if constexpr (FunctionId >= 82U && FunctionId <= 88U) {
            pointer_index = 2U;
            word_count = static_cast<std::size_t>(call.Argument(1)) *
                         ((FunctionId - 80U) / 2U);
        } else if constexpr (FunctionId >= 90U && FunctionId <= 95U) {
            constexpr std::array factors{6U, 8U, 6U, 12U, 8U, 12U};
            pointer_index = 3U;
            word_count = static_cast<std::size_t>(call.Argument(1)) *
                         factors[FunctionId - 90U];
        } else {
            pointer_index = 1U; word_count = 4U;
        }
        if (word_count > gles::kDefaultGuestTransferLimit / 4U) {
            graphics_.gl_context.Shared().SetGuestError(0x0501U);
            return 0U;
        }
        auto transfer = gles::GuestBuffer::Prepare(
            calls_.address_space, memory::GuestAddress{call.Argument(pointer_index)},
            word_count * sizeof(std::uint32_t),
            output ? gles::GuestTransferDirection::output
                   : gles::GuestTransferDirection::input,
            word_count == 0U, call.ThreadId());
        std::vector<std::uint32_t> words(word_count);
        if constexpr (!output) {
            std::memcpy(words.data(), transfer.Bytes().data(), transfer.Bytes().size());
        }
        try {
            graphics_.RequireFrame(symbol).InvokeGles3Words(
                FunctionId, call.Arguments(), words);
            if constexpr (FunctionId == 99U || FunctionId == 101U) {
                auto& attribute = graphics_.gl_context.Programmable().attributes.at(call.Argument(0));
                attribute.current_kind = FunctionId == 99U ? 1U : 2U;
                std::copy_n(words.begin(), 4, attribute.integer_current.begin());
            }
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
            return 0U;
        }
        if constexpr (output) {
            std::memcpy(transfer.WritableBytes().data(), words.data(),
                        words.size() * sizeof(std::uint32_t));
            transfer.Commit();
        }
        return 0U;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeOffset(const A32CallFrame& call,
                               const std::string_view symbol) {
        const auto transfer = graphics_.gl_context.Shared().transfer.Snapshot();
        const bool has_buffer = FunctionId == 102U
                                    ? transfer.array_buffer != 0U
                                    : transfer.element_array_buffer != 0U;
        if (!has_buffer) {
            graphics_.gl_context.Shared().SetGuestError(0x0502U);
            return 0U;
        }
        try {
            graphics_.RequireFrame(symbol).InvokeGles3Offset(
                FunctionId, call.Arguments());
            if constexpr (FunctionId == 102U) {
                auto& attribute = graphics_.gl_context.Programmable().attributes.at(call.Argument(0));
                attribute.size = call.Scalar<std::int32_t>(1);
                attribute.type = call.Argument(2);
                attribute.stride = call.Scalar<std::int32_t>(3);
                attribute.pointer = call.Argument(4);
                attribute.buffer = transfer.array_buffer;
                attribute.defined = true; attribute.integer = true;
            }
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
        }
        return 0U;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeSync(const A32CallFrame& call,
                             const std::string_view symbol) {
        auto& frame = graphics_.RequireFrame(symbol);
        try {
            if constexpr (FunctionId == 28U) {
                const auto native = frame.FenceSync(call.Argument(0), call.Argument(1));
                if (native == 0U) return 0U;
                const auto handle = next_sync_++;
                syncs_.emplace(handle, SyncState{native, graphics_.gl_context.ShareGroup()});
                return handle;
            } else {
                const auto found = syncs_.find(call.Argument(0));
                if (found == syncs_.end() || found->second.share_group != graphics_.gl_context.ShareGroup()) {
                    if constexpr (FunctionId == 62U) return 0U;
                    if constexpr (FunctionId == 19U) { if (call.Argument(0) == 0U) return 0U; }
                    graphics_.gl_context.Shared().SetGuestError(0x0501U);
                    return 0U;
                }
                const auto native = found->second.native;
                if constexpr (FunctionId == 19U) {
                    frame.DeleteSync(native); syncs_.erase(found); return 0U;
                } else if constexpr (FunctionId == 62U) {
                    return frame.IsSync(native) ? 1U : 0U;
                } else if constexpr (FunctionId == 12U || FunctionId == 103U) {
                    const auto timeout = static_cast<std::uint64_t>(call.Argument(2)) |
                                         (static_cast<std::uint64_t>(call.Argument(3)) << 32U);
                    if constexpr (FunctionId == 12U) {
                        return frame.ClientWaitSync(native, call.Argument(1), timeout);
                    } else {
                        frame.WaitSync(native, call.Argument(1), timeout); return 0U;
                    }
                } else {
                    const auto buffer_size = call.Scalar<std::int32_t>(2);
                    if (buffer_size < 0) {
                        graphics_.gl_context.Shared().SetGuestError(0x0501U);
                        return 0U;
                    }
                    auto length_out = gles::GuestBuffer::Prepare(
                        calls_.address_space, memory::GuestAddress{call.Argument(3)}, 4U,
                        gles::GuestTransferDirection::output, true, call.ThreadId());
                    auto values_out = gles::GuestBuffer::Prepare(
                        calls_.address_space, memory::GuestAddress{call.Argument(4)},
                        static_cast<std::uint64_t>(buffer_size) * 4U,
                        gles::GuestTransferDirection::output, buffer_size == 0,
                        call.ThreadId());
                    std::int32_t length{};
                    const auto values = frame.GetSyncValues(
                        native, call.Argument(1), buffer_size, length);
                    if (!length_out.IsNull()) {
                        std::memcpy(length_out.WritableBytes().data(), &length, 4U);
                        length_out.Commit();
                    }
                    if (!values_out.IsNull()) {
                        std::memcpy(values_out.WritableBytes().data(), values.data(),
                                    values.size() * 4U);
                        values_out.Commit();
                    }
                    return 0U;
                }
            }
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
            return 0U;
        }
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeBytes(const A32CallFrame& call,
                              const std::string_view symbol) {
        if constexpr (FunctionId == 13U || FunctionId == 14U) {
            if (graphics_.gl_context.Shared().transfer.BoundBuffer(0x88ECU) != 0U) {
                try {
                    graphics_.RequireFrame(symbol).TransferPixelBuffer(FunctionId == 13U ?
                        gles::AngleFrame::PixelBufferOperation::compressed3d :
                        gles::AngleFrame::PixelBufferOperation::compressed_sub3d, call.Arguments());
                } catch (const gles::GlesApiError& error) {
                    graphics_.gl_context.Shared().SetGuestError(error.Code());
                }
                return 0U;
            }
        }
        constexpr std::size_t pointer_index = FunctionId == 13U ? 8U :
                                              FunctionId == 14U ? 10U : 2U;
        constexpr std::size_t length_index = FunctionId == 13U ? 7U :
                                             FunctionId == 14U ? 9U : 3U;
        const auto length = call.Scalar<std::int32_t>(length_index);
        if (length < 0) {
            graphics_.gl_context.Shared().SetGuestError(0x0501U);
            return 0U;
        }
        std::vector<std::byte> storage;
        const auto bytes = gles::PrepareGuestInput(
            calls_.address_space,
            memory::GuestAddress{call.Argument(pointer_index)},
            static_cast<std::uint32_t>(length), length == 0, storage,
            call.ThreadId());
        try {
            graphics_.RequireFrame(symbol).InvokeGles3Bytes(
                FunctionId, call.Arguments(), bytes);
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
        }
        return 0U;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeInteger64(const A32CallFrame& call,
                                  const std::string_view symbol) {
        constexpr std::size_t pointer_index = FunctionId == 41U ? 2U :
                                              FunctionId == 42U ? 1U : 2U;
        const auto count = FunctionId == 42U ? graphics_.RequireFrame(symbol).StateQueryCount(call.Argument(0)) : 1U;
        auto transfer = gles::GuestBuffer::Prepare(
            calls_.address_space,
            memory::GuestAddress{call.Argument(pointer_index)}, count * 8U,
            gles::GuestTransferDirection::output, false, call.ThreadId());
        try {
            auto value = graphics_.RequireFrame(symbol).GetGles3Integer64(
                FunctionId, call.Arguments(), count);
            if constexpr (FunctionId == 42U) {
                if (call.Argument(0) == 0x821DU)
                    value[0] = static_cast<std::int64_t>(GuestGlesExtensions(graphics_.RequireFrame(symbol)).size());
            }
            std::memcpy(transfer.WritableBytes().data(), value.data(), value.size() * 8U);
            transfer.Commit();
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
        }
        return 0U;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeString(const A32CallFrame& call,
                               const std::string_view symbol) {
        auto& frame = graphics_.RequireFrame(symbol);
        try {
            if constexpr (FunctionId == 40U || FunctionId == 53U) {
                const auto name = graphics_.ReadCString(
                    call.Argument(1), 4096U, call.ThreadId(), symbol);
                if constexpr (FunctionId == 40U) {
                    return std::bit_cast<std::uint32_t>(
                        frame.GetFragDataLocation(call.Argument(0), name));
                } else {
                    return frame.GetUniformBlockIndex(call.Argument(0), name);
                }
            } else {
                if (call.Argument(0) != 0x1F03U) {
                    graphics_.gl_context.Shared().SetGuestError(0x0500U);
                    return 0U;
                }
                const auto extensions = GuestGlesExtensions(frame);
                if (call.Argument(1) >= extensions.size()) {
                    graphics_.gl_context.Shared().SetGuestError(0x0501U);
                    return 0U;
                }
                return PublishString(extensions[call.Argument(1)],
                                     call.Argument(1), call.ThreadId());
            }
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
            return 0U;
        }
    }

    std::uint32_t PublishString(const std::string_view value,
                                const std::uint32_t index,
                                const std::uint64_t thread_id) {
        constexpr memory::GuestAddress begin{0x71E00000U};
        constexpr std::uint32_t slot_size = 256U;
        if (index >= 256U || value.size() >= slot_size) {
            throw std::length_error("GLES3 indexed string exceeds guest arena");
        }
        const auto region = memory::GuestRange{begin, 65536U};
        if (!strings_mapped_) {
            calls_.address_space.Map(region, memory::PageProtection::read |
                                             memory::PageProtection::write);
            calls_.address_space.Protect(region, memory::PageProtection::read);
            strings_mapped_ = true;
        }
        const auto address = begin.Add(index * slot_size);
        const auto page = memory::GuestRange{
            memory::GuestAddress{address.Value() & ~0xFFFU}, 4096U};
        calls_.address_space.Protect(page, memory::PageProtection::read |
                                          memory::PageProtection::write);
        std::vector<std::byte> bytes(value.size() + 1U);
        std::memcpy(bytes.data(), value.data(), value.size());
        calls_.address_space.Write(address, bytes, thread_id);
        calls_.address_space.Protect(page, memory::PageProtection::read);
        return address.Value();
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeNames(const A32CallFrame& call,
                              const std::string_view symbol) {
        const auto count = call.Scalar<std::int32_t>(0);
        if (count < 0) {
            graphics_.gl_context.Shared().SetGuestError(0x0501U);
            return 0U;
        }
        std::vector<std::uint32_t> names(static_cast<std::size_t>(count));
        auto transfer = gles::GuestBuffer::Prepare(
            calls_.address_space, memory::GuestAddress{call.Argument(1)},
            static_cast<std::uint64_t>(names.size()) * sizeof(std::uint32_t),
            FunctionId < 31U ? gles::GuestTransferDirection::input
                             : gles::GuestTransferDirection::output,
            count == 0, call.ThreadId());
        if constexpr (FunctionId < 31U) {
            std::memcpy(names.data(), transfer.Bytes().data(), transfer.Bytes().size());
        }
        try {
            graphics_.RequireFrame(symbol).InvokeGles3Names(FunctionId, names);
            if constexpr (FunctionId == 21U) {
                auto& state = graphics_.gl_context.Programmable();
                if (state.vertex_array != 0U && std::ranges::find(names, state.vertex_array) != names.end())
                    state.BindVertexArray(0U);
                for (const auto name : names) if (name != 0U) state.vertex_arrays.erase(name);
                auto& frame = graphics_.RequireFrame(symbol);
                graphics_.gl_context.Shared().transfer.BindBuffer(0x8893U, frame.BoundBuffer(0x8893U));
            }
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
            return 0U;
        }
        if constexpr (FunctionId >= 31U) {
            std::memcpy(transfer.WritableBytes().data(), names.data(),
                        names.size() * sizeof(std::uint32_t));
            transfer.Commit();
        }
        return 0U;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeComplex(const A32CallFrame& call,
                                const std::string_view symbol) {
        auto& frame = graphics_.RequireFrame(symbol);
        try {
            if constexpr (FunctionId == 35U || FunctionId == 52U) {
                const auto capacity = call.Scalar<std::int32_t>(2);
                if (capacity < 0) { graphics_.gl_context.Shared().SetGuestError(0x0501U); return 0U; }
                auto length_out = PrepareOutput(call, 3U, 4U, true);
                auto name_out = PrepareOutput(call, FunctionId == 35U ? 4U : 6U,
                                               capacity, capacity == 0);
                std::optional<gles::GuestBuffer> size_out, type_out;
                if constexpr (FunctionId == 52U) {
                    size_out.emplace(PrepareOutput(call, 4U, 4U, false));
                    type_out.emplace(PrepareOutput(call, 5U, 4U, false));
                }
                gles::AngleActiveVariable varying;
                auto value = FunctionId == 35U
                    ? frame.GetActiveUniformBlockName(call.Argument(0), call.Argument(1), capacity)
                    : (varying = frame.GetTransformFeedbackVarying(
                           call.Argument(0), call.Argument(1)), varying.name);
                const auto copied = capacity == 0 ? 0U :
                    std::min<std::size_t>(value.size(), static_cast<std::size_t>(capacity - 1));
                const auto length = static_cast<std::int32_t>(copied);
                CommitScalar(length_out, length);
                if (!name_out.IsNull()) {
                    std::memcpy(name_out.WritableBytes().data(), value.data(), copied);
                    name_out.WritableBytes()[copied] = std::byte{}; name_out.Commit();
                }
                if constexpr (FunctionId == 52U) {
                    CommitScalar(*size_out, varying.size); CommitScalar(*type_out, varying.type);
                }
            } else if constexpr (FunctionId == 37U) {
                const auto count = CheckedCount(call.Scalar<std::int32_t>(1));
                auto indices_in = PrepareInput(call, 2U, count * 4U, count == 0U);
                auto params_out = PrepareOutput(call, 4U, count * 4U, count == 0U);
                std::vector<std::uint32_t> indices(count);
                std::memcpy(indices.data(), indices_in.Bytes().data(), count * 4U);
                const auto values = frame.GetActiveUniformValues(call.Argument(0), indices,
                                                                  call.Argument(3));
                CommitVector(params_out, values);
            } else if constexpr (FunctionId == 45U) {
                const auto capacity = call.Scalar<std::int32_t>(1);
                if (capacity < 0) { graphics_.gl_context.Shared().SetGuestError(0x0501U); return 0U; }
                auto length_out = PrepareOutput(call, 2U, 4U, true);
                auto format_out = PrepareOutput(call, 3U, 4U, false);
                auto binary_out = PrepareOutput(call, 4U, capacity, capacity == 0);
                std::int32_t length{}; std::uint32_t format{};
                const auto binary = frame.GetProgramBinary(call.Argument(0), capacity,
                                                            length, format);
                CommitScalar(length_out, length); CommitScalar(format_out, format);
                if (!binary_out.IsNull()) {
                    std::memcpy(binary_out.WritableBytes().data(), binary.data(), binary.size());
                    binary_out.Commit();
                }
            } else if constexpr (FunctionId == 54U || FunctionId == 80U) {
                const auto count = CheckedCount(call.Scalar<std::int32_t>(1));
                auto pointers_in = PrepareInput(call, 2U, count * 4U, count == 0U);
                std::vector<std::uint32_t> pointers(count);
                std::memcpy(pointers.data(), pointers_in.Bytes().data(), count * 4U);
                std::vector<std::string> names; names.reserve(count);
                for (const auto pointer : pointers) names.push_back(
                    graphics_.ReadCString(pointer, 4096U, call.ThreadId(), symbol));
                if constexpr (FunctionId == 54U) {
                    auto output = PrepareOutput(call, 3U, count * 4U, count == 0U);
                    CommitVector(output, frame.GetUniformIndices(call.Argument(0), names));
                } else {
                    frame.TransformFeedbackVaryings(call.Argument(0), names, call.Argument(3));
                }
            } else {
                const auto count = frame.UniformQueryCount(call.Argument(0), call.Scalar<std::int32_t>(1));
                auto output = PrepareOutput(call, 2U, count * 4U, false);
                CommitVector(output, frame.GetUniformUnsigned(
                    call.Argument(0), call.Scalar<std::int32_t>(1), count));
            }
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
        }
        return 0U;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeTexture3D(const A32CallFrame& call,
                                  const std::string_view symbol) {
        if (graphics_.gl_context.Shared().transfer.BoundBuffer(0x88ECU) != 0U) {
            try {
                graphics_.RequireFrame(symbol).TransferPixelBuffer(FunctionId == 76U ?
                    gles::AngleFrame::PixelBufferOperation::image3d :
                    gles::AngleFrame::PixelBufferOperation::sub3d, call.Arguments());
            } catch (const gles::GlesApiError& error) {
                graphics_.gl_context.Shared().SetGuestError(error.Code());
            }
            return 0U;
        }
        constexpr std::size_t width = FunctionId == 76U ? 3U : 5U;
        constexpr std::size_t format = FunctionId == 76U ? 7U : 8U;
        constexpr std::size_t pointer = FunctionId == 76U ? 9U : 10U;
        const auto bytes = graphics_.gl_context.Shared().transfer.UnpackBytes3D(
            call.Scalar<std::int32_t>(width), call.Scalar<std::int32_t>(width + 1U),
            call.Scalar<std::int32_t>(width + 2U), call.Argument(format),
            call.Argument(format + 1U));
        const bool nullable = FunctionId == 76U || bytes == 0U;
        auto input = gles::GuestBuffer::Prepare(calls_.address_space,
            memory::GuestAddress{call.Argument(pointer)}, bytes,
            gles::GuestTransferDirection::input, nullable, call.ThreadId());
        try {
            graphics_.RequireFrame(symbol).TextureImage3D(FunctionId, call.Arguments(),
                input.IsNull() ? std::nullopt
                               : std::optional<std::span<const std::byte>>(input.Bytes()));
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code());
        }
        return 0U;
    }

    template <gles::GlesThunkId FunctionId>
    std::uint32_t InvokeMapping(const A32CallFrame& call,
                                const std::string_view symbol) {
        auto& frame = graphics_.RequireFrame(symbol);
        try {
            if constexpr (FunctionId == 65U) {
                if (!map_arena_mapped_) throw std::logic_error("GLES3 map arena is unavailable");
                const auto length = call.Scalar<std::int32_t>(2);
                if (length < 0) { graphics_.gl_context.Shared().SetGuestError(0x0501U); return 0U; }
                const auto name = frame.BoundBuffer(call.Argument(0));
                const auto buffer = name == 0U ? 0ULL :
                    (static_cast<std::uint64_t>(graphics_.gl_context.ShareGroup()) << 32U) | name;
                if (buffer != 0U && frame.GetBufferParameter(call.Argument(0), 0x88BCU) == 0)
                    mappings_.erase(buffer);
                if (buffer == 0U || mappings_.contains(buffer)) {
                    graphics_.gl_context.Shared().SetGuestError(0x0502U);
                    return 0U;
                }
                const auto guest = AllocateMapping(static_cast<std::uint32_t>(length));
                if (guest == 0U) { graphics_.gl_context.Shared().SetGuestError(0x0505U); return 0U; }
                auto* host = frame.MapBufferRange(call.Argument(0),
                    call.Scalar<std::int32_t>(1), length, call.Argument(3));
                if (host == nullptr) return 0U;
                try {
                if ((call.Argument(3) & 0x0001U) != 0U && length != 0)
                    calls_.address_space.Write(memory::GuestAddress{guest},
                        {host, static_cast<std::size_t>(length)}, call.ThreadId());
                mappings_.emplace(buffer, Mapping{guest, static_cast<std::uint32_t>(length), host, call.Argument(3)});
                } catch (...) { static_cast<void>(frame.UnmapBuffer(call.Argument(0))); throw; }
                return guest;
            } else {
                const auto name = frame.BoundBuffer(call.Argument(0));
                const auto buffer = name == 0U ? 0ULL :
                    (static_cast<std::uint64_t>(graphics_.gl_context.ShareGroup()) << 32U) | name;
                if (buffer != 0U && frame.GetBufferParameter(call.Argument(0), 0x88BCU) == 0)
                    mappings_.erase(buffer);
                const auto found = mappings_.find(buffer);
                if (found != mappings_.end() && frame.MappedBufferPointer(call.Argument(0)) != found->second.host) {
                    graphics_.gl_context.Shared().SetGuestError(0x0502U); return 0U;
                }
                if constexpr (FunctionId == 39U) {
                    if (call.Argument(1) != 0x88BDU) {
                        graphics_.gl_context.Shared().SetGuestError(0x0500U);
                        return 0U;
                    }
                    auto output = PrepareOutput(call, 2U, 4U, false);
                    const std::uint32_t pointer = found == mappings_.end() ? 0U : found->second.guest;
                    CommitScalar(output, pointer); return 0U;
                }
                if (found == mappings_.end()) { graphics_.gl_context.Shared().SetGuestError(0x0502U); return 0U; }
                auto& mapping = found->second;
                if constexpr (FunctionId == 29U) {
                    if ((mapping.access & 0x0010U) == 0U) {
                        graphics_.gl_context.Shared().SetGuestError(0x0502U); return 0U;
                    }
                    const auto offset = call.Scalar<std::int32_t>(1);
                    const auto length = call.Scalar<std::int32_t>(2);
                    if (offset < 0 || length < 0 || static_cast<std::uint64_t>(offset) + length > mapping.length) {
                        graphics_.gl_context.Shared().SetGuestError(0x0501U); return 0U;
                    }
                    if ((mapping.access & 0x0002U) != 0U && length != 0)
                        calls_.address_space.Read(memory::GuestAddress{mapping.guest + offset},
                            {mapping.host + offset, static_cast<std::size_t>(length)}, call.ThreadId());
                    return frame.InvokeGles3Scalar(FunctionId, call.Arguments());
                } else {
                    if ((mapping.access & 0x0012U) == 0x0002U && mapping.length != 0U)
                        calls_.address_space.Read(memory::GuestAddress{mapping.guest},
                            {mapping.host, mapping.length}, call.ThreadId());
                    const auto result = frame.UnmapBuffer(call.Argument(0));
                    mappings_.erase(found);
                    return result ? 1U : 0U;
                }
            }
        } catch (const gles::GlesApiError& error) {
            graphics_.gl_context.Shared().SetGuestError(error.Code()); return 0U;
        }
    }

    gles::GuestBuffer PrepareOutput(const A32CallFrame& call, const std::size_t index,
        const std::uint64_t bytes, const bool nullable) {
        return gles::GuestBuffer::Prepare(calls_.address_space,
            memory::GuestAddress{call.Argument(index)}, bytes,
            gles::GuestTransferDirection::output, nullable, call.ThreadId());
    }
    gles::GuestBuffer PrepareInput(const A32CallFrame& call, const std::size_t index,
        const std::uint64_t bytes, const bool nullable) {
        return gles::GuestBuffer::Prepare(calls_.address_space,
            memory::GuestAddress{call.Argument(index)}, bytes,
            gles::GuestTransferDirection::input, nullable, call.ThreadId());
    }
    static std::size_t CheckedCount(const std::int32_t count) {
        if (count < 0 || static_cast<std::uint64_t>(count) > gles::kDefaultGuestTransferLimit / 4U)
            throw std::length_error("GLES3 count exceeds transfer limit");
        return static_cast<std::size_t>(count);
    }
    template <typename T> static void CommitScalar(gles::GuestBuffer& output, const T value) {
        if (output.IsNull()) return;
        std::memcpy(output.WritableBytes().data(), &value, sizeof(value)); output.Commit();
    }
    template <typename T> static void CommitVector(gles::GuestBuffer& output,
                                                    const std::vector<T>& values) {
        if (output.IsNull()) return;
        std::memcpy(output.WritableBytes().data(), values.data(), values.size() * sizeof(T));
        output.Commit();
    }

    BoundaryCallServices& calls_;
    GraphicsBoundaryContext& graphics_;
    bool strings_mapped_{};
    struct SyncState final {
        std::uintptr_t native{};
        std::uint32_t share_group{};
    };
    std::uint32_t next_sync_{1U};
    std::unordered_map<std::uint32_t, SyncState> syncs_;
    static constexpr std::uint32_t kMapArenaBegin = 0x78000000U;
    static constexpr std::uint32_t kMapArenaBytes = 0x01000000U;
    struct Mapping final { std::uint32_t guest{}, length{}; std::byte* host{}; std::uint32_t access{}; };
    bool map_arena_mapped_{};
    std::unordered_map<std::uint64_t, Mapping> mappings_;
    std::uint32_t AllocateMapping(const std::uint32_t length) const {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> occupied;
        for (const auto& [key, mapping] : mappings_) {
            static_cast<void>(key);
            occupied.emplace_back(mapping.guest - kMapArenaBegin, (mapping.length + 15U) & ~15U);
        }
        std::ranges::sort(occupied);
        std::uint32_t offset{};
        const auto size = (std::max)(length, 1U);
        for (const auto& [begin, count] : occupied) {
            if (size <= begin - offset) return kMapArenaBegin + offset;
            offset = begin + count;
        }
        return size <= kMapArenaBytes - offset ? kMapArenaBegin + offset : 0U;
    }
};

}  // namespace ogplay::runtime
