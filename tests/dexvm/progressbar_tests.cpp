#include "android_value_fixture.h"
#include "container_traversal_fixture.h"

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
using ogplay::test::container_traversal::ContainerVm;

TEST_CASE("DVM-365 ROM drawable XML rejects a null parser") {
    for (const auto backend : {InterpreterBackend::switch_dispatch, InterpreterBackend::threaded}) {
        ContainerVm f(backend);
        const auto owner = f.New("Landroid/app/Activity;");
        const auto view = f.New("Landroid/widget/ProgressBar;", "(Landroid/content/Context;)V", {VmValue::Ref(owner)});
        const auto roots = f.vm.ProtectReferences(std::array{owner, view});
        const auto xml = f.linker.FindDirectMethod(
            f.linker.ResolveDescriptor("Landroid/graphics/drawable/Drawable;"), "createFromXmlInner",
            "(Landroid/content/res/Resources;Lorg/xmlpull/v1/XmlPullParser;Landroid/util/AttributeSet;)Landroid/graphics/drawable/Drawable;");
        REQUIRE(xml);
        const auto nested = f.vm.Call(
            *xml, std::array{VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{}), VmValue::Ref(VmObjectRef{})});
        REQUIRE(nested.exception.IsValid());
        CHECK(f.linker.Class(nested.exception_class).descriptor == "Ljava/lang/NullPointerException;");
    }
}
