#include "ogplay/gles/angle_readback.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

#if OGPLAY_HAS_ANGLE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>
#undef GL_GLEXT_PROTOTYPES
#endif

namespace ogplay::gles {
namespace {
#if OGPLAY_HAS_ANGLE
bool HasToken(const char* text, const std::string_view token) {
    if (!text) return false;
    const std::string_view list(text);
    std::size_t start{};
    while (start < list.size()) {
        const auto end = list.find(' ', start);
        if (list.substr(start, end - start) == token) return true;
        if (end == list.npos) break;
        start = end + 1;
    }
    return false;
}
void CheckGl(const char* operation) {
    const auto error = glGetError();
    if (error != GL_NO_ERROR) throw GlesApiError(operation, error);
}

class PackScope final {
public:
    PackScope(const bool es3, const bool reverse) : es3_(es3), reverse_(reverse) {
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment_);
        glGetIntegerv(0x88EDU, &buffer_);
        if (es3_) for (std::size_t i = 0; i < names_.size(); ++i) {
            glGetIntegerv(names_[i], &values_[i]);
            glPixelStorei(names_[i], 0);
        }
        if (reverse_) {
            glGetIntegerv(GL_PACK_REVERSE_ROW_ORDER_ANGLE, &reverse_value_);
            glPixelStorei(GL_PACK_REVERSE_ROW_ORDER_ANGLE, GL_TRUE);
        }
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
    }
    ~PackScope() {
        glBindBuffer(0x88EBU, static_cast<GLuint>(buffer_));
        glPixelStorei(GL_PACK_ALIGNMENT, alignment_);
        if (es3_) for (std::size_t i = 0; i < names_.size(); ++i)
            glPixelStorei(names_[i], values_[i]);
        if (reverse_) glPixelStorei(GL_PACK_REVERSE_ROW_ORDER_ANGLE, reverse_value_);
    }
private:
    bool es3_, reverse_;
    GLint alignment_{}, buffer_{}, reverse_value_{};
    static constexpr std::array<GLenum, 3> names_{0x0D02U, 0x0D03U, 0x0D04U};
    std::array<GLint, 3> values_{};
};
#endif
}  // namespace

class AsyncAngleReadback::Impl final {
public:
    Impl(const AngleFrame& source, Publish publish)
        : info_(source.Info()), backend_(source.Backend()), version_(source.ClientVersion()),
          context_(source.NativeContext()), surface_(source.NativeSurface()),
          display_(source.NativeDisplay()), publish_(std::move(publish)) {
#if OGPLAY_HAS_ANGLE
        reverse_ = HasToken(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)),
                            "GL_ANGLE_pack_reverse_row_order");
        create_sync_ = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
        destroy_sync_ = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
        wait_sync_ = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(eglGetProcAddress("eglClientWaitSyncKHR"));
        if (!create_sync_ || !destroy_sync_ || !wait_sync_) throw std::runtime_error("presentation fence entry points unavailable");
        worker_ = std::thread([this] { Collect(); });
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [this] { return ready_ || failure_; });
        if (failure_) {
            lock.unlock();
            Stop();
            RethrowFailure();
        }
