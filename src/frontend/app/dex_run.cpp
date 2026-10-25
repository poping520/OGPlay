#include "ogplay/app/dex_run.h"
#include "ogplay/app/host.h"
#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/json.h"
#include "ogplay/core/logger.h"
#include "ogplay/hal/clock.h"
#include "ogplay/loader/apk.h"
#include "ogplay/loader/elf.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/dexvm_bridge.h"
#include "ogplay/runtime/integration/native_library_loader.h"
#include "ogplay/runtime/vfs/vfs.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <thread>

namespace ogplay::app {
namespace {
namespace dx = runtime::dexvm;

void WriteStderr(const std::string_view text) {
    static_cast<void>(std::fwrite(text.data(), sizeof(char), text.size(), stderr));
    static_cast<void>(std::fflush(stderr));
}

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

// A file is either a bare dex or a zip whose single dex is classes.dex.
std::vector<std::uint8_t> ReadDex(const std::filesystem::path& path) {
    auto raw = ReadFile(path);
    if (raw.size() >= 4 && std::memcmp(raw.data(), "dex\n", 4) == 0) return raw;
    std::vector<std::byte> archive(raw.size());
    std::memcpy(archive.data(), raw.data(), raw.size());
    const auto parsed = loader::ParseApkArchive(archive);
    for (const auto& entry : parsed.entries) {
        if (entry.name == "classes2.dex")
            throw std::runtime_error("multidex input is not supported: " + path.string());
    }
    const auto dex = loader::ReadApkEntry(archive, parsed, "classes.dex");
    std::vector<std::uint8_t> bytes(dex.size());
    std::memcpy(bytes.data(), dex.data(), dex.size());
    return bytes;
}

std::string ClassDescriptor(std::string name) {
    for (auto& c : name) if (c == '.') c = '/';
    return "L" + name + ";";
}

dx::VmCallOutcome CallVirtual(dx::Interpreter& vm, const dx::VmObjectRef receiver,
                              const std::string& name, const std::string& descriptor,
                              std::vector<dx::VmValue> arguments = {}) {
    auto& linker = vm.Linker();
    const auto owner = vm.Model().ObjectClass(receiver);
    const auto index = linker.FindVtableIndex(owner, name, descriptor);
    if (!index) throw std::runtime_error("run-dex: missing virtual " + name + descriptor);
    arguments.insert(arguments.begin(), dx::VmValue::Ref(receiver));
    return vm.Call(linker.Class(owner).vtable[*index], arguments);
}

// dvmDetachCurrentThread with a pending exception: the thread's handler, or
// its ThreadGroup, receives uncaughtException(thread, throwable).
void DispatchUncaught(dx::Interpreter& vm, const dx::VmObjectRef throwable) {
    auto& linker = vm.Linker();
    const auto thread_class = linker.ResolveDescriptor("Ljava/lang/Thread;");
    const auto current = linker.FindDirectMethod(thread_class, "currentThread", "()Ljava/lang/Thread;");
    if (!current) throw std::runtime_error("run-dex: Thread.currentThread is unavailable");
    const auto thread = vm.Call(*current, std::span<const dx::VmValue>{});
    if (thread.exception.IsValid()) throw std::runtime_error("run-dex: Thread.currentThread threw");
    const auto roots = vm.ProtectReferences(std::array{thread.value.ref});
    auto handler = CallVirtual(vm, thread.value.ref, "getUncaughtExceptionHandler",
                               "()Ljava/lang/Thread$UncaughtExceptionHandler;");
    if (handler.exception.IsValid()) throw std::runtime_error("run-dex: getUncaughtExceptionHandler threw");
    if (!handler.value.ref.IsValid()) {
        handler = CallVirtual(vm, thread.value.ref, "getThreadGroup", "()Ljava/lang/ThreadGroup;");
        if (handler.exception.IsValid() || !handler.value.ref.IsValid())
            throw std::runtime_error("run-dex: main thread has no ThreadGroup");
    }
    // AOSP ignores failures raised by the handler itself; keep them visible
    // on stderr so an unimplemented handler is not mistaken for silence.
    const auto dispatched = CallVirtual(vm, handler.value.ref, "uncaughtException",
        "(Ljava/lang/Thread;Ljava/lang/Throwable;)V",
        {dx::VmValue::Ref(thread.value.ref), dx::VmValue::Ref(throwable)});
    if (dispatched.exception.IsValid()) {
        WriteStderr("ogplay-run-dex: uncaught handler failed: " +
                    linker.Class(dispatched.exception_class).descriptor + ": " +
                    dispatched.exception_message + "\n");
    }
}

// DestroyJavaVM: wait until every non-daemon thread other than main finishes.
void WaitForNonDaemonThreads(dx::Interpreter& vm, dx::VmThreadRuntime& threads) {
    for (;;) {
        if (vm.ExitCode()) return;
        bool waiting = false;
        for (const auto& thread : threads.Snapshot()) {
            if (thread.id == 1U || (thread.status != dx::VmThreadStatus::running &&
                                     thread.status != dx::VmThreadStatus::created)) continue;
            const auto daemon = CallVirtual(vm, dx::VmObjectRef{thread.object}, "isDaemon", "()Z");
            if (!daemon.exception.IsValid() && daemon.value.AsInt() == 0) waiting = true;
        }
        if (!waiting) return;
        static_cast<void>(threads.WaitForHostProgress(std::chrono::milliseconds(50)));
    }
}

void WriteLedger(const std::filesystem::path& path, const core::CapabilityLedger& ledger,
                 const std::vector<core::CapabilityWarningHit>& guest_warnings) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddInteger(root, "schema", 1);
    const auto hits = writer.Array();
    for (const auto& hit : ledger.Unimplemented()) {
        const auto item = writer.Object();
        writer.AddString(item, "id", hit.id);
        writer.AddInteger(item, "count", static_cast<std::int64_t>(hit.count));
        writer.Append(hits, item);
    }
    writer.Add(root, "unimplemented", hits);
    const auto warnings = writer.Array();
    for (const auto& hit : guest_warnings) {
        const auto item = writer.Object();
        writer.AddString(item, "id", hit.id);
        writer.AddString(item, "level", "warn");
        writer.AddInteger(item, "count", static_cast<std::int64_t>(hit.count));
        writer.AddInteger(item, "first_lr", static_cast<std::int64_t>(hit.first_lr));
        writer.AddInteger(item, "last_lr", static_cast<std::int64_t>(hit.last_lr));
        writer.Append(warnings, item);
    }
    writer.Add(root, "warnings", warnings);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write " + path.string());
    output << writer.Serialize(root) << '\n';
}
}  // namespace

