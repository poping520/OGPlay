#pragma once
#include "ogplay/runtime/jni/host_copy_payload_provider.h"

#include "boot_dex.h"
// Widget state, resolved UiTree geometry and listener dispatch integration.


#include <array>
#include <algorithm>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/loader/binary_xml.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/bitmap_pixels.h"
#include "ogplay/loader/apk.h"
#include "ogplay/runtime/ui/ui_renderer.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "ogplay/session/dex_activity_lifecycle.h"
#include "ogplay/video/fake_video_player.h"
#include "ogplay/audio/encoded_music.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace ogplay::test::widget {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
[[nodiscard]] ogplay::loader::BinaryXmlAttribute AndroidAttribute(
    std::string name, const std::uint8_t type, const std::uint32_t data);

[[nodiscard]] ogplay::loader::BinaryXmlAttribute StyleAttribute(
    const std::uint8_t type, const std::uint32_t data);

struct ClickVm final {
    ogplay::runtime::HostCopyJavaPayloadProvider strings_payload;
    JniStringStore strings{strings_payload};
    ogplay::runtime::HostCopyJavaPayloadProvider arrays_payload;
    JniPrimitiveArrayStore arrays{arrays_payload};
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    ogplay::core::Logger logger;
    std::shared_ptr<DexVmAndroidContext> context;
    VirtualFileSystem vfs;
    ogplay::audio::EncodedMusicMixer music;
    std::vector<ogplay::loader::BinaryXmlElement> inflater_elements;
    std::vector<int> inflater_finish_ids;
    std::function<void(IntrinsicContext&)> inflater_finish_hook;
    std::int32_t touch_dispatch_events{}, user_interactions{}, activity_touch_events{};
    bool intercept_touches{};
    std::int32_t intercept_events{};
    std::int32_t content_view_events{};
    std::int32_t content_change_events{};
    std::function<void(IntrinsicContext&)> content_change_hook;
    std::int32_t content_surface_creates{}, content_surface_destroys{};
    std::function<void()> content_surface_destroy_hook;
    std::int32_t dialog_creates{}, dialog_prepares{};
    std::vector<std::string> dialog_events;
    std::function<void(IntrinsicContext&)> dialog_click_hook;
    std::function<void(IntrinsicContext&)> dialog_prepare_hook;
    bool dialog_key_consumed{};
    std::int32_t removal_layout_requests{};
    std::int32_t bring_front_requests{};
    std::int32_t remove_all_in_layout_requests{};
    bool content_view_handled{};
    std::int32_t key_down_events{};
    std::int32_t key_up_events{};
    std::int32_t key_multiple_events{};
    std::int32_t last_key_code{};
    std::int32_t system_ui_visibility_events{};
    std::vector<std::string> text_events;
    std::int32_t image_resource_queries{};
    VmObjectRef image_delegate_exception;
    std::array<std::int32_t, 3> last_text_range{};
    Interpreter interpreter;
    std::unique_ptr<VmThreadRuntime> threads;
    VmObjectRef activity;
    VmObjectRef video_view;
    VmObjectRef skip_button;
    VmObjectRef other_button;

    explicit ClickVm(const InterpreterBackend backend = InterpreterBackend::switch_dispatch);
    VmValue CallOn(const VmObjectRef receiver, const std::string& name,
                   const std::string& descriptor,
                   std::vector<VmValue> arguments = {});
    VmObjectRef NewTextDialog(VmObjectRef owner, VmObjectRef listener, bool cancelable = true);
    VmValue CallDirect(const VmObjectRef receiver,
                       const std::string& class_descriptor,
                       const std::string& name,
                       const std::string& descriptor,
                       std::vector<VmValue> arguments = {});
    [[nodiscard]] VmObjectRef NewListener();
    [[nodiscard]] std::int32_t CallStaticInt(const std::string& name);
    void SetTouchResult(const bool handled);
    void BindTarget(const VmObjectRef view);

    [[nodiscard]] VmObjectRef NewTouchView();
    void ResetTouchView(const bool handled);
    [[nodiscard]] std::int32_t TouchViewValue(const char* name);

    [[nodiscard]] std::optional<std::string> Click(const float x,
                                                   const float y);
};


} // namespace ogplay::test::widget
