#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "ogplay/runtime/syscall/syscall.h"

TEST_CASE("ARM PR_SET_VMA validates and publishes anonymous map names") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress mapping{0x10000U};
    memory.Map({mapping, memory.PageSize()},
               ogplay::memory::PageProtection::none);
    const ogplay::memory::GuestAddress name{0x20000U};
    memory.Map({name, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::string text = "thread signal stack";
    std::vector<std::byte> encoded;
    for (const auto character : text) {
        encoded.push_back(
            static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    encoded.push_back(std::byte{});
    memory.Write(name, encoded);
    std::vector<ogplay::runtime::GuestVmaAnnotation> annotations;
    ogplay::runtime::BindAndroidProcessSyscalls(
        dispatcher, memory,
        [&annotations](const ogplay::runtime::GuestVmaAnnotation& annotation) {
            annotations.push_back(annotation);
        });

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 172;
    frame.thread_id = 61;
    frame.arguments[0] = 0x53564d41U;
    frame.arguments[1] = 0;
    frame.arguments[2] = mapping.Value();
    frame.arguments[3] = 1024;
    frame.arguments[4] = name.Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    REQUIRE(annotations.size() == 1);
    CHECK((annotations[0].range ==
           ogplay::memory::GuestRange{mapping, 1024}));
    CHECK(annotations[0].name == text);

    frame.arguments[2] = 0x30000U;
    CHECK(dispatcher.Dispatch(frame) == -14);
    frame.arguments[2] = mapping.Value();
    frame.arguments[0] = 15;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.thread_id = 0;
    CHECK(dispatcher.Dispatch(frame) == -3);
    CHECK_THROWS_AS(
        ogplay::runtime::BindAndroidProcessSyscalls(dispatcher, memory, {}),
        ogplay::runtime::SyscallError);
}
