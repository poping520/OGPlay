#include <array>
#include <cstddef>
#include <cstdint>

#include <doctest/doctest.h>

#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
#include "ogplay/runtime/jni/jni_java_vm.h"

namespace {

[[nodiscard]] std::uint32_t Read32(
    ogplay::memory::AddressSpace& memory,
    const ogplay::memory::GuestAddress address) {
    std::array<std::byte, 4> bytes{};
    memory.Read(address, bytes);
    std::uint32_t result{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[index]))
                  << static_cast<unsigned>(index * 8U);
    }
    return result;
}

}  // namespace

TEST_CASE("guest JNI ABI publishes complete 32-bit interface objects") {
    ogplay::memory::AddressSpace memory;
    {
        const ogplay::runtime::GuestJniAbi abi(memory);
        CHECK(abi.Environment() == ogplay::runtime::kJniGuestEnvironment);
        CHECK(abi.JavaVm() == ogplay::runtime::kJniGuestJavaVm);
        CHECK(Read32(memory, abi.Environment()) ==
              ogplay::runtime::kJniGuestEnvironmentTable.Value());
        CHECK(Read32(memory, abi.JavaVm()) ==
              ogplay::runtime::kJniGuestInvokeTable.Value());

        for (std::size_t slot = 0;
             slot < ogplay::runtime::kJniReservedSlotCount; ++slot) {
            CHECK(Read32(
                      memory,
                      ogplay::runtime::kJniGuestEnvironmentTable.Add(
                          slot * sizeof(std::uint32_t))) == 0U);
        }
        for (std::size_t slot = 0; slot < 3U; ++slot) {
            CHECK(Read32(
                      memory,
                      ogplay::runtime::kJniGuestInvokeTable.Add(
                          slot * sizeof(std::uint32_t))) == 0U);
        }

        const auto get_version =
            ogplay::runtime::FindJniSlot("GetVersion")->Value();
        const auto direct_buffer =
            ogplay::runtime::FindJniSlot("NewDirectByteBuffer")->Value();
        const auto destroy_vm =
            ogplay::runtime::FindJniInvokeSlot("DestroyJavaVM")->Value();
        const auto get_env =
            ogplay::runtime::FindJniInvokeSlot("GetEnv")->Value();
        for (const auto slot : {get_version, direct_buffer}) {
            const auto target = Read32(
                memory, ogplay::runtime::kJniGuestEnvironmentTable.Add(
                            slot * sizeof(std::uint32_t)));
            const auto described = ogplay::runtime::DescribeJniGuestThunk(
                ogplay::memory::GuestAddress{target});
            REQUIRE(described.has_value());
            CHECK_FALSE(described->java_vm);
            CHECK(described->slot == slot);
            CHECK((target & 1U) == 1U);
        }
        for (const auto slot : {destroy_vm, get_env}) {
            const auto target = Read32(
                memory, ogplay::runtime::kJniGuestInvokeTable.Add(
                            slot * sizeof(std::uint32_t)));
            const auto described = ogplay::runtime::DescribeJniGuestThunk(
                ogplay::memory::GuestAddress{target});
            REQUIRE(described.has_value());
            CHECK(described->java_vm);
            CHECK(described->slot == slot);
            CHECK((target & 1U) == 1U);
        }

        ogplay::core::CapabilityLedger ledger;
        ogplay::runtime::JniFunctionTable environment_table(ledger);
        ogplay::runtime::JniInvokeFunctionTable invoke_table(ledger);
        const ogplay::runtime::JniCommonSlotDirectory directory;
        directory.Install(environment_table, invoke_table);
        CHECK(environment_table.Resolve(
                  ogplay::runtime::JniSlot{get_version}, 0x1000U)
              .Value() ==
              Read32(memory,
                     ogplay::runtime::kJniGuestEnvironmentTable.Add(
                         get_version * sizeof(std::uint32_t))));
        CHECK(invoke_table.Resolve(
                  ogplay::runtime::JniInvokeSlot{
                      static_cast<std::uint8_t>(get_env)},
                  0x1000U)
              .Value() ==
              Read32(memory,
                     ogplay::runtime::kJniGuestInvokeTable.Add(
                         get_env * sizeof(std::uint32_t))));
        CHECK_THROWS_AS(
            static_cast<void>(environment_table.Resolve(
                ogplay::runtime::JniSlot{direct_buffer}, 0x2000U)),
            ogplay::runtime::JniUnimplementedCall);
        CHECK_THROWS_AS(
            static_cast<void>(invoke_table.Resolve(
                ogplay::runtime::JniInvokeSlot{
                    static_cast<std::uint8_t>(destroy_vm)},
                0x3000U)),
            ogplay::runtime::JniInvokeUnimplementedCall);
    }

    std::array<std::byte, 1> byte{};
    CHECK_THROWS_AS(memory.Read(ogplay::runtime::kJniGuestEnvironment, byte),
                    ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        memory.Fetch(ogplay::memory::GuestAddress{
                         ogplay::runtime::kJniThunkBegin},
                     byte),
        ogplay::memory::MemoryFault);
}

TEST_CASE("guest JNI ABI maps trap code RX and tables read-only") {
    ogplay::memory::AddressSpace memory;
    const ogplay::runtime::GuestJniAbi abi(memory);
    const auto slot = ogplay::runtime::FindJniSlot("GetVersion")->Value();
    const auto target = Read32(
        memory, ogplay::runtime::kJniGuestEnvironmentTable.Add(
                    slot * sizeof(std::uint32_t)));
    std::array<std::byte, 4> code{};
    memory.Fetch(ogplay::memory::GuestAddress{target & ~1U}, code);
    CHECK(code == std::array<std::byte, 4>{
                      std::byte{0x03}, std::byte{0xdf},
                      std::byte{0x70}, std::byte{0x47}});

    const std::array<std::byte, 1> value{std::byte{0xff}};
    CHECK_THROWS_AS(
        memory.Write(ogplay::runtime::kJniGuestEnvironmentTable, value),
        ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        memory.Write(ogplay::memory::GuestAddress{target & ~1U}, value),
        ogplay::memory::MemoryFault);
}

TEST_CASE("guest JNI ABI construction rolls back occupied layouts") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestRange occupied{
        ogplay::memory::GuestAddress{ogplay::runtime::kJniInvokeThunkBegin},
        memory.PageSize()};
    memory.Map(occupied, ogplay::memory::PageProtection::read);
    CHECK_THROWS(static_cast<void>(ogplay::runtime::GuestJniAbi(memory)));

    const ogplay::memory::GuestRange environment_page{
        ogplay::memory::GuestAddress{ogplay::runtime::kJniThunkBegin},
        memory.PageSize()};
    CHECK_THROWS_AS(memory.ValidateMapped(environment_page),
                    ogplay::memory::MemoryFault);
    memory.ValidateMapped(occupied);
    memory.Unmap(occupied);
}
