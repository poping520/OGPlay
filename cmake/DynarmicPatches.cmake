# Checked adaptations of the pinned Dynarmic submodule, generated under the build tree.
# Every input is hash-verified or asserted, so a submodule update forces a review.

# ---- ARM64 cumulative FPSR ----
# A32 keeps guest cumulative FPSR live across linked blocks. All C++ boundaries
# publish/reload state, and Run saves/restores the caller's FPSR independently.
macro(_rp_retain_fpsr relative)
    if(relative STREQUAL "dynarmic/backend/arm64/emit_arm64.h")
        string(REPLACE "    bool preserve_memory_fpsr{};"
            "    bool preserve_memory_fpsr{};\n    bool retain_fpsr_between_blocks{};" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/fpsr_manager.h")
        string(REPLACE "size_t state_fpsr_offset);" "size_t state_fpsr_offset, bool retain = false);" _source "${_source}")
        string(REPLACE "    void Spill();" "    void Spill();\n    void FinishBlock();" _source "${_source}")
        string(REPLACE "    bool fpsr_loaded = false;" "    bool fpsr_loaded = false;\n    bool retain_between_blocks = false;" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/fpsr_manager.cpp")
        string(REPLACE "size_t state_fpsr_offset)" "size_t state_fpsr_offset, bool retain)" _source "${_source}")
        string(REPLACE ": code{code}, state_fpsr_offset{state_fpsr_offset} {}"
            ": code{code}, state_fpsr_offset{state_fpsr_offset}, fpsr_loaded{retain}, retain_between_blocks{retain} {}"
            _source "${_source}")
        string(REPLACE "void FpsrManager::Spill() {"
            "void FpsrManager::Spill() {\n    if (retain_between_blocks) {\n        Load();\n        code.MRS(Xscratch0, oaknut::SystemReg::FPSR);\n        code.STR(Wscratch0, Xstate, state_fpsr_offset);\n        fpsr_loaded = false;\n        return;\n    }" _source "${_source}")
        string(REPLACE "    code.MSR(oaknut::SystemReg::FPSR, XZR);"
            "    if (retain_between_blocks) {\n        code.LDR(Wscratch0, Xstate, state_fpsr_offset);\n        code.MSR(oaknut::SystemReg::FPSR, Xscratch0);\n    } else {\n        code.MSR(oaknut::SystemReg::FPSR, XZR);\n    }" _source "${_source}")
        string(REPLACE "void FpsrManager::GetFpsr(oaknut::WReg dest) {"
            "void FpsrManager::FinishBlock() {\n    if (retain_between_blocks) Load();\n    else Spill();\n}\n\nvoid FpsrManager::GetFpsr(oaknut::WReg dest) {\n    if (retain_between_blocks) {\n        Load();\n        code.MRS(dest.toX(), oaknut::SystemReg::FPSR);\n        return;\n    }" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/stack_layout.h")
        string(REPLACE "    u32 save_host_fpcr;" "    u32 save_host_fpcr;\n    u32 save_host_fpsr;" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/emit_arm64.cpp")
        string(REPLACE "FpsrManager fpsr_manager{code, conf.state_fpsr_offset};"
            "FpsrManager fpsr_manager{code, conf.state_fpsr_offset, conf.retain_fpsr_between_blocks};" _source "${_source}")
        string(REPLACE "    fpsr_manager.Spill();" "    fpsr_manager.FinishBlock();" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/a32_address_space.cpp")
        string(REPLACE "        .preserve_memory_fpsr = true," "        .preserve_memory_fpsr = true,\n        .retain_fpsr_between_blocks = true," _source "${_source}")
        # Three ABI trampolines (callbacks, exclusive read/write) formerly tail-
        # called C++. Preserve LR, publish guest FPSR, and reload updated state.
        string(REPLACE
            "    void* target = code.xptr<void*>();\n    code.LDR(X0, l_this);\n    code.LDR(Xscratch0, l_addr);\n    code.BR(Xscratch0);"
            "    void* target = code.xptr<void*>();\n    ABI_PushRegisters(code, ToRegList(X30), 0);\n    code.MRS(Xscratch0, oaknut::SystemReg::FPSR);\n    code.STR(Wscratch0, Xstate, offsetof(A32JitState, fpsr));\n    code.LDR(X0, l_this);\n    code.LDR(Xscratch0, l_addr);\n    code.BLR(Xscratch0);\n    code.LDR(Wscratch0, Xstate, offsetof(A32JitState, fpsr));\n    code.MSR(oaknut::SystemReg::FPSR, Xscratch0);\n    ABI_PopRegisters(code, ToRegList(X30), 0);\n    code.RET();"
            _source "${_source}")
        string(REPLACE "        code.MOV(Xhalt, X2);"
            "        code.MOV(Xhalt, X2);\n        code.MRS(Xscratch0, oaknut::SystemReg::FPSR);\n        code.STR(Wscratch0, SP, offsetof(StackLayout, save_host_fpsr));\n        code.LDR(Wscratch0, Xstate, offsetof(A32JitState, fpsr));\n        code.MSR(oaknut::SystemReg::FPSR, Xscratch0);"
            _source "${_source}")
        # Dispatcher compilation can use host floating-point operations too.
        string(REPLACE "        code.BLR(Xscratch0);\n        code.BR(X0);"
            "        code.MRS(Xscratch1, oaknut::SystemReg::FPSR);\n        code.STR(Wscratch1, Xstate, offsetof(A32JitState, fpsr));\n        code.BLR(Xscratch0);\n        code.LDR(Wscratch1, Xstate, offsetof(A32JitState, fpsr));\n        code.MSR(oaknut::SystemReg::FPSR, Xscratch1);\n        code.BR(X0);"
            _source "${_source}")
        string(REPLACE "        code.l(return_from_run_code);"
            "        code.l(return_from_run_code);\n        code.MRS(Xscratch0, oaknut::SystemReg::FPSR);\n        code.STR(Wscratch0, Xstate, offsetof(A32JitState, fpsr));"
            _source "${_source}")
        string(REPLACE "        code.LDR(Wscratch0, SP, offsetof(StackLayout, save_host_fpcr));\n        code.MSR(oaknut::SystemReg::FPCR, Xscratch0);"
            "        code.LDR(Wscratch0, SP, offsetof(StackLayout, save_host_fpcr));\n        code.MSR(oaknut::SystemReg::FPCR, Xscratch0);\n        code.LDR(Wscratch0, SP, offsetof(StackLayout, save_host_fpsr));\n        code.MSR(oaknut::SystemReg::FPSR, Xscratch0);"
            _source "${_source}")
    endif()
endmacro()

# ---- ARM64 exclusive monitor epoch ----
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

# ---- A32 coprocessor (all hosts) ----
# Checked A32 frontend adaptation. Never edit the pinned submodule in place.
# Keep original decoding/architectural checks and conditional dispatch; replace
# only the generic coprocessor visitor bodies before they reach the JIT emitter.
set(_cp_root "${PROJECT_SOURCE_DIR}/third_party/dynarmic/src/dynarmic/frontend/A32/translate/impl")
set(_cp_output "${CMAKE_BINARY_DIR}/generated/dynarmic-coprocessor")
file(MAKE_DIRECTORY "${_cp_output}")
target_include_directories(dynarmic PRIVATE "${PROJECT_SOURCE_DIR}/src/cpu")

set(_cp_unsupported [=[
// Runtime callback receives the precise PC and pre-instruction IT state.
static bool OgplayUnsupported(TranslatorVisitor& v) {
    static_assert(static_cast<u32>(Exception::NoExecuteFault) <
                  ogplay::cpu::detail::kUnsupportedInstructionTag);
    v.ir.BranchWritePC(v.ir.Imm32(v.ir.current_location.PC()));
    v.ir.ExceptionRaised(static_cast<Exception>(
        ogplay::cpu::detail::kUnsupportedInstructionTag | v.ir.current_location.IT().Value()));
    v.ir.SetTerm(IR::Term::CheckHalt{IR::Term::ReturnToDispatch{}});
    return false;
}
]=])

foreach(_cp_kind IN ITEMS arm thumb32)
    if(_cp_kind STREQUAL "arm")
        set(_cp_name "coprocessor.cpp")
        set(_cp_expected "519ab8a62433384a9ee93e2259287c818cad369dfe434d0aaaeba4654478a7d8")
    else()
        set(_cp_name "thumb32_coprocessor.cpp")
        set(_cp_expected "5c17cbb995c48201a63bd5c835734b3a73e4f21d575a35e0cf97a38736f24a53")
    endif()
    set(_cp_input "${_cp_root}/${_cp_name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_cp_input}")
    file(READ "${_cp_input}" _cp_source)
    # Hash normalized text so Git's checkout line endings do not affect the gate.
    string(REPLACE "\r\n" "\n" _cp_source "${_cp_source}")
    string(SHA256 _cp_hash "${_cp_source}")
    if(NOT _cp_hash STREQUAL _cp_expected)
        message(FATAL_ERROR "Dynarmic ${_cp_name} changed; review the OGPlay coprocessor adaptation")
    endif()
    foreach(_cp_op IN ITEMS CDP LDC STC MCR MRC MCRR MRRC)
        string(FIND "${_cp_source}" "bool TranslatorVisitor::${_cp_kind}_${_cp_op}(" _cp_start)
        string(SUBSTRING "${_cp_source}" ${_cp_start} -1 _cp_tail)
        string(FIND "${_cp_tail}" "\n}\n" _cp_end)
        math(EXPR _cp_end "${_cp_end} + 3")
        string(SUBSTRING "${_cp_tail}" 0 ${_cp_end} _cp_original)
        if(_cp_kind STREQUAL "arm")
            # Retain upstream UDF/unpredictable checks, then its condition gate.
            string(FIND "${_cp_original}" "    const bool two = cond == Cond::NV;" _cp_cut)
            string(SUBSTRING "${_cp_original}" 0 ${_cp_cut} _cp_replacement)
            string(APPEND _cp_replacement "    const bool two = cond == Cond::NV;\n    if (!two && !ArmConditionPassed(cond)) return true;\n")
        else()
            # TranslateThumb already handles IT conditions before the visitor.
            string(FIND "${_cp_original}" "{\n" _cp_cut)
            math(EXPR _cp_cut "${_cp_cut} + 2")
            string(SUBSTRING "${_cp_original}" 0 ${_cp_cut} _cp_replacement)
        endif()
        if(_cp_op STREQUAL "MCR")
            if(_cp_kind STREQUAL "thumb32")
                string(APPEND _cp_replacement "    if (t == Reg::PC) return UnpredictableInstruction();\n")
            endif()
            string(APPEND _cp_replacement "    if (!two && coproc_no == 15 && opc1 == 0 && CRn == CoprocReg::C7) {\n        if (CRm == CoprocReg::C10 && opc2 == 5) return ${_cp_kind}_DMB(Imm<4>{15});\n        if (CRm == CoprocReg::C10 && opc2 == 4) return ${_cp_kind}_DSB(Imm<4>{15});\n        if (CRm == CoprocReg::C5 && opc2 == 4) return ${_cp_kind}_ISB(Imm<4>{15});\n    }\n")
        elseif(_cp_op STREQUAL "MRC")
            string(APPEND _cp_replacement [=[
    if (!two && coproc_no == 15 && opc1 == 0 && CRn == CoprocReg::C13 &&
        CRm == CoprocReg::C0 && opc2 == 3) {
        const auto word = ir.CoprocGetOneWord(coproc_no, two, opc1, CRn, CRm, opc2);
        if (t != Reg::PC) ir.SetRegister(t, word);
        else ir.SetCpsrNZCVRaw(ir.And(word, ir.Imm32(0xf0000000)));
        return true;
    }
]=])
        endif()
        string(APPEND _cp_replacement "    return OgplayUnsupported(*this);\n}\n")
        string(REPLACE "${_cp_original}" "${_cp_replacement}" _cp_source "${_cp_source}")
    endforeach()
    string(REPLACE "namespace Dynarmic::A32 {" "#include \"dynarmic_guest_fault.h\"\n#include \"dynarmic/interface/A32/config.h\"\n\nnamespace Dynarmic::A32 {\n${_cp_unsupported}" _cp_source "${_cp_source}")
    file(WRITE "${_cp_output}/${_cp_name}.in" "${_cp_source}")
    configure_file("${_cp_output}/${_cp_name}.in" "${_cp_output}/${_cp_name}" COPYONLY)
    get_target_property(_cp_sources dynarmic SOURCES)
    set(_cp_found FALSE)
    foreach(_cp_item IN LISTS _cp_sources)
        if(_cp_item MATCHES "frontend/A32/translate/impl/${_cp_name}$")
            list(REMOVE_ITEM _cp_sources "${_cp_item}")
            set(_cp_found TRUE)
        endif()
    endforeach()
    if(NOT _cp_found)
        message(FATAL_ERROR "Dynarmic coprocessor translation source missing: ${_cp_name}")
    endif()
    list(APPEND _cp_sources "${_cp_output}/${_cp_name}")
    set_property(TARGET dynarmic PROPERTY SOURCES "${_cp_sources}")
endforeach()

# ---- A32 precise undefined exception state (all hosts) ----
# RaiseException publishes the next Thumb IT state. Carry the faulting IT
# state separately for undefined/unpredictable stops, as for coprocessor stops.
set(_undef_name "a32_translate_impl.cpp")
set(_undef_input "${_cp_root}/${_undef_name}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_undef_input}")
file(READ "${_undef_input}" _undef_source)
string(REPLACE "\r\n" "\n" _undef_source "${_undef_source}")
string(SHA256 _undef_hash "${_undef_source}")
if(NOT _undef_hash STREQUAL "5d548d1249bcebc37005d98085a76c1f5476397427f080482cbddf7f777250e6")
    message(FATAL_ERROR "Dynarmic ${_undef_name} changed; review the OGPlay undefined exception adaptation")
endif()
string(REPLACE "namespace Dynarmic::A32 {" "#include \"dynarmic_guest_fault.h\"\n\nnamespace Dynarmic::A32 {" _undef_source "${_undef_source}")
string(REPLACE "    ir.ExceptionRaised(exception);" [=[
    if (exception == Exception::UndefinedInstruction || exception == Exception::UnpredictableInstruction) {
        static_assert(static_cast<u32>(Exception::UndefinedInstruction) <= 0xff &&
                      static_cast<u32>(Exception::UnpredictableInstruction) <= 0xff);
        exception = static_cast<Exception>(ogplay::cpu::detail::kUndefinedInstructionTag |
                                           (static_cast<u32>(exception) << 8) | ir.current_location.IT().Value());
    }
    ir.ExceptionRaised(exception);]=] _undef_source "${_undef_source}")
file(WRITE "${_cp_output}/${_undef_name}.in" "${_undef_source}")
configure_file("${_cp_output}/${_undef_name}.in" "${_cp_output}/${_undef_name}" COPYONLY)
get_target_property(_undef_sources dynarmic SOURCES)
list(FILTER _undef_sources EXCLUDE REGEX "frontend/A32/translate/impl/${_undef_name}$")
list(APPEND _undef_sources "${_cp_output}/${_undef_name}")
set_property(TARGET dynarmic PROPERTY SOURCES "${_undef_sources}")

# ---- A32 ASIMD narrowing and saturating long (all hosts) ----
# Upstream leaves these A32 decoders commented out although the IR can express them.
# VQRSHL stays out: it needs a new IR opcode and backend code on every host.
set(_asimd_output "${CMAKE_BINARY_DIR}/generated/dynarmic-asimd")
set(_asimd_impl "frontend/A32/translate/impl")
foreach(_asimd_entry IN ITEMS
        "frontend/A32/decoder/asimd.h|f9fc42db9bdbef91cba957da781b4caafd11a2d4d5e4ba1d3b7c8ffc42190186"
        "${_asimd_impl}/a32_translate_impl.h|0c456ec642e9bb5a7a59de868596744574d3852eb2313129a6ee1b45ccbd0a30"
        "${_asimd_impl}/asimd_three_regs.cpp|ac2af5bf6c5ec9b3ab65c6380f2201d6895ab6b67fc7fbb15624b7cd0be28706"
        "${_asimd_impl}/asimd_two_regs_scalar.cpp|dc6c75540d348569fbceebd7c785a6a09728f19d540d6becdde27fc59894deda")
    string(REPLACE "|" ";" _asimd_pair "${_asimd_entry}")
    list(GET _asimd_pair 0 _asimd_relative)
    list(GET _asimd_pair 1 _asimd_expected)
    set(_asimd_input "${PROJECT_SOURCE_DIR}/third_party/dynarmic/src/dynarmic/${_asimd_relative}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_asimd_input}")
    file(READ "${_asimd_input}" _asimd_source)
    string(REPLACE "\r\n" "\n" _asimd_source "${_asimd_source}")
    string(SHA256 _asimd_hash "${_asimd_source}")
    if(NOT _asimd_hash STREQUAL _asimd_expected)
        message(FATAL_ERROR "Dynarmic ${_asimd_relative} changed; review the OGPlay ASIMD adaptation")
    endif()
    if(_asimd_relative MATCHES "decoder/asimd\\.h$")
        string(REPLACE "#include \"./asimd.inc\"" [=[#include "dynarmic/frontend/A32/decoder/asimd.inc"
INST(asimd_VADDHN,          "VADDHN",                   "111100101Dzznnnndddd0100N0M0mmmm") // ASIMD
INST(asimd_VRADDHN,         "VRADDHN",                  "111100111Dzznnnndddd0100N0M0mmmm") // ASIMD
INST(asimd_VSUBHN,          "VSUBHN",                   "111100101Dzznnnndddd0110N0M0mmmm") // ASIMD
INST(asimd_VRSUBHN,         "VRSUBHN",                  "111100111Dzznnnndddd0110N0M0mmmm") // ASIMD
INST(asimd_VQDMLAL,         "VQDMLAL/VQDMLSL",          "111100101Dzznnnndddd10o1N0M0mmmm") // ASIMD
INST(asimd_VQDMULL,         "VQDMULL",                  "111100101Dzznnnndddd1101N0M0mmmm") // ASIMD
INST(asimd_VQDMLAL_scalar,  "VQDMLAL/VQDMLSL (scalar)", "111100101Dzznnnndddd0o11N1M0mmmm") // ASIMD]=] _asimd_source "${_asimd_source}")
    elseif(_asimd_relative MATCHES "a32_translate_impl\\.h$")
        string(REPLACE "    bool asimd_VABDL(bool U, bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm);" [=[    bool asimd_VABDL(bool U, bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm);
    bool asimd_VADDHN(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm);
    bool asimd_VRADDHN(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm);
    bool asimd_VSUBHN(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm);
    bool asimd_VRSUBHN(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm);
    bool asimd_VQDMLAL(bool D, size_t sz, size_t Vn, size_t Vd, bool op, bool N, bool M, size_t Vm);
    bool asimd_VQDMULL(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm);
    bool asimd_VQDMLAL_scalar(bool D, size_t sz, size_t Vn, size_t Vd, bool op, bool N, bool M, size_t Vm);]=] _asimd_source "${_asimd_source}")
    elseif(_asimd_relative MATCHES "asimd_three_regs\\.cpp$")
        string(REPLACE "// ASIMD Three registers of different length\n" [=[// ASIMD Three registers of different length

// Dd = narrow(high half of Qn +/- Qm); the rounding forms add half of the discarded range first.
static bool NarrowHighHalf(TranslatorVisitor& v, bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm, bool subtract, bool rounding) {
    if (sz == 0b11) {
        return v.DecodeError();
    }
    if (mcl::bit::get_bit<0>(Vn) || mcl::bit::get_bit<0>(Vm)) {
        return v.UndefinedInstruction();
    }
    const size_t esize = 8U << sz;
    const size_t wide = esize * 2;
    const auto reg_n = v.ir.GetVector(ToVector(true, Vn, N));
    const auto reg_m = v.ir.GetVector(ToVector(true, Vm, M));
    auto result = subtract ? v.ir.VectorSub(wide, reg_n, reg_m) : v.ir.VectorAdd(wide, reg_n, reg_m);
    if (rounding) {
        const IR::UAny half = sz == 0b00   ? IR::UAny{v.ir.Imm16(u16(1) << 7)}
                              : sz == 0b01 ? IR::UAny{v.ir.Imm32(u32(1) << 15)}
                                           : IR::UAny{v.ir.Imm64(u64(1) << 31)};
        result = v.ir.VectorAdd(wide, result, v.ir.VectorBroadcast(wide, half));
    }
    v.ir.SetVector(ToVector(false, Vd, D), v.ir.VectorNarrow(wide, v.ir.VectorLogicalShiftRight(wide, result, static_cast<u8>(esize))));
    return true;
}

bool TranslatorVisitor::asimd_VADDHN(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm) {
    return NarrowHighHalf(*this, D, sz, Vn, Vd, N, M, Vm, false, false);
}

bool TranslatorVisitor::asimd_VRADDHN(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm) {
    return NarrowHighHalf(*this, D, sz, Vn, Vd, N, M, Vm, false, true);
}

bool TranslatorVisitor::asimd_VSUBHN(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm) {
    return NarrowHighHalf(*this, D, sz, Vn, Vd, N, M, Vm, true, false);
}

bool TranslatorVisitor::asimd_VRSUBHN(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm) {
    return NarrowHighHalf(*this, D, sz, Vn, Vd, N, M, Vm, true, true);
}

bool TranslatorVisitor::asimd_VQDMULL(bool D, size_t sz, size_t Vn, size_t Vd, bool N, bool M, size_t Vm) {
    if (sz == 0b11) {
        return DecodeError();
    }
    if (sz == 0b00 || mcl::bit::get_bit<0>(Vd)) {
        return UndefinedInstruction();
    }
    const size_t esize = 8U << sz;
    const auto reg_n = ir.GetVector(ToVector(false, Vn, N));
    const auto reg_m = ir.GetVector(ToVector(false, Vm, M));
    ir.SetVector(ToVector(true, Vd, D), ir.VectorSignedSaturatedDoublingMultiplyLong(esize, reg_n, reg_m));
    return true;
}

bool TranslatorVisitor::asimd_VQDMLAL(bool D, size_t sz, size_t Vn, size_t Vd, bool op, bool N, bool M, size_t Vm) {
    if (sz == 0b11) {
        return DecodeError();
    }
    if (sz == 0b00 || mcl::bit::get_bit<0>(Vd)) {
        return UndefinedInstruction();
    }
    const size_t esize = 8U << sz;
    const auto d = ToVector(true, Vd, D);
    const auto product = ir.VectorSignedSaturatedDoublingMultiplyLong(esize, ir.GetVector(ToVector(false, Vn, N)), ir.GetVector(ToVector(false, Vm, M)));
    const auto reg_d = ir.GetVector(d);
    ir.SetVector(d, op ? ir.VectorSignedSaturatedSub(esize * 2, reg_d, product) : ir.VectorSignedSaturatedAdd(esize * 2, reg_d, product));
    return true;
}
]=] _asimd_source "${_asimd_source}")
    else()
        string(REPLACE "bool TranslatorVisitor::asimd_VQDMULH_scalar(" [=[bool TranslatorVisitor::asimd_VQDMLAL_scalar(bool D, size_t sz, size_t Vn, size_t Vd, bool op, bool N, bool M, size_t Vm) {
    if (sz == 0b11) {
        return DecodeError();
    }
    if (sz == 0b00 || mcl::bit::get_bit<0>(Vd)) {
        return UndefinedInstruction();
    }
    const size_t esize = 8U << sz;
    const auto d = ToVector(true, Vd, D);
    const auto [m, index] = GetScalarLocation(esize, M, Vm);
    const auto reg_m = ir.VectorBroadcastElement(esize, ir.GetVector(m), index);
    const auto product = ir.VectorSignedSaturatedDoublingMultiplyLong(esize, ir.GetVector(ToVector(false, Vn, N)), reg_m);
    const auto reg_d = ir.GetVector(d);
    ir.SetVector(d, op ? ir.VectorSignedSaturatedSub(esize * 2, reg_d, product) : ir.VectorSignedSaturatedAdd(esize * 2, reg_d, product));
    return true;
}

bool TranslatorVisitor::asimd_VQDMULH_scalar(]=] _asimd_source "${_asimd_source}")
    endif()
    file(WRITE "${_asimd_output}/dynarmic/${_asimd_relative}.in" "${_asimd_source}")
    configure_file("${_asimd_output}/dynarmic/${_asimd_relative}.in" "${_asimd_output}/dynarmic/${_asimd_relative}" COPYONLY)
    if(_asimd_relative MATCHES "\\.cpp$")
        get_target_property(_asimd_sources dynarmic SOURCES)
        set(_asimd_found FALSE)
        foreach(_asimd_item IN LISTS _asimd_sources)
            if(_asimd_item MATCHES "${_asimd_relative}$")
                list(REMOVE_ITEM _asimd_sources "${_asimd_item}")
                set(_asimd_found TRUE)
            endif()
        endforeach()
        if(NOT _asimd_found)
            message(FATAL_ERROR "Dynarmic ASIMD translation source missing: ${_asimd_relative}")
        endif()
        list(APPEND _asimd_sources "${_asimd_output}/dynarmic/${_asimd_relative}")
        set_property(TARGET dynarmic PROPERTY SOURCES "${_asimd_sources}")
    endif()
endforeach()
target_include_directories(dynarmic BEFORE PRIVATE "${_asimd_output}")

# ---- ARM64 read page table ----
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
# Checked ARM64 adaptation: keep the pinned vendor tree unchanged. A separate
# table permits data loads from readable pages without granting write access.
set(_rp_root "${PROJECT_SOURCE_DIR}/third_party/dynarmic/src")
set(_rp_output "${CMAKE_BINARY_DIR}/generated/dynarmic-read-pages")
file(MAKE_DIRECTORY "${_rp_output}")

function(_rp_adapt relative expected)
    set(_input "${_rp_root}/${relative}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_input}")
    file(READ "${_input}" _source)
    string(REPLACE "\r\n" "\n" _source "${_source}")
    string(SHA256 _hash "${_source}")
    if(NOT _hash STREQUAL expected)
        message(FATAL_ERROR "Dynarmic ${relative} changed; review the read page table adaptation")
    endif()
    if(relative STREQUAL "dynarmic/interface/A32/config.h")
        string(REPLACE "#include <array>" "#include <array>\n#include <atomic>" _source "${_source}")
        string(REPLACE
            "    /// Determines if the pointer in the page_table shall be offseted locally or globally."
            "    // OGPlay ARM64 extension: data reads only; writes still use page_table.\n    std::array<std::uint8_t*, NUM_PAGE_TABLE_ENTRIES>* read_page_table = nullptr;\n    /// Determines if the pointer in the page_table shall be offseted locally or globally."
            _source "${_source}")
        string(REPLACE "    bool absolute_offset_page_table = false;"
            "    bool absolute_offset_page_table = false;\n    // Conservative live processor-ID bound, published before first Run.\n    const std::atomic<size_t>* exclusive_processor_bound = nullptr;\n    bool inline_exclusive_memory = false;"
            _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/emit_arm64.h")
        string(REPLACE "    u64 page_table_pointer;" "    u64 page_table_pointer;\n    bool preserve_memory_fpsr{};\n    u64 read_page_table_pointer{};\n    u64 write_page_table_pointer{};\n    bool inline_exclusive_memory{};\n    u64 exclusive_lock_pointer{};\n    u64 exclusive_address_pointer{};\n    u64 exclusive_addresses_pointer{};\n    u64 exclusive_value_pointer{};\n    u64 exclusive_epoch_pointer{};\n    size_t exclusive_reservation_stride{};\n    u64 exclusive_processor_bound_pointer{};\n    size_t exclusive_processor_count{};" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/a32_address_space.cpp")
        string(REPLACE "#include \"dynarmic/backend/arm64/a32_address_space.h\""
            "#include \"dynarmic/backend/arm64/a32_address_space.h\"\n#include <iterator>\n#include \"dynarmic/frontend/A32/a32_ir_emitter.h\"\n#include \"dynarmic/ir/opcodes.h\""
            _source "${_source}")
        string(REPLACE "namespace Dynarmic::Backend::Arm64 {"
            "namespace Dynarmic::Backend::Arm64 {\n#include \"${PROJECT_SOURCE_DIR}/src/cpu/dynarmic_arm64_fp_flags.inc\""
            _source "${_source}")
        string(REPLACE "        Optimization::A32GetSetElimination(ir_block, {.convert_nzc_to_nz = true});"
            "        Optimization::A32GetSetElimination(ir_block, {.convert_nzc_to_nz = true});\n        ForwardAdjacentFpscrNZCV(ir_block);"
            _source "${_source}")
        # Wrapped fallback memory calls preserve live guest FPSR. Normal
        # page-table loads/stores need not spill it on their fast path.
        string(REPLACE "ABI_PushRegisters(code, save_regs, 0);"
            "ABI_PushRegisters(code, save_regs, 16);\n    code.MRS(X0, oaknut::SystemReg::FPSR);\n    code.STR(W0, SP, 0);"
            _source "${_source}")
        string(REPLACE "ABI_PopRegisters(code, save_regs, 0);"
            "code.LDR(Wscratch1, SP, 0);\n    code.MSR(oaknut::SystemReg::FPSR, Xscratch1);\n    ABI_PopRegisters(code, save_regs, 16);"
            _source "${_source}")
        string(REPLACE "#include \"dynarmic/interface/exclusive_monitor.h\""
            "#include \"dynarmic/interface/exclusive_monitor.h\"\n#include \"${PROJECT_SOURCE_DIR}/src/cpu/dynarmic_exclusive_monitor_access.h\""
            _source "${_source}")
        string(REPLACE "        .page_table_pointer = mcl::bit_cast<u64>(conf.page_table),"
            "        .page_table_pointer = mcl::bit_cast<u64>(conf.read_page_table ? conf.read_page_table : conf.page_table),\n        .preserve_memory_fpsr = true,\n        .read_page_table_pointer = mcl::bit_cast<u64>(conf.read_page_table),\n        .write_page_table_pointer = mcl::bit_cast<u64>(conf.page_table),\n        .inline_exclusive_memory = conf.inline_exclusive_memory,\n        .exclusive_lock_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorLockPointer(conf.global_monitor)) : 0,\n        .exclusive_address_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorAddressPointer(conf.global_monitor, conf.processor_id)) : 0,\n        .exclusive_addresses_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorAddressPointer(conf.global_monitor, 0)) : 0,\n        .exclusive_value_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorValuePointer(conf.global_monitor, conf.processor_id)) : 0,\n        .exclusive_epoch_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorEpochPointer(conf.global_monitor)) : 0,\n        .exclusive_reservation_stride = conf.global_monitor ? GetExclusiveMonitorReservationStride(conf.global_monitor) : 0,\n        .exclusive_processor_bound_pointer = mcl::bit_cast<u64>(conf.exclusive_processor_bound),\n        .exclusive_processor_count = conf.global_monitor ? GetExclusiveMonitorProcessorCount(conf.global_monitor) : 0,"
            _source "${_source}")
        # Flush only the emitted prelude. Darwin sys_icache_invalidate and the
        # generic dc/ic loop touch every page, committing the whole code cache
        # per JIT; each emitted block already flushes its own range.
        string(REPLACE "    prelude_info.end_of_prelude = code.offset();\n\n    mem.invalidate_all();"
            "    prelude_info.end_of_prelude = code.offset();\n\n    mem.invalidate(mem.ptr(), static_cast<size_t>(prelude_info.end_of_prelude));"
            _source "${_source}")
        if(NOT _source MATCHES "mem\\.invalidate\\(mem\\.ptr\\(\\), static_cast<size_t>\\(prelude_info\\.end_of_prelude\\)\\);")
            message(FATAL_ERROR "Dynarmic A32 prelude icache flush adaptation did not apply")
        endif()
        # Keep the more frequent load table in the dedicated backend register.
        string(REPLACE "if (conf.page_table) {\n            code.MOV(Xpagetable, mcl::bit_cast<u64>(conf.page_table));"
            "if (conf.read_page_table || conf.page_table) {\n            code.MOV(Xpagetable, mcl::bit_cast<u64>(conf.read_page_table ? conf.read_page_table : conf.page_table));"
            _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/reg_alloc.h")
        string(REPLACE "arg3 = {});" "arg3 = {}, bool preserve_fpsr = false);" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/reg_alloc.cpp")
        string(REPLACE "std::optional<Argument::copyable_reference> arg3) {"
            "std::optional<Argument::copyable_reference> arg3, bool preserve_fpsr) {" _source "${_source}")
        string(REPLACE "    fpsr_manager.Spill();\n    SpillFlags();"
            "    if (preserve_fpsr) fpsr_manager.Load();\n    else fpsr_manager.Spill();\n    SpillFlags();" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/emit_arm64_memory.cpp")
        # Only page-table emitters use this exact lookup sequence. Fastmem and
        # ordinary ABI callbacks retain their existing FPSR spill boundaries.
        string(REPLACE
            "    ctx.fpsr.Spill();\n    ctx.reg_alloc.SpillFlags();\n    RegAlloc::Realize(Xaddr, Rvalue);\n\n    SharedLabel fallback = GenSharedLabel(), end = GenSharedLabel();\n\n    const auto [Xbase, Xoffset] = InlinePageTableEmitVAddrLookup"
            "    if (!ctx.conf.preserve_memory_fpsr) ctx.fpsr.Spill();\n    ctx.reg_alloc.SpillFlags();\n    RegAlloc::Realize(Xaddr, Rvalue);\n\n    SharedLabel fallback = GenSharedLabel(), end = GenSharedLabel();\n\n    const auto [Xbase, Xoffset] = InlinePageTableEmitVAddrLookup"
            _source "${_source}")
        string(REPLACE "#include <utility>"
            "#include <utility>\n#include \"dynarmic/common/spin_lock_arm64.h\""
            _source "${_source}")
        string(REPLACE
            "InlinePageTableEmitVAddrLookup(oaknut::CodeGenerator& code, EmitContext& ctx, oaknut::XReg Xaddr, const SharedLabel& fallback)"
            "InlinePageTableEmitVAddrLookup(oaknut::CodeGenerator& code, EmitContext& ctx, oaknut::XReg Xaddr, const SharedLabel& fallback, bool read = false)"
            _source "${_source}")
        string(REPLACE "}  // namespace\n\ntemplate<size_t bitsize>\nvoid EmitReadMemory"
            "#include \"${PROJECT_SOURCE_DIR}/src/cpu/dynarmic_arm64_exclusive.inc\"\n\n}  // namespace\n\ntemplate<size_t bitsize>\nvoid EmitReadMemory"
            _source "${_source}")
        string(REPLACE "    CallbackOnlyEmitExclusiveReadMemory<bitsize>(code, ctx, inst);"
            "    if constexpr (bitsize <= 64) {\n        if (ctx.conf.inline_exclusive_memory && ctx.conf.exclusive_lock_pointer && ctx.conf.page_table_pointer) {\n            InlineExclusiveReadMemory<bitsize>(code, ctx, inst);\n            return;\n        }\n    }\n    CallbackOnlyEmitExclusiveReadMemory<bitsize>(code, ctx, inst);"
            _source "${_source}")
        string(REPLACE "    CallbackOnlyEmitExclusiveWriteMemory<bitsize>(code, ctx, inst);"
            "    if constexpr (bitsize <= 64) {\n        if (ctx.conf.inline_exclusive_memory && ctx.conf.exclusive_lock_pointer && ctx.conf.write_page_table_pointer) {\n            InlineExclusiveWriteMemory<bitsize>(code, ctx, inst);\n            return;\n        }\n    }\n    CallbackOnlyEmitExclusiveWriteMemory<bitsize>(code, ctx, inst);"
            _source "${_source}")
        string(REPLACE "    code.LDR(Xscratch0, Xpagetable, Xscratch0, LSL, 3);"
            "    if (!read && ctx.conf.read_page_table_pointer != 0) {\n        if (ctx.conf.write_page_table_pointer == 0) {\n            code.B(*fallback);\n        } else {\n            code.MOV(Xscratch1, ctx.conf.write_page_table_pointer);\n            code.LDR(Xscratch0, Xscratch1, Xscratch0, LSL, 3);\n        }\n    } else {\n        code.LDR(Xscratch0, Xpagetable, Xscratch0, LSL, 3);\n    }"
            _source "${_source}")
        string(REPLACE
            "InlinePageTableEmitVAddrLookup<bitsize>(code, ctx, Xaddr, fallback);\n    EmitMemoryLdr"
            "InlinePageTableEmitVAddrLookup<bitsize>(code, ctx, Xaddr, fallback, true);\n    EmitMemoryLdr"
            _source "${_source}")
        string(REPLACE
            "} else if (ctx.conf.page_table_pointer != 0) {\n        InlinePageTableEmitReadMemory"
            "} else if (ctx.conf.page_table_pointer != 0 || ctx.conf.read_page_table_pointer != 0) {\n        InlinePageTableEmitReadMemory"
            _source "${_source}")
    endif()
    _rp_retain_fpsr("${relative}")
    _rp_exclusive_epoch("${relative}")
    get_filename_component(_parent "${_rp_output}/${relative}" DIRECTORY)
    file(MAKE_DIRECTORY "${_parent}")
    file(WRITE "${_rp_output}/${relative}.in" "${_source}")
    configure_file("${_rp_output}/${relative}.in" "${_rp_output}/${relative}" COPYONLY)
    if(relative MATCHES "\\.cpp$")
        get_target_property(_sources dynarmic SOURCES)
        string(REGEX REPLACE "^dynarmic/" "" _suffix "${relative}")
        set(_found FALSE)
        foreach(_item IN LISTS _sources)
            if(_item MATCHES "${_suffix}$")
                list(REMOVE_ITEM _sources "${_item}")
                set(_found TRUE)
            endif()
        endforeach()
        if(NOT _found)
            message(FATAL_ERROR "Dynarmic source missing: ${relative}")
        endif()
        list(APPEND _sources "${_rp_output}/${relative}")
        set_property(TARGET dynarmic PROPERTY SOURCES "${_sources}")
    endif()
endfunction()

_rp_adapt(dynarmic/interface/A32/config.h a3b54e06be8f22f13271d47fb5e49d1573f6236f08080d18dbdcb1f0ea01c658)
_rp_adapt(dynarmic/backend/arm64/emit_arm64.h 8eb97a435794853634320229bd4909834570a8d5ab8892810992a6a760ab28c4)
_rp_adapt(dynarmic/backend/arm64/a32_address_space.cpp 69a0ee103e8c366cff937482514b4c6284a2569b62088616af6021c3fb8f2668)
_rp_adapt(dynarmic/backend/arm64/emit_arm64_memory.cpp 6bd36b28a27a9b071fe0dd7709276caa059a471ff3641df933fbbcf6db4155b6)
_rp_adapt(dynarmic/backend/arm64/fpsr_manager.h b2107a6103de0c9e41ce5a5edefadbe35a9ebd9ad6064c268ed8d836739e94cb)
_rp_adapt(dynarmic/backend/arm64/fpsr_manager.cpp 85c7823b89d8c25c6d8be056a007889c0b8fb75f74f9ce7707079930fa4e72e8)
_rp_adapt(dynarmic/backend/arm64/stack_layout.h ad78f0c35c97338a3f5ce0eab616591793c286a490d0a9ed48a2c96dd6a64028)
_rp_adapt(dynarmic/backend/arm64/emit_arm64.cpp b05a6153720eb4ab7f5a4866adea798fe969de88899769d8b1df9ff3cfa12512)
_rp_adapt(dynarmic/backend/arm64/reg_alloc.h f9347eea344317388e1d82628340f81886e7b886014a36fb31550275edb31fbb)
_rp_adapt(dynarmic/backend/arm64/reg_alloc.cpp b89195288f57aea49e550f88c349915c37ca31e3bca452c67afee369f4c18a36)
_rp_adapt(dynarmic/interface/exclusive_monitor.h 18d8e7befc18631f5b3c5d0294d7cffe8185bc1e3695d9976f055f1c2fcd45e1)
_rp_adapt(dynarmic/backend/arm64/exclusive_monitor.cpp 613800d6632b747fac614f3b6b9f4cd49cd27279fff214cd74e404e8a26be125)
# Public so OGPlay and every Dynarmic translation unit see the same config ABI.
target_include_directories(dynarmic BEFORE PUBLIC "$<BUILD_INTERFACE:${_rp_output}>")
# A newly shadowed vendor header is absent from existing dependency files.
# Bump the ABI marker when adding such a header so incremental builds rebuild
# both the backend and its consumers rather than mixing object layouts.
target_compile_definitions(dynarmic PUBLIC OGPLAY_DYNARMIC_READ_PAGES_ABI=9)
endif()

# ---- Windows x64 cache statistics ----
if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
# Keep the pinned submodule clean. Generate a checked, x64-only diagnostic extension.
set(_dyn_src "${PROJECT_SOURCE_DIR}/third_party/dynarmic/src")
set(_dyn_diag "${CMAKE_BINARY_DIR}/generated/dynarmic-diagnostics")
file(READ "${_dyn_src}/dynarmic/interface/A32/a32.h" _dyn_header)
file(READ "${_dyn_src}/dynarmic/backend/x64/a32_interface.cpp" _dyn_impl)
foreach(_needle IN ITEMS "    void ClearCache();" "    struct Impl;")
    string(FIND "${_dyn_header}" "${_needle}" _found)
    if(_found LESS 0)
        message(FATAL_ERROR "Dynarmic diagnostic header no longer matches pinned source")
    endif()
endforeach()
string(FIND "${_dyn_impl}" "                block_of_code.ClearCache();" _found)
if(_found LESS 0)
    message(FATAL_ERROR "Dynarmic cache invalidation changed; review diagnostic extension")
endif()
string(REPLACE "    void ClearCache();" "    void ClearCache();\n    std::array<std::uint64_t, 3> CacheStatistics() const;" _dyn_header "${_dyn_header}")
string(REPLACE "struct Jit::Impl {" "struct Jit::Impl {\n    friend class Jit;" _dyn_impl "${_dyn_impl}")
string(REPLACE "    A32JitState jit_state;" "    std::uint64_t cache_flushes{};\n    A32JitState jit_state;" _dyn_impl "${_dyn_impl}")
string(REPLACE "                block_of_code.ClearCache();" "                ++cache_flushes;\n                block_of_code.ClearCache();" _dyn_impl "${_dyn_impl}")
string(APPEND _dyn_impl "\nnamespace Dynarmic::A32 {\nstd::array<std::uint64_t, 3> Jit::CacheStatistics() const {\n return {impl->block_of_code.GetTotalCodeSize(), impl->block_of_code.GetTotalCodeSize() - impl->block_of_code.SpaceRemaining(), impl->cache_flushes};\n}\n}\n")
file(MAKE_DIRECTORY "${_dyn_diag}/dynarmic/interface/A32")
file(WRITE "${_dyn_diag}/header.in" "${_dyn_header}")
configure_file("${_dyn_diag}/header.in" "${_dyn_diag}/dynarmic/interface/A32/a32.h" COPYONLY)
file(WRITE "${_dyn_diag}/source.in" "${_dyn_impl}")
configure_file("${_dyn_diag}/source.in" "${_dyn_diag}/a32_interface.cpp" COPYONLY)
get_target_property(_dyn_sources dynarmic SOURCES)
set(_dyn_replaced FALSE)
foreach(_source IN LISTS _dyn_sources)
    if(_source MATCHES "backend/x64/a32_interface.cpp$")
        list(REMOVE_ITEM _dyn_sources "${_source}")
        set(_dyn_replaced TRUE)
    endif()
endforeach()
if(NOT _dyn_replaced)
    message(FATAL_ERROR "Dynarmic x64 source missing for diagnostic extension")
endif()
list(APPEND _dyn_sources "${_dyn_diag}/a32_interface.cpp")
set_property(TARGET dynarmic PROPERTY SOURCES "${_dyn_sources}")
target_include_directories(dynarmic BEFORE PUBLIC "$<BUILD_INTERFACE:${_dyn_diag}>")
target_compile_definitions(dynarmic PUBLIC OGPLAY_DYNARMIC_CACHE_STATS=1)
endif()