#endif
    }
    ~Impl() { Stop(); }
    bool Matches(const AngleFrame& source) const noexcept {
        const auto info = source.Info();
        return source.NativeContext() == context_ && source.NativeSurface() == surface_ &&
               source.NativeDisplay() == display_ && info.width == info_.width && info.height == info_.height;
    }
    bool Enqueue() {
#if OGPLAY_HAS_ANGLE
        // Retirement wakes backpressure before collector cleanup waits for
        // this producer scope. No buffer can be deleted during submission.
        std::unique_lock producer(producer_mutex_);
        std::size_t index{};
        {
            std::unique_lock lock(mutex_);
            changed_.wait(lock, [this] {
                return stopping_ || failure_ || slots_[0].free || slots_[1].free;
            });
            if (failure_) std::rethrow_exception(failure_);
            if (stopping_) return false;
            if (reinterpret_cast<EglHandle>(eglGetCurrentContext()) != context_ ||
                reinterpret_cast<EglHandle>(eglGetCurrentSurface(EGL_DRAW)) != surface_) {
                throw std::logic_error("presentation producer context/surface is not current");
            }
            index = slots_[0].free ? 0U : 1U;
            slots_[index].free = false;
        }
        try {
            PackScope pack(version_ >= 3, reverse_);
            glBindBuffer(0x88EBU, slots_[index].buffer);
            glReadPixels(0, 0, static_cast<GLsizei>(info_.width), static_cast<GLsizei>(info_.height),
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            CheckGl("asynchronous presentation readPixels");
            const EGLint attributes[]{EGL_NONE};
            auto fence = create_sync_(reinterpret_cast<EGLDisplay>(display_), EGL_SYNC_FENCE_KHR, attributes);
            if (fence == EGL_NO_SYNC_KHR) throw std::runtime_error("presentation fence creation failed");
            slots_[index].fence = fence;
            glFlush();
            CheckGl("asynchronous presentation flush");
            {
                std::scoped_lock lock(mutex_);
                if (!stopping_) pending_.push_back(index);
            }
            changed_.notify_all();
            return true;
        } catch (...) {
            std::scoped_lock lock(mutex_);
            failure_ = std::current_exception();
            changed_.notify_all();
            throw;
        }
#else
        return false;
#endif
    }
    void RethrowFailure() const {
        std::scoped_lock lock(mutex_);
        if (failure_) std::rethrow_exception(failure_);
    }
    void Stop() {
        std::scoped_lock join(join_mutex_);
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        changed_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
private:
    void Collect() noexcept {
#if OGPLAY_HAS_ANGLE
        std::optional<AngleFrame> collector;
        try {
            collector.emplace(AngleFrame::CreatePbuffer(backend_, 1, 1, version_, context_));
            if (collector->NativeDisplay() != display_) throw std::runtime_error("presentation collector display differs");
            for (auto& slot : slots_) {
                glGenBuffers(1, &slot.buffer);
                glBindBuffer(0x88EBU, slot.buffer);
                glBufferData(0x88EBU, static_cast<GLsizeiptr>(ByteSize()), nullptr, 0x88E1U);
                CheckGl("presentation buffer allocation");
            }
            glBindBuffer(0x88EBU, 0);
            {
                std::scoped_lock lock(mutex_);
                ready_ = true;
            }
            changed_.notify_all();
            for (;;) {
                std::size_t index{};
                {
                    std::unique_lock lock(mutex_);
                    changed_.wait(lock, [this] { return stopping_ || failure_ || !pending_.empty(); });
                    if (stopping_ || failure_) break;
                    index = pending_.front();
                    pending_.pop_front();
                }
                auto& slot = slots_[index];
                // Completion polling never holds the producer/GL mutex. The
                // finite wait also lets Stop cancel a queued GPU job promptly.
                for (;;) {
                    {
                        std::scoped_lock lock(mutex_);
                        if (stopping_ || failure_) break;
                    }
                    const auto status = wait_sync_(reinterpret_cast<EGLDisplay>(display_), slot.fence, 0, 1'000'000U);
                    if (status == EGL_CONDITION_SATISFIED_KHR) break;
                    if (status != EGL_TIMEOUT_EXPIRED_KHR) throw std::runtime_error("presentation fence wait failed");
                }
                {
                    std::scoped_lock lock(mutex_);
                    if (stopping_ || failure_) break;
                }
                glBindBuffer(0x88EBU, slot.buffer);
                auto* mapped = version_ >= 3 ? collector->MapBufferRange(0x88EBU, 0, static_cast<std::int32_t>(ByteSize()), 1U)
                    : static_cast<std::byte*>(glMapBufferRangeEXT(0x88EBU, 0, static_cast<GLsizeiptr>(ByteSize()), GL_MAP_READ_BIT_EXT));
                CheckGl("presentation collector map");
                if (!mapped) throw std::runtime_error("presentation collector map returned null");
                std::vector<std::uint8_t> pixels(ByteSize());
                std::memcpy(pixels.data(), mapped, pixels.size());
                const auto valid = version_ >= 3 ? collector->UnmapBuffer(0x88EBU) : glUnmapBufferOES(0x88EBU) == GL_TRUE;
                CheckGl("presentation collector unmap");
                if (!valid) throw std::runtime_error("presentation collector buffer invalidated");
                if (!reverse_) {
                    const auto row_bytes = static_cast<std::size_t>(info_.width) * 4U;
                    for (std::size_t top = 0, bottom = info_.height - 1U; top < bottom; ++top, --bottom)
                        std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(top * row_bytes),
                            pixels.begin() + static_cast<std::ptrdiff_t>((top + 1U) * row_bytes),
                            pixels.begin() + static_cast<std::ptrdiff_t>(bottom * row_bytes));
                }
                if (!destroy_sync_(reinterpret_cast<EGLDisplay>(display_), slot.fence)) throw std::runtime_error("presentation fence destruction failed");
                slot.fence = EGL_NO_SYNC_KHR;
                publish_(std::move(pixels));
                {
                    std::scoped_lock lock(mutex_);
                    slot.free = true;
                }
                changed_.notify_all();
            }
        } catch (...) {
            std::scoped_lock lock(mutex_);
            if (!failure_) failure_ = std::current_exception();
            changed_.notify_all();
        }
        // Collector owns object deletion and native context teardown. Protect
        // against a producer that was already inside glReadPixels at Stop.
        std::scoped_lock producer(producer_mutex_);
        if (collector) {
            for (auto& slot : slots_) {
                if (slot.fence != EGL_NO_SYNC_KHR) destroy_sync_(reinterpret_cast<EGLDisplay>(display_), slot.fence);
                if (slot.buffer) glDeleteBuffers(1, &slot.buffer);
            }
        }
#endif
    }
    std::size_t ByteSize() const { return static_cast<std::size_t>(info_.width) * info_.height * 4U; }
    AngleFrameInfo info_;
    AngleBackend backend_;
    int version_;
    EglHandle context_, surface_, display_;
    Publish publish_;
    mutable std::mutex mutex_;
    std::mutex producer_mutex_, join_mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    bool ready_{}, stopping_{};
    std::exception_ptr failure_;
#if OGPLAY_HAS_ANGLE
    struct Slot { GLuint buffer{}; EGLSyncKHR fence{EGL_NO_SYNC_KHR}; bool free{true}; };
    std::array<Slot, 2> slots_;
    std::deque<std::size_t> pending_;
    bool reverse_{};
    PFNEGLCREATESYNCKHRPROC create_sync_{};
    PFNEGLDESTROYSYNCKHRPROC destroy_sync_{};
    PFNEGLCLIENTWAITSYNCKHRPROC wait_sync_{};
#endif
};

