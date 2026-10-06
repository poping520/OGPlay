# Checked ARM64 adaptation: keep the pinned vendor tree unchanged. A separate
# table permits data loads from readable pages without granting write access.
set(_rp_root "${PROJECT_SOURCE_DIR}/third_party/dynarmic/src")
set(_rp_output "${CMAKE_BINARY_DIR}/generated/dynarmic-read-pages")
file(MAKE_DIRECTORY "${_rp_output}")
include("${CMAKE_CURRENT_LIST_DIR}/DynarmicPersistentFpsr.cmake")


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
        string(REPLACE "    u64 page_table_pointer;" "    u64 page_table_pointer;\n    bool preserve_memory_fpsr{};\n    u64 read_page_table_pointer{};\n    u64 write_page_table_pointer{};\n    bool inline_exclusive_memory{};\n    u64 exclusive_lock_pointer{};\n    u64 exclusive_address_pointer{};\n    u64 exclusive_addresses_pointer{};\n    u64 exclusive_value_pointer{};\n    u64 exclusive_processor_bound_pointer{};\n    size_t exclusive_processor_count{};" _source "${_source}")
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
            "        .page_table_pointer = mcl::bit_cast<u64>(conf.read_page_table ? conf.read_page_table : conf.page_table),\n        .preserve_memory_fpsr = true,\n        .read_page_table_pointer = mcl::bit_cast<u64>(conf.read_page_table),\n        .write_page_table_pointer = mcl::bit_cast<u64>(conf.page_table),\n        .inline_exclusive_memory = conf.inline_exclusive_memory,\n        .exclusive_lock_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorLockPointer(conf.global_monitor)) : 0,\n        .exclusive_address_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorAddressPointer(conf.global_monitor, conf.processor_id)) : 0,\n        .exclusive_addresses_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorAddressPointer(conf.global_monitor, 0)) : 0,\n        .exclusive_value_pointer = conf.global_monitor ? mcl::bit_cast<u64>(GetExclusiveMonitorValuePointer(conf.global_monitor, conf.processor_id)) : 0,\n        .exclusive_processor_bound_pointer = mcl::bit_cast<u64>(conf.exclusive_processor_bound),\n        .exclusive_processor_count = conf.global_monitor ? GetExclusiveMonitorProcessorCount(conf.global_monitor) : 0,"
            _source "${_source}")
        # Keep the more frequent load table in the dedicated backend register.
        string(REPLACE "if (conf.page_table) {\n            code.MOV(Xpagetable, mcl::bit_cast<u64>(conf.page_table));"
            "if (conf.read_page_table || conf.page_table) {\n            code.MOV(Xpagetable, mcl::bit_cast<u64>(conf.read_page_table ? conf.read_page_table : conf.page_table));"
            _source "${_source}")
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
# Public so OGPlay and every Dynarmic translation unit see the same config ABI.
target_include_directories(dynarmic BEFORE PUBLIC "$<BUILD_INTERFACE:${_rp_output}>")
# A newly shadowed vendor header is absent from existing dependency files.
# Bump the ABI marker when adding such a header so incremental builds rebuild
# both the backend and its consumers rather than mixing object layouts.
target_compile_definitions(dynarmic PUBLIC OGPLAY_DYNARMIC_READ_PAGES_ABI=5)
