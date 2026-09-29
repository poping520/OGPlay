#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>

#include "ogplay/runtime/dexvm/nio_runtime.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

TEST_CASE("BND45 real ARM libdl resolves thread Looper and preserves missing-symbol errors") {
    using namespace ogplay;
    std::vector<std::vector<std::byte>> images;
    std::vector<loader::Elf32ModuleInput> modules;
    for (const auto* name : {"libc.so", "libdl.so"}) {
        std::ifstream input(std::string(OGPLAY_SOURCE_DIR) + "/data/android/19/lib/" + name, std::ios::binary);
        REQUIRE(input.good());
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        auto& image = images.emplace_back(bytes.size());
        std::transform(bytes.begin(), bytes.end(), image.begin(), [](char value) { return static_cast<std::byte>(value); });
        modules.push_back({name, image, memory::GuestAddress{0x10000000U + static_cast<std::uint32_t>(modules.size()) * 0x10000000U}});
    }
    runtime::VirtualFileSystem fs;
    auto process = runtime::AndroidGuestProcess::Start({19, modules, {}, 64, 36, 1000000, 1, &fs, {}});
    auto memory = process->GuestMemoryAccess();
    const auto text = memory.allocate(128);
    const auto string = [&](std::string_view value) {
        std::vector<std::byte> bytes(value.size() + 1);
        std::transform(value.begin(), value.end(), bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
        memory.write(text, bytes);
        return text.Value();
    };
    const auto dlopen = process->FindModuleExport(1, "dlopen");
    const auto dlsym = process->FindModuleExport(1, "dlsym");
    const auto dlerror = process->FindModuleExport(1, "dlerror");
    const auto library = process->Invoke({dlopen, {string("libandroid.so"), 2}}).return_value;
    REQUIRE(library != 0);
    const auto lookup = [&](std::string_view name) {
        const memory::GuestAddress address{process->Invoke({dlsym, {library, string(name)}}).return_value};
        REQUIRE_FALSE(address.IsNull());
        return address;
    };
    const auto query = lookup("ALooper_forThread");
    const auto prepare = lookup("ALooper_prepare");
    const auto poll = lookup("ALooper_pollOnce");
    CHECK(process->Invoke({query, {}}).return_value == 0);
    const auto main = process->PrepareThreadLooper(1);
    CHECK(process->Invoke({query, {}}).return_value == main.Value());
    CHECK(process->Invoke({prepare, {1}}).return_value == main.Value());
    CHECK(static_cast<std::int32_t>(process->Invoke({poll, {0, 0, 0, 0}}).return_value) == -3);
    process->PrepareDexVmThread(100, 0);
    runtime::A32GuestCallFrame child;
    child.target = query;
    child.thread_id = 100;
    CHECK(process->Invoke(child).return_value == 0);
    child.target = prepare;
    const auto child_looper = process->Invoke(child).return_value;
    CHECK(child_looper != main.Value());
    process->ReleaseDexVmThread(100);
    process->PrepareDexVmThread(100, 0);
    child.target = query;
    CHECK(process->Invoke(child).return_value == 0);
    child.target = prepare;
    CHECK(process->Invoke(child).return_value != child_looper);
    process->ReleaseDexVmThread(100);
    for (const auto* name : {"ALooper_acquire", "ALooper_release", "ALooper_wake", "ALooper_removeFd", "ALooper_addFd", "ALooper_pollAll"})
        static_cast<void>(lookup(name));
    CHECK(process->Invoke({dlsym, {library, string("unsupported_test_symbol")}}).return_value == 0);
    CHECK(process->Invoke({dlerror, {}}).return_value != 0);
    CHECK(process->Invoke({dlerror, {}}).return_value == 0);
    memory.release(text, 128);
    process->Stop();
}
