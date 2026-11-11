#include "dexvm/resource_fixture.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "boot_dex.h"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"
#include "ogplay/runtime/vfs/vfs.h"
#include "../fixtures/bitmap_decode_data.h"

#include "android_value_fixture.h"

using namespace ogplay::test::android_value;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("DVM-121 ColorStateList uses Java StateSet matching in ROM TextView") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        AndroidValueVm f(backend);
        const auto int_array = [&](std::initializer_list<std::int32_t> items) {
            const auto result = f.model.NewPrimitiveArray(f.linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer,
                                                          static_cast<JniSize>(items.size()));
            JniSize i{};
            for (const auto item : items)
                f.model.SetPrimitiveElement(result, i++, static_cast<std::uint32_t>(item));
            return result;
        };
        const auto states =
            f.model.NewObjectArray(f.linker.ResolveDescriptor("[[I"), f.linker.ResolveDescriptor("[I"), 2);
        const auto state_root = f.vm.ProtectReferences(std::array{states});
        f.model.SetObjectElement(states, 0, int_array({-16842910}));
        f.model.SetObjectElement(states, 1, int_array({}));
        const auto colors = int_array({static_cast<std::int32_t>(0xff808080U), static_cast<std::int32_t>(0xffffffffU)});
        const auto color_root = f.vm.ProtectReferences(std::array{colors});
        const auto list =
            f.New("Landroid/content/res/ColorStateList;", "([[I[I)V", {VmValue::Ref(states), VmValue::Ref(colors)});
        const auto root = f.vm.ProtectReferences(std::array{list});
        CHECK(f.On(list, "isStateful", "()Z").AsInt() == 1);
        CHECK(static_cast<std::uint32_t>(f.On(list, "getDefaultColor", "()I").AsInt()) == 0xffffffffU);
        CHECK(static_cast<std::uint32_t>(
                  f.On(list, "getColorForState", "([II)I", {VmValue::Ref(int_array({})), VmValue::Int(0)}).AsInt()) ==
              0xff808080U);
        CHECK(static_cast<std::uint32_t>(
                  f.On(list, "getColorForState", "([II)I", {VmValue::Ref(int_array({16842910})), VmValue::Int(0)})
                      .AsInt()) == 0xffffffffU);
        const auto activity = f.New("Landroid/app/Activity;");
        const auto text = f.New("Landroid/widget/TextView;", "(Landroid/content/Context;)V", {VmValue::Ref(activity)});
        f.On(text, "setTextColor", "(Landroid/content/res/ColorStateList;)V", {VmValue::Ref(list)});
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentTextColor", "()I").AsInt()) == 0xffffffffU);
        f.On(text, "setEnabled", "(Z)V", {VmValue::Int(0)});
        CHECK(static_cast<std::uint32_t>(f.On(text, "getCurrentTextColor", "()I").AsInt()) == 0xff808080U);
    }
}
