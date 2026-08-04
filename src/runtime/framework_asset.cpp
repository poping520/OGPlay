#include "ogplay/runtime/framework_asset.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const FrameworkAssetErrorReason reason,
                       std::string message) {
    throw FrameworkAssetError(reason, std::move(message));
}

void AppendUtf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

[[nodiscard]] std::string AssetPath(const std::vector<JniChar>& value) {
    if (value.empty()) {
        Fail(FrameworkAssetErrorReason::invalid_asset_path,
             "asset path cannot be empty");
    }
    std::string relative;
    for (std::size_t index = 0; index < value.size(); ++index) {
        std::uint32_t code_point = value[index];
        if (code_point == 0 || code_point == '\\') {
            Fail(FrameworkAssetErrorReason::invalid_asset_path,
                 "asset path contains an invalid character");
        }
        if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
            if (++index >= value.size() || value[index] < 0xDC00U ||
                value[index] > 0xDFFFU) {
                Fail(FrameworkAssetErrorReason::invalid_asset_path,
                     "asset path contains an invalid UTF-16 surrogate");
            }
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) +
                         (value[index] - 0xDC00U);
        } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            Fail(FrameworkAssetErrorReason::invalid_asset_path,
                 "asset path contains an invalid UTF-16 surrogate");
        }
        AppendUtf8(relative, code_point);
    }
    if (relative.front() == '/' || relative.front() == '\\') {
        Fail(FrameworkAssetErrorReason::invalid_asset_path,
             "asset path must be relative");
    }
    std::size_t cursor = 0;
    while (cursor <= relative.size()) {
        const auto end = relative.find('/', cursor);
        const auto component = relative.substr(
            cursor, (end == std::string::npos ? relative.size() : end) - cursor);
        if (component.empty() || component == "." || component == "..") {
            Fail(FrameworkAssetErrorReason::invalid_asset_path,
                 "asset path contains an unsafe component");
        }
        if (end == std::string::npos) break;
        cursor = end + 1;
    }
    return "/apk/assets/" + relative;
}

[[nodiscard]] JniValue VoidResult() { return JniValue{std::monostate{}}; }

}  // namespace

