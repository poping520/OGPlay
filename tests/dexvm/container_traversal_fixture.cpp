#include "container_traversal_fixture.h"
#include "ogplay/runtime/bionic/bionic_module_set.h"

namespace ogplay::test::container_traversal {
std::unique_ptr<ogplay::session::AndroidAppProcess> ContainerVm::Create(InterpreterBackend backend) {
    return Create(backend, false);
}

std::unique_ptr<ogplay::session::AndroidAppProcess> ContainerVm::Create(InterpreterBackend backend,
                                                                        bool include_skeleton, bool native_activity) {
    using namespace ogplay::test::native_library;
    std::vector<std::vector<std::byte>> bytes;
    std::vector<BionicModuleSource> sources;
    for (const auto name : SelectBionicProfile(19).guest_libraries) {
        bytes.push_back(ReadPayloadBytes("lib/" + std::string(name)));
        sources.push_back({std::string(name), bytes.back()});
    }
    std::ifstream stream(OGPLAY_DEXVM_FIXTURE_DIR "/view-measure.jar", std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(stream), {}};
    context->boot_classpath_bytes.resize(raw.size());
    std::memcpy(context->boot_classpath_bytes.data(), raw.data(), raw.size());
    context->boot_classpath_archive = ogplay::loader::ParseApkArchive(context->boot_classpath_bytes);
    const auto dex = ogplay::loader::ReadApkEntry(
        context->boot_classpath_bytes, ogplay::loader::ParseApkArchive(context->boot_classpath_bytes), "classes.dex");
    ogplay::session::AndroidAppProcessRequest request;
    request.manifest = AppManifest(native_activity ? "android.app.NativeActivity" : "fixture.MainActivity");
    const auto native = native_activity ? NativeActivityElf() : std::vector<std::byte>{};
    if (native_activity) {
        request.manifest.activity_components.front().meta_data.push_back({"android.app.lib_name", std::string("a")});
        request.native_libraries.push_back(Library("liba.so", native));
    }
    request.system_libraries = sources;
    request.dex_bytes = ReadDexFixture("view_measure.dex");
    request.icu_data = ReadPayloadBytes("icu/icudt51l.dat");
    request.tzdata = ReadPayloadBytes("zoneinfo/tzdata");
    request.boot_dex_bytes.resize(dex.size());
    std::memcpy(request.boot_dex_bytes.data(), dex.data(), dex.size());
    if (include_skeleton) {
        std::ifstream skeleton(std::filesystem::path{OGPLAY_SOURCE_DIR} / "data/dexvm/api19-skeleton.bin",
                               std::ios::binary);
        REQUIRE(skeleton.good());
        request.skeleton_bytes = {std::istreambuf_iterator<char>(skeleton), {}};
    }
    request.context = context;
    request.dexvm.interpreter.backend = backend;
    request.dexvm.interpreter.diagnostics.trace_capacity = 10000;
    request.dexvm.interpreter.diagnostics.event_mask = DexVmTraceBit(DexVmTraceKind::method_enter);
    request.maximum_ticks_per_call = UINT64_C(100000000);
    request.filesystem = &filesystem;
    request.ledger = &ledger;
    request.logger = &logger;
    return ogplay::session::AndroidAppProcess::Create(std::move(request));
}

ContainerVm::ContainerVm(InterpreterBackend backend) : ContainerVm(backend, false) {}

ContainerVm::ContainerVm(InterpreterBackend backend, bool include_skeleton)
    : ContainerVm(backend, include_skeleton, false) {}

ContainerVm::ContainerVm(InterpreterBackend backend, bool include_skeleton, bool native_activity)
    : app(Create(backend, include_skeleton, native_activity)), vm(app->DexVm().Vm()), linker(vm.Linker()),
      model(vm.Model()) {}

VmValue ContainerVm::Checked(const VmCallOutcome& result) {
    REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
    return result.value;
}

VmCallOutcome ContainerVm::OnOutcome(VmObjectRef object, const char* name, const char* descriptor,
                                     std::vector<VmValue> args) {
    const auto type = model.ObjectClass(object);
    const auto index = linker.FindVtableIndex(type, name, descriptor);
    REQUIRE(index);
    args.insert(args.begin(), VmValue::Ref(object));
    return vm.Call(linker.Class(type).vtable[*index], args);
}

VmValue ContainerVm::On(VmObjectRef object, const char* name, const char* descriptor, std::vector<VmValue> args) {
    return Checked(OnOutcome(object, name, descriptor, std::move(args)));
}

VmValue ContainerVm::Static(const char* owner, const char* name, const char* descriptor, std::vector<VmValue> args) {
    const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, descriptor);
    REQUIRE(method);
    return Checked(vm.Call(*method, args));
}

