#pragma once

#include "catalog.h"

namespace ogplay::runtime {
struct RomActivityState {
    struct Record {
        dexvm::VmObjectRef token{}, intent{}, info{}, component{}, caller{}, result_data{};
        std::string descriptor, component_name;
        int task{1}, request{-1}, result{}, orientation{-1};
        unsigned mode{};
        bool resumed{}, paused{}, stopped{}, idle{}, finishing{}, destroying{};
        bool launching{true}, pausing{};
        bool visible{true};
        bool stopping{};
        bool suspend_stop_required{};
    };

    dexvm::VmObjectRef application_thread{};
    std::vector<Record> stack;
    std::deque<DexVmAndroidContext::ActivityCommand> pending;
    int next_task{2};
    std::set<int> background_tasks;
    bool suspended{}, exiting{}, exit_requested{};
    std::int64_t exit_deadline_millis{};
};

namespace android_intrinsics {
void StartRomLauncher(dx::Interpreter& vm, const Context& context, const std::string& descriptor,
                      const std::string& component);
void RequestRomActivitySuspend(dx::Interpreter& vm, const Context& context, bool suspend);
void RequestRomActivityExit(dx::Interpreter& vm, const Context& context, std::int64_t budget_millis = 2000);
enum class RomActivityExitStatus { waiting, complete, deadline_expired };
RomActivityExitStatus ReadRomActivityExitStatus(const Context& context);
void VisitRomActivityRoots(const DexVmAndroidContext& context, const dx::VmRootVisitor& visit);
}  // namespace android_intrinsics
}  // namespace ogplay::runtime