std::shared_ptr<AsyncAngleReadback> AsyncAngleReadback::Create(const AngleFrame& source, Publish publish) {
#if OGPLAY_HAS_ANGLE
    if (!source.IsCurrentOnCallingThread()) throw std::logic_error("presentation source is not current");
    const auto info = source.Info();
    const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    const bool es3 = source.ClientVersion() >= 3;
    if (info.width == 0 || info.height == 0 ||
        static_cast<std::uint64_t>(info.width) * info.height * 4U > 64U * 1024U * 1024U ||
        source.Backend().renderer != AngleRenderer::metal ||
        !(es3 || (HasToken(extensions, "GL_NV_pixel_buffer_object") &&
                  HasToken(extensions, "GL_EXT_map_buffer_range") && HasToken(extensions, "GL_OES_mapbuffer"))) ||
        !HasToken(eglQueryString(reinterpret_cast<EGLDisplay>(source.NativeDisplay()), EGL_EXTENSIONS), "EGL_KHR_fence_sync")) return nullptr;
    return std::shared_ptr<AsyncAngleReadback>(new AsyncAngleReadback(std::make_unique<Impl>(source, std::move(publish))));
#else
    static_cast<void>(source); static_cast<void>(publish); return nullptr;
#endif
}
AsyncAngleReadback::AsyncAngleReadback(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AsyncAngleReadback::~AsyncAngleReadback() = default;
bool AsyncAngleReadback::Matches(const AngleFrame& source) const noexcept { return impl_->Matches(source); }
bool AsyncAngleReadback::Enqueue() { return impl_->Enqueue(); }
void AsyncAngleReadback::RethrowFailure() const { impl_->RethrowFailure(); }
void AsyncAngleReadback::Stop() { impl_->Stop(); }
}  // namespace ogplay::gles
