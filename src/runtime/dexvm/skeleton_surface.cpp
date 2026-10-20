#include "class_linker_internal.h"
#include "ogplay/core/json.h"
#include <charconv>
#include "ogplay/core/text.h"
#include <limits>

namespace ogplay::runtime::dexvm {
namespace {
using core::JsonValue;
[[noreturn]] void Bad() { Fail(DexVmErrorReason::invalid_image, "invalid API19 skeleton surface"); }
JsonValue At(JsonValue value, std::size_t i) { auto v=value.Element(i); if (!v) Bad(); return *v; }
std::string Text(JsonValue value) { auto v=value.String(); if (!v) Bad(); return std::string(*v); }
std::u16string Utf16(std::string_view text) {
    if (!core::IsValidUtf8(text)) Bad();
    std::u16string result;
    for (std::size_t i=0;i<text.size();) {
        auto cp=static_cast<std::uint32_t>(static_cast<unsigned char>(text[i++]));
        unsigned extra=0;
        if (cp>=0xf0U) {cp&=7U;extra=3;}
        else if (cp>=0xe0U) {cp&=15U;extra=2;}
        else if (cp>=0xc0U) {cp&=31U;extra=1;}
        while(extra--)cp=(cp<<6U)|(static_cast<unsigned char>(text[i++])&63U);
        if(cp>0xffffU) {cp-=0x10000U;result.push_back(static_cast<char16_t>(0xd800U+(cp>>10U)));result.push_back(static_cast<char16_t>(0xdc00U+(cp&1023U)));}
        else result.push_back(static_cast<char16_t>(cp));
    }
    return result;
}
std::uint32_t Flags(JsonValue value) { auto v=value.UnsignedInteger(); if (!v || *v>UINT32_MAX) Bad(); return static_cast<std::uint32_t>(*v); }
core::JsonDocument Record(const std::vector<std::uint8_t>& bytes, std::pair<std::size_t,std::size_t> range) {
    core::JsonParseError error;
    auto doc=core::JsonDocument::ParseStrict(std::string_view(reinterpret_cast<const char*>(bytes.data()+range.first),range.second),error,4U*1024U*1024U);
    if (!doc || !doc->Root().IsArray() || doc->Root().Size()!=5U) Bad();
    return std::move(*doc);
}
}
void DexClassLinker::RegisterSkeletonSurface(std::vector<std::uint8_t> bytes) {
    if (impl_->link_complete || impl_->application_unit || !impl_->skeleton_bytes.empty())
        Fail(DexVmErrorReason::internal_invariant,"skeleton must be registered once before application/Link");
    if (bytes.size()<12U || std::string_view(reinterpret_cast<const char*>(bytes.data()),8)!="OGSK19\x01\n") Bad();
    std::size_t cursor=8;
    const auto integer=[&](unsigned width) {
        if (cursor>bytes.size() || width>bytes.size()-cursor) Bad();
        std::uint32_t n=0;for (unsigned j=0;j<width;++j)n|=static_cast<std::uint32_t>(bytes[cursor++])<<(8U*j);return n;
    };
    const auto count=integer(4);if (count>20000U) Bad();
    std::unordered_map<std::string,std::pair<std::size_t,std::size_t>> index;
    std::string previous;
    for (std::uint32_t i=0;i<count;++i) {
        const auto length=integer(2);if (!length || length>bytes.size()-cursor) Bad();
        std::string name(reinterpret_cast<const char*>(bytes.data()+cursor),length);cursor+=length;
        const auto offset=integer(4),size=integer(4);
        if (name<=previous || name.front()!='L' || name.back()!=';') Bad();
        previous=name;index.emplace(std::move(name),std::pair{offset,size});
    }
    std::size_t end=cursor;
    for (auto& [name,range]:index) {
        (void)name;
        if (range.first>bytes.size()-cursor || range.second>bytes.size()-cursor-range.first) Bad();
        range.first+=cursor;end=std::max(end,range.first+range.second);
    }
    if (end!=bytes.size()) Bad();
    impl_->skeleton_index=std::move(index);impl_->skeleton_bytes=std::move(bytes);
}
DexClassId DexClassLinker::Impl::MaterializeSkeletonClass(const std::string& descriptor) {
    auto doc=Record(skeleton_bytes,skeleton_index.at(descriptor));auto root=doc.Root();
    LinkedClass cls;cls.descriptor=descriptor;cls.is_skeleton=true;
    cls.access_flags=Flags(At(root,2));cls.is_interface=(cls.access_flags&kAccInterface)!=0;
    cls.initiating_loader_mask=1U;
    const auto id=AddClass(std::move(cls));auto& extra=ExtrasAt(id);
    if (!At(root,0).IsNull())extra.missing_super=Text(At(root,0));
    const auto interfaces=At(root,1);
    for (std::size_t i=0;i<interfaces.Size();++i)extra.missing_interfaces.push_back(Text(At(interfaces,i)));
    return id;
}
std::optional<std::uint32_t> DexClassLinker::Impl::SkeletonMethodFlags(const LinkedMethod& method) const {
    std::vector<std::string> pending{classes[method.owner.Value()-1U].descriptor};
    std::set<std::string> visited;
    while (!pending.empty()) {
        const auto owner=pending.back();pending.pop_back();
        if (!visited.insert(owner).second) continue;
        const auto entry=skeleton_index.find(owner);if(entry==skeleton_index.end()) continue;
        auto doc=Record(skeleton_bytes,entry->second);const auto root=doc.Root();
        const auto methods=At(root,3);
        for(std::size_t i=0;i<methods.Size();++i) {
            const auto row=At(methods,i);
            if(Text(At(row,0))==method.name && Text(At(row,1))==method.descriptor)return Flags(At(row,2));
        }
        const auto interfaces=At(root,1);
        for(std::size_t i=0;i<interfaces.Size();++i) pending.push_back(Text(At(interfaces,i)));
        if(!At(root,0).IsNull())pending.push_back(Text(At(root,0)));
    }
    return std::nullopt;
}
void DexClassLinker::Impl::MergeSkeletonMembers(const DexClassId id) {
    auto& cls=ClassAt(id);auto& extra=ExtrasAt(id);
    if (extra.skeleton_merged || gap_survey) return;
    extra.skeleton_merged=true;
    if (cls.defining_loader!=kBootstrapLoader) return;
    const auto found=skeleton_index.find(cls.descriptor);if(found==skeleton_index.end())return;
    auto doc=Record(skeleton_bytes,found->second);const auto root=doc.Root();
    const auto methods=At(root,3);
    for (std::size_t i=0;i<methods.Size();++i) {
        const auto row=At(methods,i);const auto name=Text(At(row,0)),desc=Text(At(row,1));
        const auto same=[&](VmMethodId mid){const auto& m=MethodAt(mid);return m.name==name&&m.descriptor==desc;};
        if (std::any_of(cls.own_direct_methods.begin(),cls.own_direct_methods.end(),same)||std::any_of(cls.own_virtual_methods.begin(),cls.own_virtual_methods.end(),same))continue;
        // For an already supplied class, an inherited implementation is an existing
        // runtime member. A generated declaration must not replace its vtable
        // handler (or retroactively narrow a handwritten visibility contract).
        const auto flags=Flags(At(row,2));
        bool supplied=false;
        if (!cls.is_skeleton && (flags&(kAccStatic|kAccPrivate))==0 && name!="<init>" && name!="<clinit>") {
            auto parent=cls.super;
            while (parent) {
                const auto& base=ClassAt(*parent);
                for (const auto mid:base.own_virtual_methods) {
                    const auto& method=MethodAt(mid);
                    if (!method.is_skeleton && method.name==name && method.descriptor==desc) supplied=true;
                }
                parent=base.super;
            }
        }
        if (supplied) continue;
        LinkedMethod m;m.owner=id;m.name=name;m.descriptor=desc;m.access_flags=flags;
        m.is_static=(m.access_flags&kAccStatic)!=0;m.is_skeleton=true;m.kind=MethodKind::intrinsic;
        m.overridable=(m.access_flags&(kAccFinal|kAccPrivate))==0;
        const bool direct=m.is_static||(m.access_flags&kAccPrivate)!=0||name=="<init>"||name=="<clinit>";
        m.declared_invoke_kind=m.is_static?DeclaredInvokeKind::static_call:direct?DeclaredInvokeKind::direct:cls.is_interface?DeclaredInvokeKind::interface_call:DeclaredInvokeKind::virtual_call;
        const auto parts=SplitDescriptor(desc);m.shape=ShapeOf(parts,m.is_static);m.ins_words=m.shape.incoming_words;m.return_shorty=ShortyOf(parts.return_type);m.vtable_index=direct?-1:-2;
        auto mid=AddMethod(std::move(m));
        if (direct) {cls.own_direct_methods.push_back(mid);extra.direct_lookup.emplace(MemberKey(name,desc),mid);}
        else cls.own_virtual_methods.push_back(mid);
    }
    const auto fields=At(root,4);
    for (std::size_t i=0;i<fields.Size();++i) {
        const auto row=At(fields,i);const auto name=Text(At(row,0)),desc=Text(At(row,1));
        const auto same=[&](VmFieldId fid){const auto& f=FieldAt(fid);return f.name==name&&f.descriptor==desc;};
        if(std::any_of(cls.own_static_fields.begin(),cls.own_static_fields.end(),same)||std::any_of(cls.own_instance_fields.begin(),cls.own_instance_fields.end(),same))continue;
        LinkedField f;f.owner=id;f.name=name;f.descriptor=desc;f.access_flags=Flags(At(row,2));f.is_static=(f.access_flags&kAccStatic)!=0;
        const auto constant=Flags(At(row,3));f.is_skeleton=true;f.is_unimplemented=constant==0;f.is_ref=IsRefDescriptor(desc);f.is_wide=IsWideDescriptor(desc);
        const auto fid=AddField(std::move(f));(FieldAt(fid).is_static?cls.own_static_fields:cls.own_instance_fields).push_back(fid);
        if(constant) {
            IntrinsicFieldDecl c;c.name=name;c.descriptor=desc;c.is_static=true;c.has_constant=true;
            const auto value=Text(At(row,4));
            if(constant==2){c.string_value=value;c.constant_utf16=Utf16(value);}
            else {auto parsed=std::from_chars(value.data(),value.data()+value.size(),c.integral);if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size())Bad();}
            cls.intrinsic_constants.push_back(std::move(c));
        }
    }
}
}
