#include "boot_dex.h"
#include <doctest/doctest.h>
#include <array>
#include <fstream>
#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace {
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;
std::vector<std::uint8_t> Bytes(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("missing test data");
    return {std::istreambuf_iterator<char>(f),{}};
}
std::vector<std::uint8_t> Surface() {
    const std::array<std::pair<std::string,std::string>,2> records{{
        {"Landroid/shape/Base;",R"(["Ljava/lang/Object;",[],1,[["<init>","()V",1],["answer","()I",9],["missing","()I",9],["toString","()Ljava/lang/String;",1]],[["VALUE","I",25,1,"123"],["TEXT","Ljava/lang/String;",25,2,"\u4e2d\ud83d\ude00"],["state","I",9,0,null],["value","I",1,0,null]]])"},
        {"Landroid/shape/Callback;",R"(["Ljava/lang/Object;",[],1537,[["apply","()I",1025]],[]])"}}};
    std::vector<std::uint8_t> b{'O','G','S','K','1','9',1,'\n'};
    const auto n=[&](std::uint32_t v,unsigned width){for(unsigned i=0;i<width;++i)b.push_back(static_cast<std::uint8_t>(v>>(i*8U)));};
    n(records.size(),4);std::uint32_t offset=0;
    for(const auto& [name,body]:records){n(static_cast<std::uint32_t>(name.size()),2);b.insert(b.end(),name.begin(),name.end());n(offset,4);n(static_cast<std::uint32_t>(body.size()),4);offset+=static_cast<std::uint32_t>(body.size());}
    for(const auto& [name,body]:records){(void)name;b.insert(b.end(),body.begin(),body.end());}return b;
}
struct Vm {
    JniStringStore strings;JniPrimitiveArrayStore arrays;JavaObjectModel model{strings,arrays};
    DexClassLinker linker;ogplay::core::CapabilityLedger ledger;std::unique_ptr<Interpreter> vm;
    Vm(InterpreterBackend backend,bool skeleton=true,bool handwritten=false) {
        linker.RegisterIntrinsics(CoreIntrinsicCatalog());
        IntrinsicClassDecl final_host;final_host.descriptor="Lshape/FinalHost;";final_host.superclass="Ljava/lang/Object;";
        IntrinsicMethodDecl final_method;final_method.name="toString";final_method.descriptor="()Ljava/lang/String;";
        final_method.access_flags=0x11;final_method.must_override=true;final_method.invoke_kind=DeclaredInvokeKind::virtual_call;
        final_host.methods.push_back(final_method);linker.RegisterIntrinsics(std::array{final_host});
        linker.RegisterIntrinsics(AndroidIntrinsicCatalog(std::make_shared<DexVmAndroidContext>()));
        ogplay::test::RegisterBootDex(linker);
        if(handwritten){
            auto builder=IntrinsicClassBuilder::Class("Landroid/shape/Base;");
            builder.StaticMethod("answer","()I",[](IntrinsicContext&){return VmValue::Int(77);});
            auto decl=std::move(builder).Build();
            IntrinsicFieldDecl field;field.name="VALUE";field.descriptor="I";field.is_static=true;field.has_constant=true;field.integral=88;
            decl.fields.push_back(field);
            linker.RegisterIntrinsics(std::array{decl});
        }
        if(skeleton)linker.RegisterSkeletonSurface(Surface());
        linker.RegisterDex(Bytes(std::filesystem::path(OGPLAY_DEXVM_FIXTURE_DIR)/"skeleton.dex"));linker.Link();
        vm=std::make_unique<Interpreter>(linker,model,nullptr,ledger,InterpreterConfig{.backend=backend});
    }
    VmCallOutcome Call(const std::string& name,const std::string& desc="()I",std::vector<VmValue> args={}) {
        auto owner=linker.ResolveDescriptor("Lshape/Calls;");auto method=linker.FindDirectMethod(owner,name,desc);REQUIRE(method);return vm->Call(*method,args);
    }
};
}
TEST_CASE("API19 skeleton lazy identity hierarchy and own members") {
    Vm f(InterpreterBackend::switch_dispatch);
    CHECK_FALSE(f.linker.FindClass("Landroid/shape/Base;"));
    const auto before=f.linker.ClassCount();auto base=f.linker.ResolveDescriptor("Landroid/shape/Base;");
    CHECK(f.linker.ClassCount()==before+1);CHECK(f.linker.ResolveDescriptor("Landroid/shape/Base;")==base);
    const auto child=f.linker.ResolveDescriptor("Lshape/Child;");auto iface=f.linker.ResolveDescriptor("Landroid/shape/Callback;");
    CHECK(f.linker.IsAssignable(base,child));CHECK(f.linker.IsAssignable(iface,child));
    auto slot=f.linker.FindVtableIndex(base,"toString","()Ljava/lang/String;");REQUIRE(slot);
    const auto override_id=f.linker.Class(base).vtable[*slot];
    CHECK(f.linker.Method(override_id).owner==base);CHECK(f.linker.Method(override_id).is_skeleton);
    CHECK_THROWS_AS(static_cast<void>(f.linker.ResolveDescriptor("Lshape/ForbiddenOverride;")),DexVmError);
    CHECK(f.linker.Class(child).own_instance_fields.empty());
    for(auto method:f.linker.MethodsOf(child))CHECK(f.linker.Method(method).name=="apply");
    CHECK(f.linker.FindFieldRecursive(child,"value","I")==f.linker.FindFieldRecursive(base,"value","I"));
    Vm off(InterpreterBackend::switch_dispatch,false);
    CHECK_THROWS_AS(static_cast<void>(off.linker.ResolveDescriptor("Lshape/Child;")),DexVmError);
    CHECK_THROWS_AS(static_cast<void>(off.linker.ResolveDescriptor("Landroid/shape/Base;")),DexVmError);
}
TEST_CASE("API19 skeleton constants and fail closed access both backends") {
    for(auto backend:{InterpreterBackend::switch_dispatch,InterpreterBackend::threaded}) {
        Vm f(backend);
        auto constant=f.Call("constant");REQUIRE_FALSE(constant.exception.IsValid());CHECK(constant.value.AsInt()==123);
        auto text=f.Call("string","()Ljava/lang/String;");REQUIRE_FALSE(text.exception.IsValid());CHECK(f.model.StringValue(text.value.ref)==u"中😀");
        CHECK(f.Call("missing").exception.IsValid());
        CHECK(f.Call("staticGet").exception.IsValid());CHECK(f.Call("staticPut","()V").exception.IsValid());
        const auto object=f.vm->NewIntrinsicInstance("Landroid/shape/Base;");
        CHECK(f.Call("instanceGet","(Landroid/shape/Base;)I",{VmValue::Ref(object)}).exception.IsValid());
        CHECK(f.Call("instancePut","(Landroid/shape/Base;)V",{VmValue::Ref(object)}).exception.IsValid());
        const auto base=f.linker.ResolveDescriptor("Landroid/shape/Base;");
        const auto ctor=f.linker.FindDirectMethod(base,"<init>","()V");REQUIRE(ctor);
        CHECK(f.vm->Call(*ctor,std::array{VmValue::Ref(object)}).exception.IsValid());
        const auto slot=f.linker.FindVtableIndex(base,"toString","()Ljava/lang/String;");REQUIRE(slot);
        CHECK(f.vm->Call(f.linker.Class(base).vtable[*slot],std::array{VmValue::Ref(object)}).exception.IsValid());
        const auto hits=f.ledger.Unimplemented();
        CHECK(std::any_of(hits.begin(),hits.end(),[](const auto& h){return h.id=="dexvm.skeleton.field.Landroid/shape/Base;->state:I"&&h.count==2;}));
        CHECK(std::any_of(hits.begin(),hits.end(),[](const auto& h){return h.id=="dexvm.skeleton.field.Landroid/shape/Base;->value:I"&&h.count==2;}));
        CHECK(std::any_of(hits.begin(),hits.end(),[](const auto& h){return h.id.find("missing()I")!=std::string::npos;}));
    }
}
TEST_CASE("API19 skeleton supplements handwritten declarations without replacing handlers") {
    Vm f(InterpreterBackend::switch_dispatch,true,true);
    auto owner=f.linker.ResolveDescriptor("Landroid/shape/Base;");auto m=f.linker.FindDirectMethod(owner,"answer","()I");REQUIRE(m);
    CHECK_FALSE(f.linker.Method(*m).is_skeleton);CHECK(f.vm->Call(*m,{}).value.AsInt()==77);
    auto c=f.Call("constant");REQUIRE_FALSE(c.exception.IsValid());CHECK(c.value.AsInt()==88);
    auto field=f.linker.FindFieldRecursive(owner,"VALUE","I");REQUIRE(field);CHECK_FALSE(f.linker.Field(*field).is_skeleton);
}
TEST_CASE("API19 skeleton real artifact remains indexed until requested") {
    DexClassLinker linker;linker.RegisterIntrinsics(CoreIntrinsicCatalog());
        linker.RegisterIntrinsics(AndroidIntrinsicCatalog(std::make_shared<DexVmAndroidContext>()));
        ogplay::test::RegisterBootDex(linker);
    auto before=linker.ClassCount();linker.RegisterSkeletonSurface(Bytes(std::filesystem::path(OGPLAY_SOURCE_DIR)/"data/dexvm/api19-skeleton.bin"));
    CHECK(linker.ClassCount()==before);CHECK_FALSE(linker.FindClass("Landroid/animation/Animator;"));
    linker.Link();CHECK(linker.ClassCount()==before);CHECK_FALSE(linker.FindClass("Landroid/animation/Animator;"));
    auto array=linker.ResolveDescriptor("Ljava/util/ArrayList;");
    auto slot=linker.FindVtableIndex(array,"size","()I");REQUIRE(slot);
    const auto original=linker.Class(array).vtable[*slot];CHECK_FALSE(linker.Method(original).is_skeleton);
    CHECK(linker.Method(original).kind==MethodKind::interpreted);
    auto id=linker.ResolveDescriptor("Landroid/animation/Animator;");CHECK(linker.Class(id).is_skeleton);
    CHECK(linker.ResolveDescriptor("Landroid/animation/Animator;")==id);
    const auto vibrator=linker.ResolveDescriptor("Landroid/os/NullVibrator;");
    const auto cancel=linker.FindVtableIndex(vibrator,"cancel","()V");REQUIRE(cancel);
    const auto& cancel_method=linker.Method(linker.Class(vibrator).vtable[*cancel]);
    CHECK(cancel_method.owner==vibrator);CHECK(cancel_method.is_skeleton);
    auto context=linker.ResolveDescriptor("Landroid/content/Context;");
    const auto field=linker.FindFieldRecursive(context,"DISPLAY_SERVICE","Ljava/lang/String;");REQUIRE(field);
    CHECK(linker.Field(*field).is_skeleton);CHECK_FALSE(linker.Field(*field).is_unimplemented);
    const auto& constants=linker.Class(context).intrinsic_constants;
    CHECK(std::any_of(constants.begin(),constants.end(),[](const auto& c){return c.name=="DISPLAY_SERVICE" && c.string_value=="display";}));
}

