#pragma once

#include "catalog.h"

namespace ogplay::runtime {
struct RomActivityState {
    struct Record {
        dexvm::VmObjectRef token{}, intent{}, info{}, component{}, caller{}, result_data{}, configuration{};
        std::string descriptor, component_name;
        int task{1}, request{-1}, result{}, orientation{-1};
        unsigned mode{};
        bool resumed{}, paused{}, stopped{}, idle{}, finishing{}, destroying{};
        bool launching{true}, pausing{};
        bool visible{true}, immersive{};
        bool stopping{};
        bool suspend_stop_required{};
    };

    dexvm::VmObjectRef application_thread{}, configuration{};
    int configuration_sequence{};
    std::vector<Record> stack;
    std::deque<DexVmAndroidContext::ActivityCommand> pending;
    int next_task{2};
    std::set<int> background_tasks;
    bool suspended{}, host_closing{}, exit_requested{};
};

namespace android_intrinsics {
void StartRomLauncher(dx::Interpreter& vm, const Context& context, const std::string& descriptor,
                      const std::string& component);
void RequestRomActivitySuspend(dx::Interpreter& vm, const Context& context, bool suspend);
void VisitRomActivityRoots(const DexVmAndroidContext& context, const dx::VmRootVisitor& visit);
}  // namespace android_intrinsics
}  // namespace ogplay::runtime
