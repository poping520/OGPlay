# ARM64 monitor readers publish an atomic reservation and validate a writer
# epoch. Writers retain the existing lock, invalidation and CAS operation.
macro(_rp_exclusive_epoch relative)
    if(relative STREQUAL "dynarmic/interface/exclusive_monitor.h")
        string(REPLACE "        exclusive_addresses[processor_id] = masked_address;"
            "        std::atomic_ref<VAddr>(exclusive_addresses[processor_id]).store(masked_address, std::memory_order_release);" _source "${_source}")
        string(REPLACE "        Unlock();\n        return result;" "        EndWrite();\n        return result;" _source "${_source}")
        string(REPLACE "    void Unlock();" "    void Unlock();\n    void BeginWrite();\n    void EndWrite();" _source "${_source}")
        string(REPLACE "    SpinLock lock;"
            "    friend std::atomic<std::uint64_t>* GetExclusiveMonitorEpochPointer(ExclusiveMonitor*);\n    std::atomic<std::uint64_t> write_epoch{};\n    SpinLock lock;" _source "${_source}")
        string(REPLACE "exclusive_addresses[processor_id]" "reservations[processor_id].address" _source "${_source}")
        string(REPLACE "exclusive_values[processor_id]" "reservations[processor_id].value" _source "${_source}")
        string(REPLACE "    std::vector<VAddr> exclusive_addresses;\n    std::vector<Vector> exclusive_values;"
            "    struct alignas(128) Reservation {\n        VAddr address{INVALID_EXCLUSIVE_ADDRESS};\n        Vector value{};\n    };\n    friend size_t GetExclusiveMonitorReservationStride(ExclusiveMonitor*);\n    std::vector<Reservation> reservations;" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/exclusive_monitor.cpp")
        string(REPLACE "bool ExclusiveMonitor::CheckAndClear"
            "void ExclusiveMonitor::BeginWrite() {\n    Lock();\n    write_epoch.fetch_add(1, std::memory_order_relaxed);\n    std::atomic_thread_fence(std::memory_order_seq_cst);\n}\n\nvoid ExclusiveMonitor::EndWrite() {\n    write_epoch.fetch_add(1, std::memory_order_release);\n    Unlock();\n}\n\nbool ExclusiveMonitor::CheckAndClear" _source "${_source}")
        string(REPLACE "    Lock();\n    if (exclusive_addresses[processor_id] != masked_address) {\n        Unlock();"
            "    BeginWrite();\n    if (std::atomic_ref<VAddr>(exclusive_addresses[processor_id]).load(std::memory_order_acquire) != masked_address) {\n        EndWrite();" _source "${_source}")
        string(REPLACE "        if (other_address == masked_address) {\n            other_address = INVALID_EXCLUSIVE_ADDRESS;"
            "        auto reservation = std::atomic_ref<VAddr>(other_address);\n        if (reservation.load(std::memory_order_acquire) == masked_address) {\n            reservation.store(INVALID_EXCLUSIVE_ADDRESS, std::memory_order_release);" _source "${_source}")
        string(REPLACE "    Lock();\n    std::fill(exclusive_addresses.begin(), exclusive_addresses.end(), INVALID_EXCLUSIVE_ADDRESS);\n    Unlock();"
            "    BeginWrite();\n    for (auto& address : exclusive_addresses) {\n        std::atomic_ref<VAddr>(address).store(INVALID_EXCLUSIVE_ADDRESS, std::memory_order_release);\n    }\n    EndWrite();" _source "${_source}")
        string(REPLACE "    exclusive_addresses[processor_id] = INVALID_EXCLUSIVE_ADDRESS;"
            "    std::atomic_ref<VAddr>(exclusive_addresses[processor_id]).store(INVALID_EXCLUSIVE_ADDRESS, std::memory_order_release);" _source "${_source}")
        string(REPLACE ": exclusive_addresses(processor_count, INVALID_EXCLUSIVE_ADDRESS), exclusive_values(processor_count) {}"
            ": reservations(processor_count) {}" _source "${_source}")
        string(REPLACE "exclusive_addresses.size()" "reservations.size()" _source "${_source}")
        string(REPLACE "exclusive_addresses[processor_id]" "reservations[processor_id].address" _source "${_source}")
        string(REPLACE "for (VAddr& other_address : exclusive_addresses) {"
            "for (auto& slot : reservations) {\n        auto& other_address = slot.address;" _source "${_source}")
        string(REPLACE "for (auto& address : exclusive_addresses) {"
            "for (auto& slot : reservations) {\n        auto& address = slot.address;" _source "${_source}")
    endif()
endmacro()
