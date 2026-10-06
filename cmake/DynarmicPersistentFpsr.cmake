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