FrameworkAssetError::FrameworkAssetError(
    const FrameworkAssetErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

FrameworkAssetErrorReason FrameworkAssetError::Reason() const noexcept {
    return reason_;
}

class FrameworkAssetHle::Impl final {
public:
    Impl(JniClassRegistry& classes, JniInvocationEngine& invocations,
         JniEnvironment& environment, JniStringStore& strings,
         JniPrimitiveArrayStore& arrays, VirtualFileSystem& vfs)
        : classes_(&classes),
          invocations_(&invocations),
          environment_(&environment),
          strings_(&strings),
          arrays_(&arrays),
          vfs_(&vfs),
          asset_manager_(AllocateJniHostObjectIdentity()) {}

    ~Impl() {
        std::scoped_lock lock(mutex_);
        for (auto& [identity, stream] : streams_) {
            static_cast<void>(identity);
            if (!stream.closed) {
                try {
                    vfs_->Close(stream.descriptor);
                } catch (...) {
                }
            }
        }
    }

    [[nodiscard]] FrameworkAssetClassSet Install() {
        if (installed_) {
            Fail(FrameworkAssetErrorReason::duplicate_install,
                 "framework asset HLE is already installed");
        }
        const auto object = classes_->FindClass("java/lang/Object");
        const auto context = classes_->FindClass("android/content/Context");
        if (!object.has_value() || !context.has_value()) {
            Fail(FrameworkAssetErrorReason::missing_framework,
                 "framework lifecycle classes must be installed first");
        }
        const auto input_stream = classes_->RegisterClass(
            {"java/io/InputStream",
             "java/lang/Object",
             {{"read", "([B)I", "framework.input_stream.read", false},
              {"available", "()I", "framework.input_stream.available", false},
              {"close", "()V", "framework.input_stream.close", false}},
             {}});
        const auto asset_manager = classes_->RegisterClass(
            {"android/content/res/AssetManager",
             "java/lang/Object",
             {{"open", "(Ljava/lang/String;)Ljava/io/InputStream;",
               "framework.asset_manager.open", false}},
             {}});
        invocations_->RegisterHandler(
            "framework.context.get_assets",
            [this](const JniInvocation& invocation) {
                static_cast<void>(Resolve(invocation.thread_id,
                                          invocation.receiver));
                return JniValue{environment_->PublishLocalObject(
                    invocation.thread_id, asset_manager_)};
            });
        invocations_->RegisterHandler(
            "framework.asset_manager.open",
            [this](const JniInvocation& invocation) { return Open(invocation); });
        invocations_->RegisterHandler(
            "framework.input_stream.read",
            [this](const JniInvocation& invocation) { return Read(invocation); });
        invocations_->RegisterHandler(
            "framework.input_stream.available",
            [this](const JniInvocation& invocation) {
                return Available(invocation);
            });
        invocations_->RegisterHandler(
            "framework.input_stream.close",
            [this](const JniInvocation& invocation) { return Close(invocation); });
        installed_ = true;
        classes_set_ = {asset_manager, input_stream};
        return classes_set_;
    }

private:
    struct Stream final {
        std::int32_t descriptor{};
        std::uint64_t size{};
        std::uint64_t offset{};
        bool closed{};
    };

    [[nodiscard]] JniObjectIdentity Resolve(
        const std::uint64_t thread_id, const JniReference reference) const {
        const auto object =
            environment_->ResolveObjectForHle(thread_id, reference);
        if (!object.has_value()) {
            Fail(FrameworkAssetErrorReason::invalid_argument,
                 "framework HLE object argument cannot be null");
        }
        return *object;
    }

    [[nodiscard]] JniValue Open(const JniInvocation& invocation) {
        if (Resolve(invocation.thread_id, invocation.receiver) !=
            asset_manager_) {
            Fail(FrameworkAssetErrorReason::invalid_argument,
                 "AssetManager receiver has an unknown identity");
        }
        const auto string = Resolve(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments.front()));
        const auto path = AssetPath(
            strings_->Region(string, 0, strings_->Length(string)));
        std::int32_t descriptor{};
        VfsFileInfo info;
        try {
            info = vfs_->Stat(path);
            if (info.source != VfsSource::apk) {
                Fail(FrameworkAssetErrorReason::io_failure,
                     "AssetManager path is not backed by the APK mount");
            }
            descriptor = vfs_->Open(path, {.read = true});
        } catch (const VfsError& error) {
            Fail(FrameworkAssetErrorReason::io_failure,
                 std::string("cannot open APK asset: ") + error.what());
        }
        const auto stream = AllocateJniHostObjectIdentity();
        {
            std::scoped_lock lock(mutex_);
            streams_.emplace(stream.value,
                             Stream{descriptor, info.size, 0, false});
        }
        try {
            return JniValue{environment_->PublishLocalObject(
                invocation.thread_id, stream)};
        } catch (...) {
            std::scoped_lock lock(mutex_);
            streams_.erase(stream.value);
            vfs_->Close(descriptor);
            throw;
        }
    }

    [[nodiscard]] Stream& RequireStream(const JniObjectIdentity identity) {
        if (identity.domain != JniObjectDomain::host) {
            Fail(FrameworkAssetErrorReason::unknown_stream,
                 "InputStream receiver is not a host HLE object");
        }
        const auto found = streams_.find(identity.value);
        if (found == streams_.end()) {
            Fail(FrameworkAssetErrorReason::unknown_stream,
                 "InputStream receiver is unknown");
        }
        if (found->second.closed) {
            Fail(FrameworkAssetErrorReason::closed_stream,
                 "InputStream is closed");
        }
        return found->second;
    }

    [[nodiscard]] JniValue Read(const JniInvocation& invocation) {
        const auto stream_identity =
            Resolve(invocation.thread_id, invocation.receiver);
        const auto array = Resolve(
            invocation.thread_id,
            std::get<JniReference>(invocation.arguments.front()));
        if (arrays_->Kind(array) != JniPrimitiveKind::byte) {
            Fail(FrameworkAssetErrorReason::invalid_argument,
                 "InputStream.read requires a byte array");
        }
        const auto length = arrays_->Length(array);
        if (length == 0) return JniValue{JniInt{0}};
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        std::size_t count{};
        {
            std::scoped_lock lock(mutex_);
            auto& stream = RequireStream(stream_identity);
            if (stream.offset == stream.size) return JniValue{JniInt{-1}};
            try {
                count = vfs_->Read(stream.descriptor, bytes);
            } catch (const VfsError& error) {
                Fail(FrameworkAssetErrorReason::io_failure,
                     std::string("cannot read APK asset: ") + error.what());
            }
            stream.offset += count;
        }
        std::vector<JniByte> output;
        output.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            output.push_back(static_cast<JniByte>(
                std::to_integer<std::uint8_t>(bytes[index])));
        }
        arrays_->SetRegion(array, 0, JniPrimitiveArrayData{std::move(output)});
        return JniValue{static_cast<JniInt>(count)};
    }

    [[nodiscard]] JniValue Available(const JniInvocation& invocation) {
        const auto identity = Resolve(invocation.thread_id, invocation.receiver);
        std::scoped_lock lock(mutex_);
        const auto& stream = RequireStream(identity);
        const auto remaining = stream.size - stream.offset;
        const auto maximum =
            static_cast<std::uint64_t>(std::numeric_limits<JniInt>::max());
        return JniValue{static_cast<JniInt>(std::min(remaining, maximum))};
    }

    [[nodiscard]] JniValue Close(const JniInvocation& invocation) {
        const auto identity = Resolve(invocation.thread_id, invocation.receiver);
        if (identity.domain != JniObjectDomain::host) {
            Fail(FrameworkAssetErrorReason::unknown_stream,
                 "InputStream receiver is not a host HLE object");
        }
        std::scoped_lock lock(mutex_);
        const auto found = streams_.find(identity.value);
        if (found == streams_.end()) {
            Fail(FrameworkAssetErrorReason::unknown_stream,
                 "InputStream receiver is unknown");
        }
        if (!found->second.closed) {
            try {
                vfs_->Close(found->second.descriptor);
            } catch (const VfsError& error) {
                Fail(FrameworkAssetErrorReason::io_failure,
                     std::string("cannot close APK asset: ") + error.what());
            }
            found->second.closed = true;
        }
        return VoidResult();
    }

    JniClassRegistry* classes_{};
    JniInvocationEngine* invocations_{};
    JniEnvironment* environment_{};
    JniStringStore* strings_{};
    JniPrimitiveArrayStore* arrays_{};
    VirtualFileSystem* vfs_{};
    JniObjectIdentity asset_manager_;
    bool installed_{};
    FrameworkAssetClassSet classes_set_{};
    std::mutex mutex_;
    std::map<std::uint64_t, Stream> streams_;
};

FrameworkAssetHle::FrameworkAssetHle(
    JniClassRegistry& classes, JniInvocationEngine& invocations,
    JniEnvironment& environment, JniStringStore& strings,
    JniPrimitiveArrayStore& arrays, VirtualFileSystem& vfs)
    : impl_(std::make_unique<Impl>(classes, invocations, environment, strings,
                                   arrays, vfs)) {}
FrameworkAssetHle::~FrameworkAssetHle() = default;
FrameworkAssetHle::FrameworkAssetHle(FrameworkAssetHle&&) noexcept = default;
FrameworkAssetHle& FrameworkAssetHle::operator=(
    FrameworkAssetHle&&) noexcept = default;
FrameworkAssetClassSet FrameworkAssetHle::Install() {
    return impl_->Install();
}

}  // namespace ogplay::runtime
