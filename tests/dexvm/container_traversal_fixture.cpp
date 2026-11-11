#include "container_traversal_fixture.h"
#include "ogplay/runtime/bionic/bionic_module_set.h"
#include "../runtime/resources_test_support.h"

namespace ogplay::test::container_traversal {
std::unique_ptr<AndroidTestRuntime> ContainerVm::Create(InterpreterBackend backend, bool native_activity,
                                                        bool sealed_apk) {
    using namespace ogplay::test::native_library;
    ogplay::test::BindBootDexArchive(*context);
    auto manifest = AppManifest(native_activity ? "android.app.NativeActivity" : "fixture.MainActivity");
    std::vector<ogplay::loader::ApkNativeLibrary> libraries;
    if (native_activity) {
        manifest.activity_components.front().meta_data.push_back({"android.app.lib_name", std::string("a")});
        libraries.push_back(Library("liba.so", NativeActivityElf()));
    }
    context->package_name = manifest.package;
    context->package_resource_path = "/data/app/fixture-1.apk";
    context->application_class_name = manifest.application_class;
    context->application_enabled = true;
    context->package_inventory_known = true;
    context->activity_inventory_known = true;
    context->activity_components = std::move(manifest.activity_components);
    context->surface_width = 800;
    context->surface_height = 480;
    auto dex = ReadDexFixture("view_measure.dex");
    if (sealed_apk) {
        const auto bytes = std::as_bytes(std::span{dex});
        const std::array entries{std::pair{std::string("classes.dex"), std::vector(bytes.begin(), bytes.end())}};
        context->sealed_apk_bytes = std::make_shared<const std::vector<std::byte>>(resources::Zip(entries));
        for (const auto* dir : {"/data", "/data/data", "/data/data/fixture"})
            filesystem.CreateDirectory(dir);
        filesystem.PutFile(context->package_resource_path, *context->sealed_apk_bytes, false);
    }
    return std::make_unique<AndroidTestRuntime>(
        context, ledger, logger, AndroidIntrinsicCatalog(context), std::move(dex),
        InterpreterConfig{
            .backend = backend,
            .diagnostics = {.trace_capacity = 10000, .event_mask = DexVmTraceBit(DexVmTraceKind::method_enter)}},
        &filesystem, std::move(libraries));
}

ContainerVm::ContainerVm(InterpreterBackend backend) : ContainerVm(backend, false) {}

ContainerVm::ContainerVm(InterpreterBackend backend, bool native_activity, bool sealed_apk)
    : runtime(Create(backend, native_activity, sealed_apk)), vm(runtime->bridge->Vm()), linker(vm.Linker()),
      model(vm.Model()) {
    context->pcm_playback = &runtime->session->PcmPlayback();
    context->encoded_audio_playback = &runtime->session->SoundPoolMixer();
    context->encoded_music = &runtime->session->EncodedMusic();
}

VmValue ContainerVm::Checked(const VmCallOutcome& result) {
    REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
    return result.value;
}

VmCallOutcome ContainerVm::OnOutcome(VmObjectRef object, const char* name, const char* descriptor,
                                     std::vector<VmValue> args) {
    ogplay::test::MountResourceFixture(*context, vm);
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
    ogplay::test::MountResourceFixture(*context, vm);
    const auto method = linker.FindDirectMethod(linker.ResolveDescriptor(owner), name, descriptor);
    REQUIRE(method);
    return Checked(vm.Call(*method, args));
}

VmObjectRef ContainerVm::New(const char* owner, const char* descriptor, std::vector<VmValue> args) {
    if (std::string_view(owner) == "Landroid/content/Context;") return ogplay::test::MakeRomAppContext(vm, context);
    Checked(vm.EnsureClassInitialized(linker.ResolveDescriptor(owner)));
    const auto result = vm.NewIntrinsicInstance(owner);
    const auto roots = vm.ProtectReferences(std::array{result});
    if (std::string_view(descriptor).starts_with("(Landroid/content/Context;") && !args.empty())
        ogplay::test::AttachViewTestContext(vm, args.front().ref, context);
    args.insert(args.begin(), VmValue::Ref(result));
    Static(owner, "<init>", descriptor, std::move(args));
    return result;
}
}  // namespace ogplay::test::container_traversal

namespace ogplay::test::container_traversal {
AndroidTestRuntime::AndroidTestRuntime(const std::shared_ptr<DexVmAndroidContext>& context,
                                       ogplay::core::CapabilityLedger& ledger, ogplay::core::Logger& logger,
                                       std::vector<IntrinsicClassDecl> catalog, std::vector<std::uint8_t> dex,
                                       InterpreterConfig config, VirtualFileSystem* filesystem,
                                       std::vector<ogplay::loader::ApkNativeLibrary> apk_libraries,
                                       ogplay::gles::AngleBackend graphics)
    : inventory(std::move(apk_libraries)), selected(inventory, ogplay::loader::AndroidArmAbi::armeabi_v7a) {
    using namespace ogplay::test::native_library;
    auto& fs = filesystem ? *filesystem : owned_filesystem;
    for (const auto& library : selected.Libraries())
        if (library.abi == selected.Abi())
            fs.PutFile(NativeLibraryLoader::SyntheticGuestPath(library.soname), library.image, false);
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
                                               .backend = graphics,
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
    auto boot_dex = ogplay::test::ReadBootDex();
    bridge = std::make_unique<DexVmGuestBridge>(*session, std::move(dex), catalog, context, ledger, &logger,
                                                DexVmBridgeConfig{.interpreter = config}, std::move(boot_dex));
    context->threads = &bridge->Threads();
    static_cast<void>(libraries->LoadLibrary("ogplay_jni", context->application_class_loader_token));
}

AndroidTestRuntime::~AndroidTestRuntime() = default;
}  // namespace ogplay::test::container_traversal
