#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/session/profile_entry_scope.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadInterpreterFixture() {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/interp.dex";
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    return {std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
}

struct Vm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model{strings, arrays};
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    Vm()
        : interpreter(
              [this]() -> DexClassLinker& {
                  auto catalog = CoreIntrinsicCatalog();
                  IntrinsicClassDecl presets;
                  presets.descriptor = "LFixturePresets;";
                  presets.superclass = "Ljava/lang/Object;";
                  presets.fields = {
                      {"z", "Z", true, false, 0, {}},
                      {"b", "B", true, false, 0, {}},
                      {"c", "C", true, false, 0, {}},
                      {"s", "S", true, false, 0, {}},
                      {"i", "I", true, false, 0, {}},
                      {"j", "J", true, false, 0, {}},
                      {"f", "F", true, false, 0, {}},
                      {"d", "D", true, false, 0, {}},
                      {"text", "Ljava/lang/String;", true, false, 0, {}},
                      {"instance", "I", false, false, 0, {}},
                  };
                  catalog.push_back(std::move(presets));
                  linker.RegisterIntrinsics(catalog);
                  linker.RegisterDex(ReadInterpreterFixture());
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger) {}
};

struct AndroidVm final {
  JniStringStore strings;
  JniPrimitiveArrayStore arrays;
  JavaObjectModel model{strings, arrays};
  DexClassLinker linker;
  std::shared_ptr<ogplay::runtime::DexVmAndroidContext> context{
      std::make_shared<ogplay::runtime::DexVmAndroidContext>()};
  ogplay::core::CapabilityLedger ledger;
  Interpreter interpreter;

  AndroidVm()
      : interpreter(
            [this]() -> DexClassLinker & {
              auto catalog = CoreIntrinsicCatalog();
              const auto android = ogplay::runtime::AndroidIntrinsicCatalog(context);
              catalog.insert(catalog.end(), android.begin(), android.end());
              linker.RegisterIntrinsics(catalog);
              linker.RegisterDex(ReadInterpreterFixture());
              linker.Link();
              return linker;
            }(),
            model, nullptr, ledger) {}

  [[nodiscard]] VmMethodId Virtual(const std::string_view descriptor,
                                   const std::string_view name,
                                   const std::string_view signature) {
    const auto owner = linker.FindClass(descriptor);
    REQUIRE(owner.has_value());
    const auto index = linker.FindVtableIndex(*owner, std::string(name),
                                              std::string(signature));
    REQUIRE(index.has_value());
    return linker.Class(*owner).vtable[*index];
  }
};

TEST_CASE("android intrinsic catalog is unique and directly bound") {
  auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
  const auto catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
  CHECK(catalog.size() == 173);

  std::unordered_set<std::string> descriptors;
  for (const auto& declaration : catalog) {
    CHECK(descriptors.insert(declaration.descriptor).second);
    for (const auto& method : declaration.methods) {
      CHECK(static_cast<bool>(method.implementation));
    }
    if (declaration.clinit_implementation) {
      CHECK(static_cast<bool>(declaration.clinit_implementation));
    }
  }

  const auto method_count = [&catalog](const std::string_view descriptor) {
    const auto found = std::find_if(
        catalog.begin(), catalog.end(), [descriptor](const auto& declaration) {
          return declaration.descriptor == descriptor;
        });
    REQUIRE(found != catalog.end());
    return found->methods.size();
  };
  CHECK(method_count("Landroid/app/Activity;") == 22);
  CHECK(method_count("Landroid/content/Intent;") == 15);
  CHECK(method_count("Landroid/os/Bundle;") == 12);
  CHECK(method_count("Landroid/widget/TextView;") == 14);
  CHECK(method_count("Ljavax/microedition/khronos/egl/EGL10;") == 25);
  CHECK(method_count("Ljavax/microedition/khronos/egl/EGL10$Impl;") == 25);
}