TEST_CASE("API19 skeleton preserves survey neutral methods and recorded stubs") {
    Vm f(InterpreterBackend::switch_dispatch,true,true);
    f.linker.EnableGapSurvey();
    auto result=f.Call("missing");REQUIRE_FALSE(result.exception.IsValid());CHECK(result.value.AsInt()==0);
    auto hits=f.linker.GapSurveyHits();
    CHECK(std::any_of(hits.begin(),hits.end(),[](const auto& h){return h.member=="missing()I";}));
    auto callback=f.linker.ResolveDescriptor("Landroid/shape/Callback;");
    CHECK_FALSE(f.linker.Class(callback).is_skeleton);
    hits=f.linker.GapSurveyHits();
    CHECK(std::any_of(hits.begin(),hits.end(),[](const auto& h){return h.owner_descriptor=="Landroid/shape/Callback;" && h.member.empty();}));
    auto malformed=Surface();malformed.resize(16);
    DexClassLinker bad;
    CHECK_THROWS_AS(bad.RegisterSkeletonSurface(std::move(malformed)),DexVmError);
}

TEST_CASE("API19 skeleton Java loader materializes while findLoadedClass stays passive") {
    Vm f(InterpreterBackend::switch_dispatch);
    ClassLoaderFacade facade(f.linker,f.model);
    CHECK_FALSE(facade.FindLoadedClass(kBootstrapLoader,"android.shape.Base"));
    CHECK_FALSE(f.linker.FindClass("Landroid/shape/Base;"));
    auto id=facade.LoadClass(kBootstrapLoader,"android.shape.Base");
    CHECK(facade.FindLoadedClass(kBootstrapLoader,"android.shape.Base")==id);
    CHECK(facade.LoadClass(kApplicationLoader,"android.shape.Base")==id);
}
