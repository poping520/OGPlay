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
#include "ogplay/runtime/dexvm/vm_threads.h"
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

  explicit AndroidVm(InterpreterConfig config = {})
      : interpreter(
            [this]() -> DexClassLinker & {
              auto catalog = CoreIntrinsicCatalog(
                  AndroidCoreIntrinsicServices(context));
              const auto android = ogplay::runtime::AndroidIntrinsicCatalog(context);
              catalog.insert(catalog.end(), android.begin(), android.end());
              linker.RegisterIntrinsics(catalog);
              linker.RegisterDex(ReadInterpreterFixture());
              linker.Link();
              return linker;
            }(),
            model, nullptr, ledger, config) {}

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

  [[nodiscard]] VmMethodId Direct(const std::string_view descriptor,
                                  const std::string_view signature) {
    const auto owner = linker.FindClass(descriptor);
    REQUIRE(owner.has_value());
    const auto method = linker.FindDirectMethod(
        *owner, "<init>", std::string(signature));
    REQUIRE(method.has_value());
    return *method;
  }

  void AttachBase(const VmObjectRef object, const VmObjectRef base) {
    const auto java_class = model.ObjectClass(object);
    const auto index = linker.FindVtableIndex(
        java_class, "attachBaseContext", "(Landroid/content/Context;)V");
    REQUIRE(index.has_value());
    const auto outcome = interpreter.Call(
        linker.Class(java_class).vtable[*index],
        std::vector{VmValue::Ref(object), VmValue::Ref(base)});
    REQUIRE_FALSE(outcome.exception.IsValid());
  }
};