DexRunOptions ParseDexRunArgs(const std::span<const std::string_view> args) {
    DexRunOptions options;
    std::size_t index = 0;
    for (; index < args.size(); ++index) {
        const auto arg = args[index];
        const auto value = [&]() -> std::string {
            if (index + 1 >= args.size())
                throw std::invalid_argument("run-dex option needs a value: " + std::string(arg));
            return std::string(args[++index]);
        };
        if (arg == "--") { ++index; break; }
        if (arg == "--class") options.main_class = value();
        else if (arg == "--backend") {
            options.backend = value();
            if (options.backend != "switch" && options.backend != "threaded")
                throw std::invalid_argument("--backend must be switch or threaded");
        } else if (arg == "--ledger-json") options.ledger_json = std::filesystem::path(value());
        else if (arg == "--native-lib-dir") {
            options.native_lib_dir = value();
            if (options.native_lib_dir.empty()) throw std::invalid_argument("--native-lib-dir must not be empty");
        } else if (arg == "--enable-assertions" || arg == "-ea") options.enable_assertions = true;
        else if (arg == "--merge-console")
            options.merge_console = true;
        else if (arg.starts_with("--")) throw std::invalid_argument("unknown run-dex option: " + std::string(arg));
        else if (options.input.empty()) options.input = std::filesystem::path(std::string(arg));
        else { break; }
    }
    for (; index < args.size(); ++index) options.arguments.emplace_back(args[index]);
    if (options.input.empty()) throw std::invalid_argument("run-dex requires <dex-or-jar>");
    if (options.main_class.empty()) throw std::invalid_argument("--class must not be empty");
    return options;
}