VmObjectRef ContainerVm::New(const char* owner, const char* descriptor, std::vector<VmValue> args) {
    Checked(vm.EnsureClassInitialized(linker.ResolveDescriptor(owner)));
    const auto result = vm.NewIntrinsicInstance(owner);
    const auto roots = vm.ProtectReferences(std::array{result});
    if (std::string_view(descriptor).starts_with("(Landroid/content/Context;") && !args.empty())
        ogplay::test::AttachViewTestContext(vm, args.front().ref);
    args.insert(args.begin(), VmValue::Ref(result));
    Static(owner, "<init>", descriptor, std::move(args));
    return result;
}
}  // namespace ogplay::test::container_traversal

namespace ogplay::test::container_traversal {
AndroidTestRuntime::AndroidTestRuntime(const std::shared_ptr<DexVmAndroidContext>& context,
                                       ogplay::core::CapabilityLedger& ledger, ogplay::core::Logger& logger,
                                       std::vector<IntrinsicClassDecl> catalog, std::vector<std::uint8_t> dex,
                                       InterpreterConfig config, bool skeleton, VirtualFileSystem* filesystem)
    : inventory(std::vector<ogplay::loader::ApkNativeLibrary>{}),
      selected(inventory, ogplay::loader::AndroidArmAbi::armeabi_v7a) {
    using namespace ogplay::test::native_library;
    auto& fs = filesystem ? *filesystem : owned_filesystem;
    fs.PutFile("/system/usr/icu/icudt51l.dat", ReadPayloadBytes("icu/icudt51l.dat"), false);
    fs.PutFile("/system/usr/share/zoneinfo/tzdata", ReadPayloadBytes("zoneinfo/tzdata"), false);
    std::vector<std::vector<std::byte>> bytes;
    std::vector<BionicModuleSource> sources;
    for (const auto name : SelectBionicProfile(19).guest_libraries) {
        bytes.push_back(ReadPayloadBytes("lib/" + std::string(name)));
        sources.push_back({std::string(name), bytes.back()});
    }
    const auto libc =
        std::find_if(sources.begin(), sources.end(), [](const auto& source) { return source.name == "libc.so"; });
    REQUIRE(libc != sources.end());
    std::vector<BionicModuleSource> dependencies;
    for (const auto& source : sources)
        if (source.name != "libc.so") dependencies.push_back(source);
    auto modules = BuildBionicModuleSet(SelectBionicProfile(19), "libc.so", libc->image, dependencies);
    const auto inputs = modules.Inputs();
    auto process = AndroidGuestProcess::Start({.system_modules = inputs,
                                               .width = context->surface_width ? context->surface_width : 800,
                                               .height = context->surface_height ? context->surface_height : 480,
                                               .maximum_ticks_per_call = UINT64_C(100000000),
                                               .filesystem = &fs});
    session = AndroidGuestCallSession::AdoptProcess(std::move(process));
    libraries =
        std::make_unique<NativeLibraryLoader>(session->Process(), selected, SelectBionicProfile(19), sources, &logger);
    context->session = session.get();
    context->native_libraries = libraries.get();
    context->vfs = &fs;
    context->ui_test_accessibility_disabled = true;
    const auto boot =
        ogplay::loader::ReadApkEntry(context->boot_classpath_bytes, context->boot_classpath_archive, "classes.dex");
    std::vector<std::uint8_t> boot_dex(boot.size());
    std::memcpy(boot_dex.data(), boot.data(), boot.size());
    std::vector<std::uint8_t> skeleton_bytes;
    if (skeleton) {
        std::ifstream stream(std::filesystem::path{OGPLAY_SOURCE_DIR} / "data/dexvm/api19-skeleton.bin",
                             std::ios::binary);
        REQUIRE(stream.good());
        skeleton_bytes = {std::istreambuf_iterator<char>(stream), {}};
    }
    bridge = std::make_unique<DexVmGuestBridge>(*session, std::move(dex), catalog, context, ledger, &logger,
                                                DexVmBridgeConfig{.interpreter = config}, std::move(boot_dex),
                                                std::move(skeleton_bytes));
    context->threads = &bridge->Threads();
    static_cast<void>(libraries->LoadLibrary("ogplay_jni", context->application_class_loader_token));
}

AndroidTestRuntime::~AndroidTestRuntime() = default;
}  // namespace ogplay::test::container_traversal
