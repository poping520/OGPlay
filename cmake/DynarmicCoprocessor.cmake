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
