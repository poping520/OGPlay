// Session exit semantics: System.exit() ends the process, Activity.finish()
// only retires one activity. An installer shell that calls startActivity and
// then finishes itself is a handoff, and with real host Java threads the main
// loop can observe that finish() before the switch is serviced. Treating it
// as an exit shut Dungeon Hunter down at the handoff, so each case below
// fails if the predicate goes back to reading one session-wide flag.

#include <doctest/doctest.h>

#include "ogplay/runtime/integration/dexvm_android.h"

namespace {

using ogplay::runtime::DexVmAndroidContext;
using ogplay::runtime::SessionExitRequested;
using ogplay::runtime::dexvm::VmObjectRef;

constexpr VmObjectRef kInstaller{0x11};
constexpr VmObjectRef kGame{0x22};

}  // namespace

TEST_CASE("System.exit ends the session and a bare activity does not") {
    DexVmAndroidContext context;
    context.activity = kInstaller;
    CHECK_FALSE(SessionExitRequested(context));
    context.exit_requested = true;
    CHECK(SessionExitRequested(context));
}

TEST_CASE("finish() during an activity handoff does not end the session") {
    DexVmAndroidContext context;
    context.activity = kInstaller;

    // startActivity() raises the handoff, then the shell finishes itself.
    context.activity_switch_pending = true;
    context.finishing_activity = kInstaller.Value();
    CHECK_FALSE(SessionExitRequested(context));

    // The lifecycle services the switch: the departing request is answered by
    // its retirement, and the arriving activity owns the session.
    context.activity_switch_pending = false;
    context.finishing_activity = 0U;
    context.activity = kGame;
    CHECK_FALSE(SessionExitRequested(context));

    // A late finish() repeated by the retired shell's own host thread lands on
    // a handle nothing owns any more.
    context.finishing_activity = kInstaller.Value();
    CHECK_FALSE(SessionExitRequested(context));

    // The activity that is actually on screen finishing itself does end it.
    context.finishing_activity = kGame.Value();
    CHECK(SessionExitRequested(context));
}