int RunDex(const DexRunOptions& options, const DexConsole& console) {
    auto app_dex = ReadDex(options.input);
    const auto data = BundledDataPaths().root;
    const auto boot_raw = ReadFile(data / "android/19/framework/bootdex.jar");
    std::vector<std::byte> boot_archive(boot_raw.size());
    std::memcpy(boot_archive.data(), boot_raw.data(), boot_raw.size());
    const auto context = runtime::AssembleAndroidContext(std::move(boot_archive), {true});
    const auto boot_entry =
        loader::ReadApkEntry(context->boot_classpath_bytes, context->boot_classpath_archive, "classes.dex");
    std::vector<std::uint8_t> boot_dex(boot_entry.size());
    std::memcpy(boot_dex.data(), boot_entry.data(), boot_entry.size());
    auto skeleton = ReadFile(data / "dexvm/api19-skeleton.bin");

    core::CapabilityLedger ledger;
    auto catalog = runtime::AndroidIntrinsicCatalog(context);
    // The real dalvikvm entry is NativeStart.main, which calls the selected
    // application's main. Retain that actual call boundary in captured stacks.
    auto entry = dx::IntrinsicClassBuilder::Class("Ldalvik/system/NativeStart;");
    entry.StaticMethod("main", "([Ljava/lang/String;)V",
        [&options](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            const auto owner = vm.Linker().ResolveDescriptor(ClassDescriptor(options.main_class));
            const auto target = vm.Linker().FindDirectMethod(owner, "main", "([Ljava/lang/String;)V");
            if (!target) throw std::runtime_error("static main(String[]) not found");
            const auto result = vm.Call(*target, call.arguments);
            if (result.exception.IsValid()) vm.SetPendingException(result.exception);
            return dx::VmValue::Void();
        }, dx::kAccPublic | dx::kAccStatic | dx::kAccNative);
    catalog.push_back(std::move(entry).Build());

    core::Logger logger;
    logger.AddSink(std::make_shared<core::ConsoleSink>(stderr), core::LogLevel::warn);
    runtime::VirtualFileSystem vfs;
    const auto& bionic = runtime::SelectBionicProfile(19);
    std::vector<runtime::BionicOwnedModule> owned_system;
    for (const auto name : bionic.guest_libraries) {
        const auto bytes = ReadFile(data / bionic.data_directory / "lib" / name);
        std::vector<std::byte> image(bytes.size());
        std::memcpy(image.data(), bytes.data(), bytes.size());
        owned_system.push_back({std::string(name), std::move(image), memory::GuestAddress{0}});
    }
    std::vector<runtime::BionicModuleSource> system_sources;
    std::vector<runtime::BionicModuleSource> dependencies;
    std::span<const std::byte> libc;
    for (const auto& source : owned_system) {
        system_sources.push_back({source.name, source.image});
        if (source.name == "libc.so") libc = source.image;
        else dependencies.push_back({source.name, source.image});
    }
    auto system = runtime::BuildBionicModuleSet(bionic, "libc.so", libc, dependencies);
    const auto inputs = system.Inputs();
    const auto put_payload = [&](const char* guest_path, const char* relative_path) {
        const auto bytes = ReadFile(data / bionic.data_directory / relative_path);
        vfs.PutFile(guest_path, std::as_bytes(std::span(bytes)), false);
    };
    put_payload("/system/usr/icu/icudt51l.dat", "icu/icudt51l.dat");
    put_payload("/system/usr/share/zoneinfo/tzdata", "zoneinfo/tzdata");
    runtime::AndroidGuestProcessRequest request;
    request.system_modules = inputs;
    request.width = 800;
    request.height = 480;
    request.filesystem = &vfs;
    request.boundary_options.logger = &logger;
    request.initial_environment = std::make_shared<const runtime::GuestProcessEnvironment>(
        runtime::GuestProcessEnvironment::Api19(context->external_storage_root));
    auto session = runtime::AndroidGuestCallSession::AdoptProcess(
        runtime::AndroidGuestProcess::Start(request));
    std::vector<loader::ApkNativeLibrary> application_libraries;
    if (!options.native_lib_dir.empty()) {
        if (!std::filesystem::is_directory(options.native_lib_dir))
            throw std::invalid_argument("--native-lib-dir is not a directory: " + options.native_lib_dir.string());
        std::set<std::string> sonames;
        for (const auto& file : std::filesystem::directory_iterator(options.native_lib_dir)) {
            const auto name = file.path().filename().string();
            if (file.path().extension() != ".so") continue;
            if (!file.is_regular_file() || !std::regex_match(name, std::regex("lib[A-Za-z0-9_+.-]+\\.so")))
                throw std::invalid_argument("invalid native library filename: " + name);
            const auto bytes = ReadFile(file.path());
            std::vector<std::byte> image(bytes.size());
            std::memcpy(image.data(), bytes.data(), bytes.size());
            const auto elf = loader::ParseElf32Arm(image);
            const auto dynamic = loader::ReadElf32DynamicInfo(image, elf);
            const auto soname = dynamic.soname.value_or(name);
            if (!sonames.insert(soname).second)
                throw std::invalid_argument("duplicate native library soname: " + soname);
            loader::ApkNativeLibrary library;
            library.entry_name = "lib/armeabi-v7a/" + name;
            library.basename = name;
            library.image = std::move(image);
            application_libraries.push_back(std::move(library));
        }
    }
    loader::ApkNativeLibraryInventory inventory(std::move(application_libraries));
    loader::ApkSelectedNativeLibraries selected(inventory, loader::AndroidArmAbi::armeabi_v7a);
    runtime::NativeLibraryLoader native_libraries(session->Process(), selected,
                                                 bionic, system_sources, &logger);
    context->session = session.get();
    context->vfs = &vfs;
    context->native_libraries = &native_libraries;
    runtime::DexVmBridgeConfig config;
    config.interpreter.enable_assertions = options.enable_assertions;
    config.interpreter.backend = options.backend == "threaded" ? dx::InterpreterBackend::threaded
                                                               : dx::InterpreterBackend::switch_dispatch;
    runtime::DexVmGuestBridge bridge(*session, std::move(app_dex), catalog, context,
                                    ledger, &logger, config, std::move(boot_dex),
                                    std::move(skeleton));
    static_cast<void>(native_libraries.LoadLibrary("ogplay_jni", context->application_class_loader_token));
    auto& vm = bridge.Vm();
    auto& linker = bridge.Linker();
    auto& model = bridge.Model();
    auto& threads = bridge.Threads();
    vm.SetConsoleSink(console ? console : DexConsole([](const std::string_view bytes) {
                          static_cast<void>(std::fwrite(bytes.data(), sizeof(char), bytes.size(), stdout));
                          static_cast<void>(std::fflush(stdout));
                      }),
                      console || options.merge_console ? DexConsole{} : DexConsole([](const std::string_view bytes) {
                          WriteStderr(bytes);
                      }));
    // A standalone run has no frame pump: a realtime unified Clock drives the
    // single Android monotonic clock that Thread.sleep/wait and System time read.
    vm.Monitors().SetClockAdvance({});
    vm.Monitors().SetClockDriverBlockedProbe({});
    std::jthread clock_driver([context](const std::stop_token stop) {
        hal::RealtimeClock clock(1000);
        auto last = clock.Ticks();
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const auto now = clock.Ticks();
            runtime::AdvanceAndroidClock(*context, static_cast<std::int64_t>(now - last));
            last = now;
        }
    });

    int status = 0;
    try {
        const auto main_class = linker.FindClass(ClassDescriptor(options.main_class));
        if (!main_class) throw std::runtime_error("main class not found: " + options.main_class);
        const auto main = linker.FindDirectMethod(*main_class, "main", "([Ljava/lang/String;)V");
        if (!main) throw std::runtime_error("static main(String[]) not found in " + options.main_class);
        dx::VmObjectRef argv;
        {
            const dx::VmExecutionLockScope lock(vm.ExecutionLock());
            argv = model.NewObjectArray(linker.ResolveDescriptor("[Ljava/lang/String;"),
                                        linker.ResolveDescriptor("Ljava/lang/String;"),
                                        static_cast<runtime::JniSize>(options.arguments.size()));
        }
        const auto argv_root = vm.ProtectReferences(std::array{argv});
        for (std::size_t i = 0; i < options.arguments.size(); ++i) {
            const dx::VmExecutionLockScope lock(vm.ExecutionLock());
            model.SetObjectElement(argv, static_cast<runtime::JniSize>(i),
                                   vm.NewStringUtf8(options.arguments[i]));
        }
        try {
            const std::array main_arguments{dx::VmValue::Ref(argv)};
            const auto entry_method = linker.FindDirectMethod(
                linker.ResolveDescriptor("Ldalvik/system/NativeStart;"), "main", "([Ljava/lang/String;)V");
            const auto outcome = vm.Call(*entry_method, main_arguments);
            if (outcome.exception.IsValid()) {
                status = 1;
                const auto thrown_root = vm.ProtectReferences(std::array{outcome.exception});
                DispatchUncaught(vm, outcome.exception);
            }
            threads.FinishRootThread();
            WaitForNonDaemonThreads(vm, threads);
        } catch (const dx::DexVmError&) {
            if (!vm.ExitCode()) throw;
        }
        while (const auto failure = threads.TakeFailure())
            WriteStderr("ogplay-run-dex: worker failure: " + *failure + "\n");
    } catch (...) {
        threads.Shutdown();
        if (!options.ledger_json.empty())
            WriteLedger(options.ledger_json, ledger, session->Process().CapabilityWarnings());
        throw;
    }
    threads.Shutdown();
    if (!options.ledger_json.empty()) WriteLedger(options.ledger_json, ledger, session->Process().CapabilityWarnings());
    if (const auto code = vm.ExitCode()) return *code;
    return status;
}

}  // namespace ogplay::app
