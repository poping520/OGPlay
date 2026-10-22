#pragma once
#include "ogplay/runtime/jni/java_payload.h"

namespace ogplay::runtime {
// Explicit fixture/headless backing. Address() always fails.
class HostCopyJavaPayloadProvider final : public JavaPayloadProvider {
public:
    HostCopyJavaPayloadProvider();
    ~HostCopyJavaPayloadProvider() override;
    JavaPayloadHandle Allocate(std::size_t bytes) override;
    void Release(const JavaPayloadHandle&) override;
    JavaPayloadBorrow Pin(const JavaPayloadHandle&) override;
    void Unpin(const JavaPayloadBorrow&) override;
    JavaPayloadStatistics Statistics() const override;

private:
    const void* OwnerIdentity() const noexcept override;
    class Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace ogplay::runtime
