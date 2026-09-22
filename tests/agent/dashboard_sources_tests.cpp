#include <doctest/doctest.h>
#include <future>
#include "ogplay/agent/dashboard.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/hal/clock.h"
using namespace ogplay;
TEST_CASE("Dashboard source DexVM busy snapshot never waits for execution lock") {
    runtime::JniStringStore strings; runtime::JniPrimitiveArrayStore arrays;
    runtime::dexvm::JavaObjectModel objects(strings, arrays); runtime::dexvm::DexClassLinker linker;
    core::CapabilityLedger ledger; runtime::dexvm::Interpreter vm(linker,objects,nullptr,ledger);
    const auto empty = vm.TrySnapshot(); REQUIRE(empty); CHECK(empty->heap_used == 0); CHECK(empty->heap_maximum > 0);
    static_cast<void>(objects.NewString(u"unrooted snapshot regression"));
    const auto before = vm.TrySnapshot(); REQUIRE(before); CHECK(before->heap_used > 0);
    const auto sweep = vm.CollectGarbage();
    const auto after = vm.TrySnapshot(); REQUIRE(after);
    CHECK(after->stats.gc_collections == before->stats.gc_collections + 1);
    CHECK(after->stats.gc_freed_bytes == sweep.freed_bytes);
    CHECK(after->stats.gc_pause_ns >= before->stats.gc_pause_ns);
    CHECK(after->heap_used < before->heap_used);

    std::promise<void> held, release;
    auto future = release.get_future();
    auto owner = std::async(std::launch::async,[&] { vm.ExecutionLock().Acquire(); held.set_value(); future.wait(); vm.ExecutionLock().Release(); });
    held.get_future().wait();
    const auto start = hal::Clock::SteadyTimestampNs();
    CHECK_FALSE(vm.TrySnapshot());
    CHECK(hal::Clock::SteadyTimestampNs() - start < 100000000ULL);
    release.set_value(); owner.get(); CHECK(vm.TrySnapshot());
}
TEST_CASE("Dashboard source JNI reference counts and busy propagation") {
    runtime::JniReferenceTable refs; refs.AttachThread(1);
    const runtime::JniObjectIdentity identity{runtime::JniObjectDomain::host,42};
    const auto local = refs.NewLocal(1,identity); const auto global = refs.NewGlobal(identity); const auto weak = refs.NewWeakGlobal(identity);
    const auto snapshot = refs.TrySnapshot(); REQUIRE(snapshot);
    CHECK(snapshot->local == 1); CHECK(snapshot->global == 1); CHECK(snapshot->weak_global == 1); CHECK(snapshot->attached_threads == 1);
    std::promise<void> held, release; auto future = release.get_future(); bool notified = false;
    auto owner = std::async(std::launch::async,[&] { refs.VisitRoots([&](auto) { if (!notified) { notified = true; held.set_value(); future.wait(); } }); });
    held.get_future().wait(); const auto start = hal::Clock::SteadyTimestampNs();
    CHECK_FALSE(refs.TrySnapshot()); CHECK(hal::Clock::SteadyTimestampNs() - start < 100000000ULL);
    release.set_value(); owner.get();
    refs.DeleteLocal(1,local); refs.DeleteGlobal(global); refs.DeleteWeakGlobal(weak); CHECK(refs.TrySnapshot()->local == 0);
}
TEST_CASE("Dashboard source memory counts reflect protection and mapping changes") {
    memory::AddressSpace memory; const memory::GuestAddress address{0x10000};
    memory.Map({address,8192},memory::PageProtection::read | memory::PageProtection::write);
    auto snapshot = memory.TrySnapshot(); REQUIRE(snapshot); CHECK(snapshot->pages_by_protection[3] == 2);
    memory.Protect({address,4096},memory::PageProtection::none);
    snapshot = memory.TrySnapshot(); REQUIRE(snapshot); CHECK(snapshot->pages_by_protection[0] == 1); CHECK(snapshot->pages_by_protection[3] == 1);
    memory.Unmap({address,8192}); CHECK(memory.TrySnapshot()->pages_by_protection[0] == 0);
}
TEST_CASE("Dashboard source VFS bounded descriptor identities and busy reads") {
    runtime::VirtualFileSystem vfs; std::promise<void> held, release; auto future = release.get_future();
    runtime::VfsLazyMountEntry entry; entry.path="file"; entry.size=1;
    entry.read_all=[] { return std::vector<std::byte>{std::byte{1}}; };
    entry.read_at=[&](std::uint64_t, std::span<std::byte> bytes) { held.set_value(); future.wait(); bytes[0]=std::byte{1}; return std::size_t{1}; };
    vfs.MountLazyReadOnly(runtime::VfsSource::apk,"/assets",std::span{&entry,1});
    const auto fd=vfs.Open("/assets/file",{.read=true}); const auto node=vfs.TryDescriptorNode(fd); REQUIRE(node);
    auto read=std::async(std::launch::async,[&] { std::array<std::byte,1> bytes{}; return vfs.Read(fd,bytes); });
    held.get_future().wait(); const auto start=hal::Clock::SteadyTimestampNs();
    const auto snapshot=vfs.TrySnapshot(); CHECK(hal::Clock::SteadyTimestampNs()-start<100000000ULL);
    release.set_value(); CHECK(read.get()==1);
    REQUIRE(snapshot); CHECK(snapshot->partial); CHECK(snapshot->descriptors[0].busy); CHECK(snapshot->mounts[0].root=="/assets");
    CHECK(vfs.TrySnapshot()->descriptors[0].node_id==node); vfs.Close(fd); CHECK(vfs.TrySnapshot()->total_descriptors==0);
}
