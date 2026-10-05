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
        string(REPLACE
            "    /// Determines if the pointer in the page_table shall be offseted locally or globally."
            "    // OGPlay ARM64 extension: data reads only; writes still use page_table.\n    std::array<std::uint8_t*, NUM_PAGE_TABLE_ENTRIES>* read_page_table = nullptr;\n    /// Determines if the pointer in the page_table shall be offseted locally or globally."
            _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/emit_arm64.h")
        string(REPLACE "    u64 page_table_pointer;" "    u64 page_table_pointer;\n    u64 read_page_table_pointer{};\n    u64 write_page_table_pointer{};" _source "${_source}")
    elseif(relative STREQUAL "dynarmic/backend/arm64/a32_address_space.cpp")
        string(REPLACE "        .page_table_pointer = mcl::bit_cast<u64>(conf.page_table),"
            "        .page_table_pointer = mcl::bit_cast<u64>(conf.read_page_table ? conf.read_page_table : conf.page_table),\n        .read_page_table_pointer = mcl::bit_cast<u64>(conf.read_page_table),\n        .write_page_table_pointer = mcl::bit_cast<u64>(conf.page_table),"
            _source "${_source}")
        # Keep the more frequent load table in the dedicated backend register.
        string(REPLACE "if (conf.page_table) {\n            code.MOV(Xpagetable, mcl::bit_cast<u64>(conf.page_table));"
            "if (conf.read_page_table || conf.page_table) {\n            code.MOV(Xpagetable, mcl::bit_cast<u64>(conf.read_page_table ? conf.read_page_table : conf.page_table));"
            _source "${_source}")
    else()
        string(REPLACE
            "InlinePageTableEmitVAddrLookup(oaknut::CodeGenerator& code, EmitContext& ctx, oaknut::XReg Xaddr, const SharedLabel& fallback)"
            "InlinePageTableEmitVAddrLookup(oaknut::CodeGenerator& code, EmitContext& ctx, oaknut::XReg Xaddr, const SharedLabel& fallback, bool read = false)"
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
# Public so OGPlay and every Dynarmic translation unit see the same config ABI.
target_include_directories(dynarmic BEFORE PUBLIC "$<BUILD_INTERFACE:${_rp_output}>")
# A newly shadowed vendor header is absent from existing dependency files.
# Bump the ABI marker when adding such a header so incremental builds rebuild
# both the backend and its consumers rather than mixing object layouts.
target_compile_definitions(dynarmic PUBLIC OGPLAY_DYNARMIC_READ_PAGES_ABI=3)
