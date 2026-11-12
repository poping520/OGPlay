#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_format_Time_Natives(const Context&) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/text/format/Time;");
    builder.MergesBootClass();
    const auto delegate = [](const char* name, const char* signature, bool instance) {
        const std::string target = instance ? std::string("(Landroid/text/format/Time;") + (signature + 1) : signature;
        return [name, target, instance](dx::IntrinsicContext& call) {
            auto& vm = call.vm;
            const auto owner = vm.Linker().ResolveDescriptor("Landroid/text/format/TimeNative;");
            const auto method = vm.Linker().FindDirectMethod(owner, name, target);
            if (!method) throw dx::DexVmError{dx::DexVmErrorReason::internal_invariant, "Time native delegate missing"};
            std::vector<dx::VmValue> args;
            if (instance) args.push_back(dx::VmValue::Ref(call.receiver));
            args.insert(args.end(), call.arguments.begin(), call.arguments.end());
            const auto outcome = vm.Call(*method, args);
            if (outcome.exception.IsValid()) {
                const auto type = vm.Linker().Class(outcome.exception_class).descriptor;
                if (type == "Landroid/text/format/TimeNative$ParseFailure;")
                    throw dx::VmJavaThrow{"Landroid/util/TimeFormatException;", outcome.exception_message};
                if (type == "Ljava/lang/UnsupportedOperationException;" && std::string_view(name) == "format1")
                    if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.android_time", 0);
                throw dx::VmJavaThrow{type, outcome.exception_message, outcome.exception};
            }
            return outcome.value;
        };
    };
    constexpr auto public_native = dx::kAccPublic | dx::kAccNative;
    constexpr auto private_native = dx::kAccPrivate | dx::kAccNative;
    for (const auto& [name, signature] :
         std::array{std::pair{"normalize", "(Z)J"}, std::pair{"switchTimezone", "(Ljava/lang/String;)V"},
                    std::pair{"setToNow", "()V"}, std::pair{"toMillis", "(Z)J"}, std::pair{"set", "(J)V"},
                    std::pair{"format2445", "()Ljava/lang/String;"}}) {
        builder.VirtualMethod(name, signature, delegate(name, signature, true), public_native);
    }
    builder.OverrideMethod("toString", "()Ljava/lang/String;", delegate("describe", "()Ljava/lang/String;", true),
                           public_native);
    for (const auto* name : {"nativeParse", "nativeParse3339"}) {
        constexpr auto signature = "(Ljava/lang/String;)Z";
        builder.DirectMethod(name, signature, delegate(name, signature, true), private_native);
    }
    constexpr auto compare = "(Landroid/text/format/Time;Landroid/text/format/Time;)I";
    builder.StaticMethod("nativeCompare", compare, delegate("nativeCompare", compare, false),
                         private_native | dx::kAccStatic);

    const std::array arrays{
        builder.BoundStaticField("sShortMonths", "[Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sLongMonths", "[Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sLongStandaloneMonths", "[Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sShortWeekdays", "[Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sLongWeekdays", "[Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic)};
    const std::array strings{
        builder.BoundStaticField("sTimeOnlyFormat", "Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sDateOnlyFormat", "Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sDateTimeFormat", "Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sAm", "Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sPm", "Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic),
        builder.BoundStaticField("sDateCommand", "Ljava/lang/String;", dx::kAccPrivate | dx::kAccStatic)};
    const auto format =
        delegate("format1",
                 "(Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;"
                 "[Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                 "Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                 true);
    builder.DirectMethod(
        "format1", "(Ljava/lang/String;)Ljava/lang/String;",
        [arrays, strings, format](dx::IntrinsicContext& call) {
            const dx::IntrinsicCall fields(call);
            std::vector<dx::VmValue> args{call.arguments[0]};
            for (auto field : arrays)
                args.push_back(dx::VmValue::Ref(fields.GetRef(field)));
            for (auto field : strings)
                args.push_back(dx::VmValue::Ref(fields.GetRef(field)));
            auto forwarded = call;
            forwarded.arguments = args;
            return format(forwarded);
        },
        private_native);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