ogplay::session::TitleProfile PresetProfile() {
    ogplay::session::TitleProfile profile;
    profile.schema = 2;
    profile.runtime.presets.push_back({
        .class_name = "Counter",
        .field = "seed",
        .type = "I",
        .value = std::int64_t{42},
        .reason = "fixture data is provisioned",
    });
    return profile;
}

}  // namespace

TEST_CASE("Profile entry resolves override before manifest launcher") {
    ogplay::session::TitleProfile profile;
    profile.runtime.entry =
        ogplay::session::ProfileEntry{"org.example.EngineActivity"};
    CHECK(ogplay::session::ResolveProfileLaunchDescriptor(
              profile, std::string{"org.example.StoreActivity"}) ==
          "Lorg/example/EngineActivity;");
    profile.runtime.entry.reset();
    CHECK(ogplay::session::ResolveProfileLaunchDescriptor(
              profile, std::string{"org.example.StoreActivity"}) ==
          "Lorg/example/StoreActivity;");
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::session::ResolveProfileLaunchDescriptor(
            profile, std::nullopt)),
        ogplay::session::ProfileEntryScopeError);
}

TEST_CASE("Profile static preset applies only after guest clinit") {
    Vm without_preset;
    const auto counter = without_preset.linker.FindClass("LCounter;");
    REQUIRE(counter.has_value());
    const auto initialized =
        without_preset.interpreter.EnsureClassInitialized(*counter);
    REQUIRE_FALSE(initialized.exception.IsValid());
    CHECK(without_preset.linker.Class(*counter).static_storage[0] == 7U);

    Vm with_preset;
    ogplay::core::Logger logger;
  ogplay::session::ApplyProfileStaticPresets(PresetProfile(),
                                             with_preset.interpreter, &logger);
    const auto applied = with_preset.linker.FindClass("LCounter;");
    REQUIRE(applied.has_value());
    CHECK(with_preset.linker.Class(*applied).clinit_state ==
          ClinitState::initialized);
    CHECK(with_preset.linker.Class(*applied).static_storage[0] == 42U);
  const auto records =
      logger.Snapshot(ogplay::core::LogLevel::info, "session.profile_entry");
    REQUIRE(records.size() == 1U);
    CHECK(records.front().message == "applied static field preset");
}

TEST_CASE("Profile static preset rejects missing guest facts") {
    Vm vm;
    auto missing_class = PresetProfile();
    missing_class.runtime.presets.front().class_name = "Missing";
    CHECK_THROWS_WITH_AS(
      ogplay::session::ApplyProfileStaticPresets(missing_class, vm.interpreter),
        "Profile preset class is not in the dex: LMissing;",
        ogplay::session::ProfileEntryScopeError);

    auto wrong_field = PresetProfile();
    wrong_field.runtime.presets.front().field = "absent";
    CHECK_THROWS_AS(
      ogplay::session::ApplyProfileStaticPresets(wrong_field, vm.interpreter),
        ogplay::session::ProfileEntryScopeError);
}