TEST_CASE("android intrinsic catalog is unique and directly bound") {
  auto context = std::make_shared<ogplay::runtime::DexVmAndroidContext>();
  const auto catalog = ogplay::runtime::AndroidIntrinsicCatalog(context);
    CHECK(catalog.size() == 185);

  std::unordered_set<std::string> descriptors;
  for (const auto& declaration : catalog) {
    CHECK_FALSE(declaration.descriptor.starts_with("Ljava/io/"));
    CHECK_FALSE(declaration.descriptor.starts_with("Ljava/util/zip/"));
    CHECK_FALSE(declaration.descriptor.starts_with("Ljava/net/"));
    CHECK_FALSE(declaration.descriptor.starts_with("Ljava/nio/"));
    CHECK_FALSE(declaration.descriptor.starts_with("Ljava/util/"));
    CHECK_FALSE(declaration.descriptor.starts_with("Ljavax/net/"));
    CHECK_FALSE(declaration.descriptor.starts_with("Ljavax/xml/"));
    CHECK_FALSE(declaration.descriptor.starts_with("Lorg/xml/"));
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
  CHECK(method_count("Landroid/app/Application;") == 2);
  CHECK(method_count("Landroid/app/Activity;") == 26);
  CHECK(method_count("Landroid/app/Service;") == 13);
  CHECK(method_count("Landroid/content/ContextWrapper;") == 18);
  CHECK(method_count("Landroid/view/ContextThemeWrapper;") == 4);
  CHECK(method_count("Landroid/content/Intent;") == 16);
  CHECK(method_count("Landroid/os/Bundle;") == 17);
  CHECK(method_count("Landroid/content/pm/PackageManager;") == 5);
  CHECK(method_count("Landroid/widget/TextView;") == 15);
  CHECK(method_count("Ljavax/microedition/khronos/egl/EGL10;") == 25);
  CHECK(method_count("Ljavax/microedition/khronos/egl/EGL10$Impl;") == 25);
}

TEST_CASE("API 19 Context wrappers publish the real superclass chain") {
  AndroidVm vm;
  const auto require_class = [&vm](const std::string_view descriptor) {
    const auto java_class = vm.linker.FindClass(descriptor);
    REQUIRE(java_class.has_value());
    return *java_class;
  };
  const auto context = require_class("Landroid/content/Context;");
  const auto wrapper = require_class("Landroid/content/ContextWrapper;");
  const auto themed = require_class("Landroid/view/ContextThemeWrapper;");
  const auto application = require_class("Landroid/app/Application;");
  const auto activity = require_class("Landroid/app/Activity;");
  const auto service = require_class("Landroid/app/Service;");
  const auto intent_service = require_class("Landroid/app/IntentService;");

  CHECK(vm.linker.Class(wrapper).super == context);
  CHECK(vm.linker.Class(themed).super == wrapper);
  CHECK(vm.linker.Class(application).super == wrapper);
  CHECK(vm.linker.Class(activity).super == themed);
  CHECK(vm.linker.Class(service).super == wrapper);
  CHECK(vm.linker.Class(intent_service).super == service);
  CHECK(vm.linker.IsAssignable(context, application));
  CHECK(vm.linker.IsAssignable(context, activity));
  CHECK(vm.linker.IsAssignable(wrapper, activity));
  CHECK(vm.linker.IsAssignable(themed, activity));
  CHECK(vm.linker.IsAssignable(wrapper, intent_service));
}

TEST_CASE("ContextWrapper owns one base identity and delegates supported calls") {
  AndroidVm vm;
  vm.context->package_name = "org.example.wrapper";
  const auto base =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
  const auto wrapper = vm.interpreter.NewIntrinsicInstance(
      "Landroid/content/ContextWrapper;");
  auto outcome = vm.interpreter.Call(
      vm.Direct("Landroid/content/ContextWrapper;",
                "(Landroid/content/Context;)V"),
      std::vector{VmValue::Ref(wrapper), VmValue::Ref(base)});
  REQUIRE_FALSE(outcome.exception.IsValid());

  outcome = vm.interpreter.Call(
      vm.Virtual("Landroid/content/ContextWrapper;", "getBaseContext",
                 "()Landroid/content/Context;"),
      std::vector{VmValue::Ref(wrapper)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(outcome.value.ref == base);

  outcome = vm.interpreter.Call(
      vm.Virtual("Landroid/content/ContextWrapper;", "getPackageName",
                 "()Ljava/lang/String;"),
      std::vector{VmValue::Ref(wrapper)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(vm.interpreter.StringUtf8(outcome.value.ref) ==
        "org.example.wrapper");

  outcome = vm.interpreter.Call(
      vm.Virtual("Landroid/content/ContextWrapper;", "attachBaseContext",
                 "(Landroid/content/Context;)V"),
      std::vector{VmValue::Ref(wrapper), VmValue::Ref(base)});
  REQUIRE(outcome.exception.IsValid());
  CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
        "Ljava/lang/IllegalStateException;");
}

TEST_CASE("ContextThemeWrapper keeps bounded theme state on the same base") {
  AndroidVm vm;
  const auto base =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
  const auto themed = vm.interpreter.NewIntrinsicInstance(
      "Landroid/view/ContextThemeWrapper;");
  auto outcome = vm.interpreter.Call(
      vm.Direct("Landroid/view/ContextThemeWrapper;",
                "(Landroid/content/Context;I)V"),
      std::vector{VmValue::Ref(themed), VmValue::Ref(base),
                  VmValue::Int(0x1234)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  outcome = vm.interpreter.Call(
      vm.Virtual("Landroid/view/ContextThemeWrapper;", "getBaseContext",
                 "()Landroid/content/Context;"),
      std::vector{VmValue::Ref(themed)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(outcome.value.ref == base);
  outcome = vm.interpreter.Call(
      vm.Virtual("Landroid/view/ContextThemeWrapper;", "getThemeResId",
                 "()I"),
      std::vector{VmValue::Ref(themed)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(outcome.value.AsInt() == 0x1234);
}

TEST_CASE("Context package manager has one process identity") {
  AndroidVm vm;
  const auto activity =
      vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
  const auto context =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
  vm.AttachBase(activity, context);
  const auto activity_target = vm.Virtual(
      "Landroid/app/Activity;", "getPackageManager",
      "()Landroid/content/pm/PackageManager;");

  const auto from_activity = vm.interpreter.Call(
      activity_target, std::vector{VmValue::Ref(activity)});
  REQUIRE_FALSE(from_activity.exception.IsValid());
  REQUIRE(from_activity.value.ref.IsValid());
  const auto from_context = vm.interpreter.Call(
      vm.Virtual("Landroid/content/Context;", "getPackageManager",
                 "()Landroid/content/pm/PackageManager;"),
      std::vector{VmValue::Ref(context)});
  REQUIRE_FALSE(from_context.exception.IsValid());
  CHECK(from_context.value.ref == from_activity.value.ref);
  CHECK(vm.linker.Class(vm.model.ObjectClass(from_activity.value.ref))
            .descriptor == "Landroid/content/pm/PackageManager;");
}

TEST_CASE("PackageManager P0 exposes only explicit current-package facts") {
  for (const auto backend : {InterpreterBackend::switch_dispatch,
                             InterpreterBackend::threaded}) {
    InterpreterConfig config;
    config.backend = backend;
    AndroidVm vm(config);
    vm.context->package_name = "org.example.game";
    vm.context->package_version_code = 7U;
    vm.context->package_version_name = "1.2.3";
    vm.context->target_sdk_version = 19U;
    vm.context->application_class_name = "org.example.game.GameApplication";
    vm.context->application_label = std::string("OGPlay Game");
    vm.context->application_icon = 0x7f020001U;
    vm.context->application_meta_data.emplace(
        "com.example.configuration", std::string("live"));
    vm.context->application_meta_data.emplace("com.example.number",
                                               std::int32_t{42});
    vm.context->requested_permissions = {"android.permission.INTERNET"};
    vm.context->granted_permissions.insert("android.permission.INTERNET");
    vm.context->system_features.insert("android.hardware.touchscreen");

    const auto manager = vm.interpreter.NewIntrinsicInstance(
        "Landroid/content/pm/PackageManager;");
    const auto package = vm.interpreter.NewStringUtf8("org.example.game");
    const auto invoke = [&](const std::string_view name,
                            const std::string_view descriptor,
                            std::vector<VmValue> arguments) {
      arguments.insert(arguments.begin(), VmValue::Ref(manager));
      return vm.interpreter.Call(
          vm.Virtual("Landroid/content/pm/PackageManager;", name, descriptor),
          arguments);
    };
    const auto field = [&](const VmObjectRef object, const std::string& name,
                           const std::string& descriptor) {
      const auto found = vm.linker.FindFieldRecursive(
          vm.model.ObjectClass(object), name, descriptor);
      REQUIRE(found.has_value());
      return vm.model.InstanceSlots(object)[vm.linker.Field(*found).slot];
    };
    const auto ref_field = [&](const VmObjectRef object,
                               const std::string& name,
                               const std::string& descriptor) {
      const auto slot = field(object, name, descriptor);
      if (slot.bits == 0U && slot.tag == SlotTag::uninit) {
        return VmObjectRef{};
      }
      REQUIRE(slot.tag == SlotTag::ref);
      return VmObjectRef{static_cast<std::uint32_t>(slot.bits)};
    };
    const auto int_field = [&](const VmObjectRef object,
                               const std::string& name) {
      const auto slot = field(object, name, "I");
      REQUIRE(slot.tag == SlotTag::cat1);
      return static_cast<std::int32_t>(slot.bits);
    };

    const auto application = invoke(
        "getApplicationInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;",
        {VmValue::Ref(package), VmValue::Int(0x80)});
    REQUIRE_FALSE(application.exception.IsValid());
    REQUIRE(application.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(ref_field(
              application.value.ref, "packageName", "Ljava/lang/String;")) ==
          "org.example.game");
    CHECK(vm.interpreter.StringUtf8(ref_field(
              application.value.ref, "className", "Ljava/lang/String;")) ==
          "org.example.game.GameApplication");
    CHECK(int_field(application.value.ref, "uid") == 10000);
    CHECK(int_field(application.value.ref, "targetSdkVersion") == 19);
    CHECK(int_field(application.value.ref, "icon") ==
          static_cast<std::int32_t>(0x7f020001U));
    const auto metadata = ref_field(application.value.ref, "metaData",
                                    "Landroid/os/Bundle;");
    REQUIRE(metadata.IsValid());
    const auto metadata_key =
        vm.interpreter.NewStringUtf8("com.example.configuration");
    const auto metadata_value = vm.interpreter.Call(
        vm.Virtual("Landroid/os/Bundle;", "getString",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
        std::vector{VmValue::Ref(metadata), VmValue::Ref(metadata_key)});
    REQUIRE_FALSE(metadata_value.exception.IsValid());
    CHECK(vm.interpreter.StringUtf8(metadata_value.value.ref) == "live");

    const auto without_metadata = invoke(
        "getApplicationInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;",
        {VmValue::Ref(package), VmValue::Int(0)});
    REQUIRE_FALSE(without_metadata.exception.IsValid());
    CHECK_FALSE(ref_field(without_metadata.value.ref, "metaData",
                          "Landroid/os/Bundle;").IsValid());

    const auto package_info = invoke(
        "getPackageInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;",
        {VmValue::Ref(package), VmValue::Int(0x1080)});
    REQUIRE_FALSE(package_info.exception.IsValid());
    REQUIRE(package_info.value.ref.IsValid());
    CHECK(int_field(package_info.value.ref, "versionCode") == 7);
    CHECK(vm.interpreter.StringUtf8(ref_field(
              package_info.value.ref, "versionName", "Ljava/lang/String;")) ==
          "1.2.3");
    const auto permissions = ref_field(package_info.value.ref,
                                       "requestedPermissions",
                                       "[Ljava/lang/String;");
    REQUIRE(permissions.IsValid());
    REQUIRE(vm.model.ArrayLength(permissions) == 1);
    CHECK(vm.interpreter.StringUtf8(vm.model.GetObjectElement(permissions, 0)) ==
          "android.permission.INTERNET");

    const auto label = invoke(
        "getApplicationLabel",
        "(Landroid/content/pm/ApplicationInfo;)Ljava/lang/CharSequence;",
        {VmValue::Ref(application.value.ref)});
    REQUIRE_FALSE(label.exception.IsValid());
    CHECK(vm.interpreter.StringUtf8(label.value.ref) == "OGPlay Game");

    const auto internet =
        vm.interpreter.NewStringUtf8("android.permission.INTERNET");
    const auto camera =
        vm.interpreter.NewStringUtf8("android.permission.CAMERA");
    CHECK(invoke("checkPermission",
                 "(Ljava/lang/String;Ljava/lang/String;)I",
                 {VmValue::Ref(internet), VmValue::Ref(package)})
              .value.AsInt() == 0);
    CHECK(invoke("checkPermission",
                 "(Ljava/lang/String;Ljava/lang/String;)I",
                 {VmValue::Ref(camera), VmValue::Ref(package)})
              .value.AsInt() == -1);
    const auto touchscreen =
        vm.interpreter.NewStringUtf8("android.hardware.touchscreen");
    const auto gps =
        vm.interpreter.NewStringUtf8("android.hardware.location.gps");
    CHECK(invoke("hasSystemFeature", "(Ljava/lang/String;)Z",
                 {VmValue::Ref(touchscreen)})
              .value.AsInt() == 1);
    CHECK(invoke("hasSystemFeature", "(Ljava/lang/String;)Z",
                 {VmValue::Ref(gps)})
              .value.AsInt() == 0);

    const auto unknown = vm.interpreter.NewStringUtf8("org.example.missing");
    const auto missing = invoke(
        "getApplicationInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;",
        {VmValue::Ref(unknown), VmValue::Int(0)});
    REQUIRE(missing.exception.IsValid());
    CHECK(vm.linker.Class(vm.model.ObjectClass(missing.exception)).descriptor ==
          "Landroid/content/pm/PackageManager$NameNotFoundException;");
    const auto unsupported = invoke(
        "getApplicationInfo",
        "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;",
        {VmValue::Ref(package), VmValue::Int(0x40)});
    REQUIRE(unsupported.exception.IsValid());
    CHECK(vm.linker.Class(vm.model.ObjectClass(unsupported.exception)).descriptor ==
          "Ljava/lang/UnsupportedOperationException;");
  }
}

TEST_CASE("Window policy and Activity orientation preserve API19 state") {
  AndroidVm vm;
  const auto window =
      vm.interpreter.NewIntrinsicInstance("Landroid/view/Window;");
  const auto invoke_window = [&](const std::string_view name,
                                 const std::string_view descriptor,
                                 std::vector<VmValue> arguments = {}) {
    arguments.insert(arguments.begin(), VmValue::Ref(window));
    return vm.interpreter.Call(
        vm.Virtual("Landroid/view/Window;", name, descriptor), arguments);
  };
  const auto first = invoke_window("getAttributes",
      "()Landroid/view/WindowManager$LayoutParams;");
  const auto second = invoke_window("getAttributes",
      "()Landroid/view/WindowManager$LayoutParams;");
  REQUIRE_FALSE(first.exception.IsValid());
  REQUIRE(first.value.ref.IsValid());
  CHECK(first.value.ref == second.value.ref);

  const auto read_attribute = [&](const std::string& name) {
    const auto owner = vm.model.ObjectClass(first.value.ref);
    const auto field = vm.linker.FindFieldRecursive(owner, name, "I");
    REQUIRE(field.has_value());
    return static_cast<std::int32_t>(
        vm.model.InstanceSlots(first.value.ref)[vm.linker.Field(*field).slot]
            .bits);
  };
  REQUIRE_FALSE(invoke_window("setFlags", "(II)V",
      {VmValue::Int(5), VmValue::Int(7)}).exception.IsValid());
  CHECK(read_attribute("flags") == 5);
  REQUIRE_FALSE(invoke_window("addFlags", "(I)V", {VmValue::Int(8)})
                    .exception.IsValid());
  CHECK(read_attribute("flags") == 13);
  REQUIRE_FALSE(invoke_window("clearFlags", "(I)V", {VmValue::Int(4)})
                    .exception.IsValid());
  CHECK(read_attribute("flags") == 9);
  REQUIRE_FALSE(invoke_window("setSoftInputMode", "(I)V",
      {VmValue::Int(0x20)}).exception.IsValid());
  CHECK(read_attribute("softInputMode") == 0x20);
  REQUIRE_FALSE(invoke_window("setSoftInputMode", "(I)V",
      {VmValue::Int(0)}).exception.IsValid());
  CHECK(read_attribute("softInputMode") == 0x20);
  REQUIRE_FALSE(invoke_window("setType", "(I)V", {VmValue::Int(3)})
                    .exception.IsValid());
  CHECK(read_attribute("type") == 3);

  const auto first_activity =
      vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
  const auto second_activity =
      vm.interpreter.NewIntrinsicInstance("Landroid/app/Activity;");
  const auto get_orientation = vm.Virtual(
      "Landroid/app/Activity;", "getRequestedOrientation", "()I");
  const auto set_orientation = vm.Virtual(
      "Landroid/app/Activity;", "setRequestedOrientation", "(I)V");
  auto outcome = vm.interpreter.Call(
      get_orientation, std::vector{VmValue::Ref(first_activity)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK(outcome.value.AsInt() == -1);
  REQUIRE_FALSE(vm.interpreter.Call(
      set_orientation,
      std::vector{VmValue::Ref(first_activity), VmValue::Int(0)})
                    .exception.IsValid());
  CHECK(vm.interpreter.Call(
            get_orientation, std::vector{VmValue::Ref(first_activity)})
            .value.AsInt() == 0);
  CHECK(vm.interpreter.Call(
            get_orientation, std::vector{VmValue::Ref(second_activity)})
            .value.AsInt() == -1);
}

TEST_CASE("Display metrics publish the injected API19 surface facts") {
  AndroidVm vm;
  vm.context->surface_width = 800;
  vm.context->surface_height = 480;
  vm.context->ui_density = 1.5F;
  vm.context->ui_scaled_density = 1.75F;
  const auto display =
      vm.interpreter.NewIntrinsicInstance("Landroid/view/Display;");
  const auto metrics =
      vm.interpreter.NewIntrinsicInstance("Landroid/util/DisplayMetrics;");
  const auto invoke = [&](const std::string_view name,
                          const VmObjectRef output) {
    return vm.interpreter.Call(
        vm.Virtual("Landroid/view/Display;", name,
                   "(Landroid/util/DisplayMetrics;)V"),
        std::vector{VmValue::Ref(display), VmValue::Ref(output)});
  };
  REQUIRE_FALSE(invoke("getMetrics", metrics).exception.IsValid());

  const auto read = [&](const std::string& name,
                        const std::string& descriptor) {
    const auto field = vm.linker.FindFieldRecursive(
        vm.model.ObjectClass(metrics), name, descriptor);
    REQUIRE(field.has_value());
    return vm.model.InstanceSlots(metrics)[vm.linker.Field(*field).slot].bits;
  };
  CHECK(static_cast<std::int32_t>(read("widthPixels", "I")) == 800);
  CHECK(static_cast<std::int32_t>(read("heightPixels", "I")) == 480);
  CHECK(std::bit_cast<float>(read("density", "F")) == doctest::Approx(1.5F));
  CHECK(static_cast<std::int32_t>(read("densityDpi", "I")) == 240);
  CHECK(std::bit_cast<float>(read("scaledDensity", "F")) ==
        doctest::Approx(1.75F));
  CHECK(std::bit_cast<float>(read("xdpi", "F")) == doctest::Approx(240.0F));
  CHECK(static_cast<std::int32_t>(read("noncompatWidthPixels", "I")) == 800);
  REQUIRE_FALSE(invoke("getRealMetrics", metrics).exception.IsValid());
  CHECK(invoke("getMetrics", VmObjectRef{}).exception.IsValid());

  const auto get_id = vm.interpreter.Call(
      vm.Virtual("Landroid/view/Display;", "getDisplayId", "()I"),
      std::vector{VmValue::Ref(display)});
  REQUIRE_FALSE(get_id.exception.IsValid());
  CHECK(get_id.value.AsInt() == 0);
}

TEST_CASE("Configuration derives API19 screen layout from injected metrics") {
  AndroidVm vm;
  const auto resources =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/res/Resources;");
  const auto get_configuration = vm.Virtual(
      "Landroid/content/res/Resources;", "getConfiguration",
      "()Landroid/content/res/Configuration;");
  const auto read_field = [&](const VmObjectRef configuration,
                              const char* name) {
    const auto field = vm.linker.FindFieldRecursive(
        vm.model.ObjectClass(configuration), name, "I");
    REQUIRE(field.has_value());
    return static_cast<std::int32_t>(
        vm.model.InstanceSlots(configuration)[vm.linker.Field(*field).slot]
            .bits);
  };
  const auto get = [&] {
    const auto outcome = vm.interpreter.Call(
        get_configuration, std::vector{VmValue::Ref(resources)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    return outcome.value.ref;
  };

  vm.context->surface_width = 800;
  vm.context->surface_height = 480;
  vm.context->ui_density = 1.0F;
  const auto configuration = get();
  CHECK(read_field(configuration, "keyboard") == 1);
  CHECK(read_field(configuration, "screenLayout") == 0x10000023);

  vm.context->surface_width = 320;
  vm.context->surface_height = 480;
  CHECK(get() == configuration);
  CHECK(read_field(configuration, "screenLayout") == 0x12);

  vm.context->ui_density = 0.0F;
  CHECK_THROWS(static_cast<void>(vm.interpreter.Call(
      get_configuration, std::vector{VmValue::Ref(resources)})));
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

    CHECK_FALSE(ogplay::runtime::RetireSurfaceHolderGeneration(
                    interpreter, *context).has_value());
    CHECK(read("destroyed") == 1);
    CHECK(context->surface_callbacks.empty());
    CHECK(context->surface_holders.empty());

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
    VmThreadRuntime threads(interpreter);
    const auto thread_class = linker.FindClass("Ljava/lang/Thread;");
    REQUIRE(thread_class.has_value());
  const auto set_priority =
      linker.FindVtableIndex(*thread_class, "setPriority", "(I)V");
    REQUIRE(set_priority.has_value());
  const auto thread = interpreter.NewIntrinsicInstance("Ljava/lang/Thread;");
    const auto constructor =
        linker.FindDirectMethod(*thread_class, "<init>", "()V");
    REQUIRE(constructor.has_value());
    auto outcome = interpreter.Call(
        *constructor, std::vector<VmValue>{VmValue::Ref(thread)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    const auto target = linker.Class(*thread_class).vtable[*set_priority];
    outcome = interpreter.Call(
        target, std::vector<VmValue>{VmValue::Ref(thread), VmValue::Int(7)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    const auto get_priority =
        linker.FindVtableIndex(*thread_class, "getPriority", "()I");
    REQUIRE(get_priority.has_value());
    outcome = interpreter.Call(
        linker.Class(*thread_class).vtable[*get_priority],
        std::vector<VmValue>{VmValue::Ref(thread)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK(outcome.value.AsInt() == 7);

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
  CHECK(outcome.value.AsLong() > 1);
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
    outcome = interpreter.Call(
        linker.Class(*thread_class).vtable[*get_priority],
        std::vector<VmValue>{VmValue::Ref(thread)});
    REQUIRE_FALSE(outcome.exception.IsValid());
    CHECK(outcome.value.AsInt() == 7);
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

TEST_CASE("Context unregisterReceiver enforces per-context registration") {
  AndroidVm vm;
  const auto first_context =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
  const auto second_context =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/Context;");
  const auto receiver =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/BroadcastReceiver;");
  const auto filter =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/IntentFilter;");
  const auto register_receiver = vm.Virtual(
      "Landroid/content/Context;", "registerReceiver",
      "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;");
  const auto unregister_receiver = vm.Virtual(
      "Landroid/content/Context;", "unregisterReceiver",
      "(Landroid/content/BroadcastReceiver;)V");

  auto outcome = vm.interpreter.Call(
      register_receiver,
      std::vector<VmValue>{VmValue::Ref(first_context), VmValue::Ref(receiver),
                           VmValue::Ref(filter)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK_FALSE(outcome.value.ref.IsValid());
  CHECK(vm.context->broadcast_receivers.at(first_context.Value()).contains(
      receiver.Value()));

  outcome = vm.interpreter.Call(
      unregister_receiver,
      std::vector<VmValue>{VmValue::Ref(second_context), VmValue::Ref(receiver)});
  REQUIRE(outcome.exception.IsValid());
  CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
        "Ljava/lang/IllegalArgumentException;");
  CHECK(vm.context->broadcast_receivers.at(first_context.Value()).contains(
      receiver.Value()));

  outcome = vm.interpreter.Call(
      unregister_receiver,
      std::vector<VmValue>{VmValue::Ref(first_context), VmValue::Ref(receiver)});
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK_FALSE(vm.context->broadcast_receivers.contains(first_context.Value()));
  outcome = vm.interpreter.Call(
      unregister_receiver,
      std::vector<VmValue>{VmValue::Ref(first_context), VmValue::Ref(receiver)});
  REQUIRE(outcome.exception.IsValid());
  CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
        "Ljava/lang/IllegalArgumentException;");
}

TEST_CASE("IntentFilter retains distinct case-sensitive data schemes") {
  AndroidVm vm;
  const auto first =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/IntentFilter;");
  const auto second =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/IntentFilter;");
  const auto add_scheme = vm.Virtual(
      "Landroid/content/IntentFilter;", "addDataScheme",
      "(Ljava/lang/String;)V");
  const auto add = [&](const VmObjectRef filter, const VmObjectRef scheme) {
    return vm.interpreter.Call(
        add_scheme,
        std::vector<VmValue>{VmValue::Ref(filter), VmValue::Ref(scheme)});
  };

  const auto http = vm.interpreter.NewStringUtf8("http");
  const auto upper_http = vm.interpreter.NewStringUtf8("HTTP");
  REQUIRE_FALSE(add(first, http).exception.IsValid());
  REQUIRE_FALSE(add(first, http).exception.IsValid());
  REQUIRE_FALSE(add(first, upper_http).exception.IsValid());
  REQUIRE_FALSE(add(second, upper_http).exception.IsValid());
  CHECK(vm.context->intent_filter_schemes.at(first.Value()) ==
        std::vector<std::string>{"http", "HTTP"});
  CHECK(vm.context->intent_filter_schemes.at(second.Value()) ==
        std::vector<std::string>{"HTTP"});

  const auto null_outcome = add(first, VmObjectRef{});
  REQUIRE(null_outcome.exception.IsValid());
  CHECK(vm.linker.Class(null_outcome.exception_class).descriptor ==
        "Ljava/lang/NullPointerException;");
}

TEST_CASE("IntentFilter retains API19 data authority metadata") {
  AndroidVm vm;
  const auto first =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/IntentFilter;");
  const auto second =
      vm.interpreter.NewIntrinsicInstance("Landroid/content/IntentFilter;");
  const auto add_authority = vm.Virtual(
      "Landroid/content/IntentFilter;", "addDataAuthority",
      "(Ljava/lang/String;Ljava/lang/String;)V");
  const auto add = [&](const VmObjectRef filter, const VmObjectRef host,
                       const VmObjectRef port) {
    return vm.interpreter.Call(
        add_authority,
        std::vector<VmValue>{VmValue::Ref(filter), VmValue::Ref(host),
                             VmValue::Ref(port)});
  };
  const auto wildcard = vm.interpreter.NewStringUtf8("*.example.com");
  const auto plain = vm.interpreter.NewStringUtf8("example.org");
  const auto port = vm.interpreter.NewStringUtf8("+443");
  REQUIRE_FALSE(add(first, wildcard, port).exception.IsValid());
  REQUIRE_FALSE(add(first, wildcard, port).exception.IsValid());
  REQUIRE_FALSE(add(second, plain, VmObjectRef{}).exception.IsValid());

  const auto& first_entries =
      vm.context->intent_filter_authorities.at(first.Value());
  REQUIRE(first_entries.size() == 2);
  CHECK(first_entries[0].original_host == "*.example.com");
  CHECK(first_entries[0].match_host == ".example.com");
  CHECK(first_entries[0].wildcard);
  CHECK(first_entries[0].port == 443);
  CHECK(first_entries[1].original_host == first_entries[0].original_host);
  const auto& second_entry =
      vm.context->intent_filter_authorities.at(second.Value()).front();
  CHECK_FALSE(second_entry.wildcard);
  CHECK(second_entry.match_host == "example.org");
  CHECK(second_entry.port == -1);

  auto outcome = add(first, VmObjectRef{}, VmObjectRef{});
  REQUIRE(outcome.exception.IsValid());
  CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
        "Ljava/lang/NullPointerException;");
  for (const char* invalid : {"443x", "2147483648", "-2147483649"}) {
    outcome = add(first, plain, vm.interpreter.NewStringUtf8(invalid));
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
          "Ljava/lang/NumberFormatException;");
  }
}

TEST_CASE("Intent removeExtra clears every typed backing entry") {
  AndroidVm vm;
  const auto intent = vm.interpreter.NewIntrinsicInstance("Landroid/content/Intent;");
  const auto key = vm.interpreter.NewStringUtf8("shared-key");
  vm.context->intent_string_extras[intent.Value()]["shared-key"] = "value";
  vm.context->intent_int_extras[intent.Value()]["shared-key"] = 42;
  const auto remove = vm.Virtual("Landroid/content/Intent;", "removeExtra", "(Ljava/lang/String;)V");
  const std::vector arguments{VmValue::Ref(intent), VmValue::Ref(key)};
  auto outcome = vm.interpreter.Call(remove, arguments);
  REQUIRE_FALSE(outcome.exception.IsValid());
  CHECK_FALSE(vm.context->intent_string_extras.contains(intent.Value()));
  CHECK_FALSE(vm.context->intent_int_extras.contains(intent.Value()));
  outcome = vm.interpreter.Call(remove, arguments);
  CHECK_FALSE(outcome.exception.IsValid());
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