TEST_CASE("Profile static presets preserve every supported field type") {
    Vm vm;
    ogplay::session::TitleProfile profile;
    profile.schema = 2;
    const auto add = [&](const char* field, const char* type,
                         ogplay::session::ProfileStaticPresetValue value) {
        profile.runtime.presets.push_back({
            .class_name = "FixturePresets",
            .field = field,
            .type = type,
            .value = std::move(value),
            .reason = "fixture fact",
        });
    };
    add("z", "Z", true);
    add("b", "B", std::int64_t{-12});
    add("c", "C", std::int64_t{0x4E2D});
    add("s", "S", std::int64_t{-1234});
    add("i", "I", std::int64_t{-123456});
    add("j", "J", std::int64_t{0x123456789LL});
    add("f", "F", 1.25);
    add("d", "D", -2.5);
    add("text", "Ljava/lang/String;", std::string{"ready"});
    ogplay::session::ApplyProfileStaticPresets(profile, vm.interpreter);

    const auto owner = vm.linker.FindClass("LFixturePresets;");
    REQUIRE(owner.has_value());
    const auto bits = [&](const char* field, const char* type) {
        const auto id = vm.linker.FindFieldRecursive(*owner, field, type);
        REQUIRE(id.has_value());
        const auto& linked = vm.linker.Field(*id);
        const auto& storage = vm.linker.Class(*owner).static_storage;
        std::uint64_t result = storage[linked.slot];
        if (linked.is_wide) {
      result |= static_cast<std::uint64_t>(storage[linked.slot + 1U]) << 32U;
        }
        return result;
    };
    CHECK(bits("z", "Z") == 1U);
    CHECK(static_cast<std::int8_t>(bits("b", "B")) == -12);
    CHECK(bits("c", "C") == 0x4E2DU);
    CHECK(static_cast<std::int16_t>(bits("s", "S")) == -1234);
    CHECK(static_cast<std::int32_t>(bits("i", "I")) == -123456);
    CHECK(static_cast<std::int64_t>(bits("j", "J")) == 0x123456789LL);
    CHECK(std::bit_cast<float>(static_cast<std::uint32_t>(bits("f", "F"))) ==
          doctest::Approx(1.25F));
    CHECK(std::bit_cast<double>(bits("d", "D")) == doctest::Approx(-2.5));
  CHECK(vm.interpreter.StringUtf8(VmObjectRef(static_cast<std::uint32_t>(
                  bits("text", "Ljava/lang/String;")))) == "ready");

    auto instance = profile;
    instance.runtime.presets.front().field = "instance";
    instance.runtime.presets.front().type = "I";
    instance.runtime.presets.front().value = std::int64_t{1};
    CHECK_THROWS_AS(
        ogplay::session::ApplyProfileStaticPresets(instance, vm.interpreter),
        ogplay::session::ProfileEntryScopeError);
}

TEST_CASE("AudioManager reports the deterministic external music fact") {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model(strings, arrays);
    DexClassLinker linker;
    auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
    auto catalog = CoreIntrinsicCatalog();
    const auto android_catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
  catalog.insert(catalog.end(), android_catalog.begin(), android_catalog.end());
    linker.RegisterIntrinsics(catalog);
    linker.RegisterDex(ReadInterpreterFixture());
    linker.Link();
    ogplay::core::CapabilityLedger ledger;
  Interpreter interpreter(linker, model, nullptr, ledger);
  const auto audio_class = linker.FindClass("Landroid/media/AudioManager;");
    REQUIRE(audio_class.has_value());
  const auto index =
      linker.FindVtableIndex(*audio_class, "isMusicActive", "()Z");
    REQUIRE(index.has_value());
  const auto instance =
      interpreter.NewIntrinsicInstance("Landroid/media/AudioManager;");
  const auto outcome =
      interpreter.Call(linker.Class(*audio_class).vtable[*index],
        std::vector<VmValue>{VmValue::Ref(instance)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK(outcome.value.AsInt() == 0);
}

TEST_CASE("TelephonyManager records and cancels offline listeners") {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model(strings, arrays);
    DexClassLinker linker;
    auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
    auto catalog = CoreIntrinsicCatalog();
    const auto android_catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
  catalog.insert(catalog.end(), android_catalog.begin(), android_catalog.end());
    linker.RegisterIntrinsics(catalog);
    linker.RegisterDex(ReadInterpreterFixture());
    linker.Link();
    ogplay::core::CapabilityLedger ledger;
  Interpreter interpreter(linker, model, nullptr, ledger);
    const auto manager_class =
        linker.FindClass("Landroid/telephony/TelephonyManager;");
    REQUIRE(manager_class.has_value());
    const auto index = linker.FindVtableIndex(
      *manager_class, "listen", "(Landroid/telephony/PhoneStateListener;I)V");
    REQUIRE(index.has_value());
  const auto manager =
      interpreter.NewIntrinsicInstance("Landroid/telephony/TelephonyManager;");
    const auto listener = interpreter.NewIntrinsicInstance(
        "Landroid/telephony/PhoneStateListener;");
    const auto target = linker.Class(*manager_class).vtable[*index];
    auto outcome = interpreter.Call(
        target, std::vector<VmValue>{VmValue::Ref(manager),
                                   VmValue::Ref(listener), VmValue::Int(32)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK(context->telephony_listeners.at(listener.Value()) == 32);
    outcome = interpreter.Call(
        target, std::vector<VmValue>{VmValue::Ref(manager),
                                   VmValue::Ref(listener), VmValue::Int(0)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK_FALSE(context->telephony_listeners.contains(listener.Value()));

  const auto sim_state =
      linker.FindVtableIndex(*manager_class, "getSimState", "()I");
  const auto phone_type =
      linker.FindVtableIndex(*manager_class, "getPhoneType", "()I");
    REQUIRE(sim_state.has_value());
    REQUIRE(phone_type.has_value());
  outcome = interpreter.Call(linker.Class(*manager_class).vtable[*sim_state],
        std::vector<VmValue>{VmValue::Ref(manager)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK(outcome.value.AsInt() == 1);
  outcome = interpreter.Call(linker.Class(*manager_class).vtable[*phone_type],
        std::vector<VmValue>{VmValue::Ref(manager)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK(outcome.value.AsInt() == 0);
}

TEST_CASE("SurfaceView owns a stable holder and callback identity") {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model(strings, arrays);
    DexClassLinker linker;
    auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
    auto catalog = CoreIntrinsicCatalog();
    const auto android_catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
  catalog.insert(catalog.end(), android_catalog.begin(), android_catalog.end());
    linker.RegisterIntrinsics(catalog);
    linker.RegisterDex(ReadInterpreterFixture());
    linker.Link();
    ogplay::core::CapabilityLedger ledger;
  Interpreter interpreter(linker, model, nullptr, ledger);
    const auto view_class = linker.FindClass("Landroid/view/SurfaceView;");
    REQUIRE(view_class.has_value());
    const auto get_holder = linker.FindVtableIndex(
        *view_class, "getHolder", "()Landroid/view/SurfaceHolder;");
    REQUIRE(get_holder.has_value());
    const auto view =
        interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
    const auto target = linker.Class(*view_class).vtable[*get_holder];
  const auto first =
      interpreter.Call(target, std::vector<VmValue>{VmValue::Ref(view)});
  const auto second =
      interpreter.Call(target, std::vector<VmValue>{VmValue::Ref(view)});
    REQUIRE_FALSE(first.exception.IsValid());
    REQUIRE_FALSE(second.exception.IsValid());
    CHECK(first.value.ref.IsValid());
    CHECK(first.value.ref == second.value.ref);

  const auto holder_class = linker.FindClass("Landroid/view/SurfaceHolder;");
    REQUIRE(holder_class.has_value());
    const auto add_callback = linker.FindVtableIndex(
      *holder_class, "addCallback", "(Landroid/view/SurfaceHolder$Callback;)V");
    REQUIRE(add_callback.has_value());
    const auto callback =
        interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
  const auto added =
      interpreter.Call(linker.Class(*holder_class).vtable[*add_callback],
        std::vector<VmValue>{VmValue::Ref(first.value.ref),
                             VmValue::Ref(callback)});
    REQUIRE_FALSE(added.exception.IsValid());
    CHECK(context->surface_callbacks.at(first.value.ref.Value()) ==
          std::vector<VmObjectRef>{callback});
}

TEST_CASE("managed surface delivers holder callbacks to every registration") {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model(strings, arrays);
    DexClassLinker linker;
    auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
    auto catalog = CoreIntrinsicCatalog();
    const auto android_catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
    catalog.insert(catalog.end(), android_catalog.begin(),
                   android_catalog.end());
    linker.RegisterIntrinsics(catalog);
    linker.RegisterDex(ReadInterpreterFixture());
    linker.Link();
    context->surface_width = 1280;
    context->surface_height = 720;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter(linker, model, nullptr, ledger);

    const auto probe_class = linker.FindClass("LSurfaceProbe;");
    REQUIRE(probe_class.has_value());
    const auto make =
        linker.FindDirectMethod(*probe_class, "make", "()Ljava/lang/Object;");
    REQUIRE(make.has_value());
    const auto read = [&](const std::string& name) {
        const auto method = linker.FindDirectMethod(*probe_class, name, "()I");
        REQUIRE(method.has_value());
        const auto outcome = interpreter.Call(*method, {});
        REQUIRE_FALSE(outcome.exception.IsValid());
        return outcome.value.AsInt();
    };

    const auto view =
        interpreter.NewIntrinsicInstance("Landroid/view/SurfaceView;");
    const auto view_class = linker.FindClass("Landroid/view/SurfaceView;");
    REQUIRE(view_class.has_value());
    const auto get_holder = linker.FindVtableIndex(
        *view_class, "getHolder", "()Landroid/view/SurfaceHolder;");
    REQUIRE(get_holder.has_value());
    const auto holder =
        interpreter.Call(linker.Class(*view_class).vtable[*get_holder],
                         std::vector<VmValue>{VmValue::Ref(view)});
    REQUIRE_FALSE(holder.exception.IsValid());

    const auto holder_class = linker.FindClass("Landroid/view/SurfaceHolder;");
    REQUIRE(holder_class.has_value());
    const auto add_callback = linker.FindVtableIndex(
        *holder_class, "addCallback",
        "(Landroid/view/SurfaceHolder$Callback;)V");
    REQUIRE(add_callback.has_value());
    const auto add = [&](const VmObjectRef callback) {
        return interpreter.Call(
            linker.Class(*holder_class).vtable[*add_callback],
            std::vector<VmValue>{VmValue::Ref(holder.value.ref),
                                 VmValue::Ref(callback)});
    };

    // These titles register both the view and the activity on one holder.
    const auto first = interpreter.Call(*make, {}).value.ref;
    const auto second = interpreter.Call(*make, {}).value.ref;
    REQUIRE_FALSE(add(first).exception.IsValid());
    REQUIRE_FALSE(add(second).exception.IsValid());
    // addCallback is idempotent per callback, so events are not doubled.
    REQUIRE_FALSE(add(first).exception.IsValid());
    CHECK(context->surface_callbacks.at(holder.value.ref.Value()).size() == 2);
    REQUIRE(add(VmObjectRef{}).exception.IsValid());

    CHECK_FALSE(ogplay::runtime::DispatchSurfaceHolderCallbacks(
                    interpreter, *context,
                    ogplay::runtime::SurfaceHolderPhase::created)
                    .has_value());
    CHECK(read("created") == 2);

    CHECK_FALSE(ogplay::runtime::DispatchSurfaceHolderCallbacks(
                    interpreter, *context,
                    ogplay::runtime::SurfaceHolderPhase::changed)
                    .has_value());
    CHECK(read("width") == 1280);
    CHECK(read("height") == 720);

    CHECK_FALSE(ogplay::runtime::DispatchSurfaceHolderCallbacks(
                    interpreter, *context,
                    ogplay::runtime::SurfaceHolderPhase::destroyed)
                    .has_value());
    CHECK(read("destroyed") == 1);

    // A registration that cannot receive the event is reported, not skipped.
    context->surface_callbacks[holder.value.ref.Value()] = {view};
    CHECK(ogplay::runtime::DispatchSurfaceHolderCallbacks(
              interpreter, *context,
              ogplay::runtime::SurfaceHolderPhase::created)
              .has_value());
}

TEST_CASE("Thread priority validates and records the guest fact") {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model(strings, arrays);
    DexClassLinker linker;
    auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
    auto catalog = CoreIntrinsicCatalog();
    const auto android_catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
  catalog.insert(catalog.end(), android_catalog.begin(), android_catalog.end());
    linker.RegisterIntrinsics(catalog);
    linker.RegisterDex(ReadInterpreterFixture());
    linker.Link();
    ogplay::core::CapabilityLedger ledger;
  Interpreter interpreter(linker, model, nullptr, ledger);
    const auto thread_class = linker.FindClass("Ljava/lang/Thread;");
    REQUIRE(thread_class.has_value());
  const auto set_priority =
      linker.FindVtableIndex(*thread_class, "setPriority", "(I)V");
    REQUIRE(set_priority.has_value());
  const auto thread = interpreter.NewIntrinsicInstance("Ljava/lang/Thread;");
    const auto target = linker.Class(*thread_class).vtable[*set_priority];
    auto outcome = interpreter.Call(
        target, std::vector<VmValue>{VmValue::Ref(thread), VmValue::Int(7)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK(context->java_threads.at(thread.Value()).priority == 7);

  const auto get_id = linker.FindVtableIndex(*thread_class, "getId", "()J");
  const auto set_name =
      linker.FindVtableIndex(*thread_class, "setName", "(Ljava/lang/String;)V");
  const auto get_name =
      linker.FindVtableIndex(*thread_class, "getName", "()Ljava/lang/String;");
  REQUIRE(get_id.has_value());
  REQUIRE(set_name.has_value());
  REQUIRE(get_name.has_value());
  outcome = interpreter.Call(linker.Class(*thread_class).vtable[*get_id],
                             std::vector<VmValue>{VmValue::Ref(thread)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(outcome.value.AsLong() == static_cast<std::int64_t>(thread.Value()));
  outcome = interpreter.Call(
      linker.Class(*thread_class).vtable[*set_name],
      std::vector<VmValue>{VmValue::Ref(thread),
                           VmValue::Ref(interpreter.NewStringUtf8("render"))});
  REQUIRE_FALSE(outcome.exception.IsValid());
  outcome = interpreter.Call(linker.Class(*thread_class).vtable[*get_name],
                             std::vector<VmValue>{VmValue::Ref(thread)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(interpreter.StringUtf8(outcome.value.ref) == "render");

    outcome = interpreter.Call(
        target, std::vector<VmValue>{VmValue::Ref(thread), VmValue::Int(11)});
    CHECK(outcome.exception.IsValid());
    CHECK(context->java_threads.at(thread.Value()).priority == 7);
}

TEST_CASE("ViewTreeObserver retains stable observer and listener identity") {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model(strings, arrays);
    DexClassLinker linker;
    auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
    auto catalog = CoreIntrinsicCatalog();
    const auto android_catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
  catalog.insert(catalog.end(), android_catalog.begin(), android_catalog.end());
    linker.RegisterIntrinsics(catalog);
    linker.RegisterDex(ReadInterpreterFixture());
    linker.Link();
    ogplay::core::CapabilityLedger ledger;
  Interpreter interpreter(linker, model, nullptr, ledger);
    const auto view_class = linker.FindClass("Landroid/view/View;");
    REQUIRE(view_class.has_value());
    const auto get_observer = linker.FindVtableIndex(
      *view_class, "getViewTreeObserver", "()Landroid/view/ViewTreeObserver;");
    REQUIRE(get_observer.has_value());
    const auto view = interpreter.NewIntrinsicInstance("Landroid/view/View;");
    const auto get_target = linker.Class(*view_class).vtable[*get_observer];
  const auto first =
      interpreter.Call(get_target, std::vector<VmValue>{VmValue::Ref(view)});
  const auto second =
      interpreter.Call(get_target, std::vector<VmValue>{VmValue::Ref(view)});
    REQUIRE_FALSE(first.exception.IsValid());
    REQUIRE_FALSE(second.exception.IsValid());
    CHECK(first.value.ref == second.value.ref);

    const auto observer_class =
        linker.FindClass("Landroid/view/ViewTreeObserver;");
    REQUIRE(observer_class.has_value());
    const auto add_listener = linker.FindVtableIndex(
        *observer_class, "addOnGlobalLayoutListener",
        "(Landroid/view/ViewTreeObserver$OnGlobalLayoutListener;)V");
    REQUIRE(add_listener.has_value());
  const auto listener = interpreter.NewIntrinsicInstance("Landroid/view/View;");
  const auto added =
      interpreter.Call(linker.Class(*observer_class).vtable[*add_listener],
        std::vector<VmValue>{VmValue::Ref(first.value.ref),
                             VmValue::Ref(listener)});
    REQUIRE_FALSE(added.exception.IsValid());
    CHECK(context->global_layout_listeners.at(first.value.ref.Value()) ==
          listener);
}

TEST_CASE("URLEncoder applies UTF-8 form encoding") {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model(strings, arrays);
    DexClassLinker linker;
    auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
    auto catalog = CoreIntrinsicCatalog();
    const auto android_catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
  catalog.insert(catalog.end(), android_catalog.begin(), android_catalog.end());
    linker.RegisterIntrinsics(catalog);
    linker.RegisterDex(ReadInterpreterFixture());
    linker.Link();
    ogplay::core::CapabilityLedger ledger;
  Interpreter interpreter(linker, model, nullptr, ledger);
    const auto encoder = linker.FindClass("Ljava/net/URLEncoder;");
    REQUIRE(encoder.has_value());
    const auto encode = linker.FindDirectMethod(
        *encoder, "encode",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    REQUIRE(encode.has_value());
    auto outcome = interpreter.Call(
      *encode, std::vector<VmValue>{
            VmValue::Ref(interpreter.NewStringUtf8("a b+c/\xC3\xA9")),
            VmValue::Ref(interpreter.NewStringUtf8("UTF-8"))});
    REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(interpreter.StringUtf8(outcome.value.ref) == "a+b%2Bc%2F%C3%A9");

    outcome = interpreter.Call(
        *encode,
      std::vector<VmValue>{VmValue::Ref(interpreter.NewStringUtf8("value")),
            VmValue::Ref(interpreter.NewStringUtf8("UTF-16"))});
    CHECK(outcome.exception.IsValid());
}

TEST_CASE("Bundle preserves typed values and clear state") {
  AndroidVm vm;
  const auto bundle =
      vm.interpreter.NewIntrinsicInstance("Landroid/os/Bundle;");
  const auto key = vm.interpreter.NewStringUtf8("count");
  auto outcome = vm.interpreter.Call(
      vm.Virtual("Landroid/os/Bundle;", "putInt", "(Ljava/lang/String;I)V"),
      std::vector<VmValue>{VmValue::Ref(bundle), VmValue::Ref(key),
                           VmValue::Int(37)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  outcome = vm.interpreter.Call(
      vm.Virtual("Landroid/os/Bundle;", "containsKey", "(Ljava/lang/String;)Z"),
      std::vector<VmValue>{VmValue::Ref(bundle), VmValue::Ref(key)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(outcome.value.AsInt() == 1);
  outcome = vm.interpreter.Call(
      vm.Virtual("Landroid/os/Bundle;", "getInt", "(Ljava/lang/String;)I"),
      std::vector<VmValue>{VmValue::Ref(bundle), VmValue::Ref(key)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(outcome.value.AsInt() == 37);
  outcome =
      vm.interpreter.Call(vm.Virtual("Landroid/os/Bundle;", "clear", "()V"),
                          std::vector<VmValue>{VmValue::Ref(bundle)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(vm.context->bundles.at(bundle.Value()).empty());
}

TEST_CASE("SAX setup retains its handler and parsing fails explicitly") {
  AndroidVm vm;
  const auto factory_class =
      vm.linker.FindClass("Ljavax/xml/parsers/SAXParserFactory;");
  REQUIRE(factory_class.has_value());
  const auto new_instance = vm.linker.FindDirectMethod(
      *factory_class, "newInstance", "()Ljavax/xml/parsers/SAXParserFactory;");
  REQUIRE(new_instance.has_value());
  auto outcome = vm.interpreter.Call(*new_instance, {});
  REQUIRE_FALSE(outcome.exception.IsValid());
  const auto factory = outcome.value.ref;
  outcome = vm.interpreter.Call(
      vm.Virtual("Ljavax/xml/parsers/SAXParserFactory;", "newSAXParser",
                 "()Ljavax/xml/parsers/SAXParser;"),
      std::vector<VmValue>{VmValue::Ref(factory)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  const auto parser = outcome.value.ref;
  outcome = vm.interpreter.Call(vm.Virtual("Ljavax/xml/parsers/SAXParser;",
                                           "getXMLReader",
                                           "()Lorg/xml/sax/XMLReader;"),
                                std::vector<VmValue>{VmValue::Ref(parser)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  const auto reader = outcome.value.ref;
  const auto handler =
      vm.interpreter.NewIntrinsicInstance("Landroid/view/View;");
  outcome = vm.interpreter.Call(
      vm.Virtual("Lorg/xml/sax/XMLReader$Impl;", "setContentHandler",
                 "(Lorg/xml/sax/ContentHandler;)V"),
      std::vector<VmValue>{VmValue::Ref(reader), VmValue::Ref(handler)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(vm.context->sax_content_handlers.at(reader.Value()) == handler);
  const auto input =
      vm.interpreter.NewIntrinsicInstance("Lorg/xml/sax/InputSource;");
  outcome = vm.interpreter.Call(
      vm.Virtual("Lorg/xml/sax/XMLReader$Impl;", "parse",
                 "(Lorg/xml/sax/InputSource;)V"),
      std::vector<VmValue>{VmValue::Ref(reader), VmValue::Ref(input)});
  REQUIRE(outcome.exception.IsValid());
  CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
        "Ljava/lang/UnsupportedOperationException;");
}

TEST_CASE("bounded method reflection enumerates and invokes display rotation") {
  AndroidVm vm;
  const auto display_class = vm.linker.FindClass("Landroid/view/Display;");
  REQUIRE(display_class.has_value());
  const auto display_class_object = vm.model.ClassObject(*display_class);
  auto outcome = vm.interpreter.Call(
      vm.Virtual("Ljava/lang/Class;", "getDeclaredMethods",
                 "()[Ljava/lang/reflect/Method;"),
      std::vector<VmValue>{VmValue::Ref(display_class_object)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  const auto methods = outcome.value.ref;
  VmObjectRef rotation;
  for (JniSize index = 0; index < vm.model.ArrayLength(methods); ++index) {
    const auto method = vm.model.GetObjectElement(methods, index);
    const auto named =
        vm.interpreter.Call(vm.Virtual("Ljava/lang/reflect/Method;", "getName",
                                       "()Ljava/lang/String;"),
                            std::vector<VmValue>{VmValue::Ref(method)});
    REQUIRE_FALSE(named.exception.IsValid());
    if (vm.interpreter.StringUtf8(named.value.ref) == "getRotation") {
      rotation = method;
      break;
    }
  }
  REQUIRE(rotation.IsValid());
  const auto display =
      vm.interpreter.NewIntrinsicInstance("Landroid/view/Display;");
  const auto object_class = vm.linker.ResolveDescriptor("Ljava/lang/Object;");
  const auto object_array_class =
      vm.linker.ResolveDescriptor("[Ljava/lang/Object;");
  const auto no_arguments =
      vm.model.NewObjectArray(object_array_class, object_class, 0);
  outcome = vm.interpreter.Call(
      vm.Virtual("Ljava/lang/reflect/Method;", "invoke",
                 "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;"),
      std::vector<VmValue>{VmValue::Ref(rotation), VmValue::Ref(display),
                           VmValue::Ref(no_arguments)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  const auto boxed = outcome.value.ref;
  outcome =
      vm.interpreter.Call(vm.Virtual("Ljava/lang/Integer;", "intValue", "()I"),
                          std::vector<VmValue>{VmValue::Ref(boxed)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(outcome.value.AsInt() == 0);
}
