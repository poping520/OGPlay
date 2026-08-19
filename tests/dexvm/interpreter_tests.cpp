// DexVM stage-1 interpreter conformance over dexasm fixtures.
// Expected values are recorded with their semantic source: AOSP
// vm/mterp/c/OP_*.cpp at the pinned baseline (07 §2 mode B) or the Dalvik
// bytecode specification (docs/dalvik-bytecode at the same tag).

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

struct Vm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    explicit Vm(const InterpreterConfig config = {},
                const JavaObjectModelConfig model_config = {})
        : model(strings, arrays, model_config),
          linker(),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterDex(ReadFixture("interp.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, config) {}

    [[nodiscard]] VmMethodId Static(const std::string& class_descriptor,
                                    const std::string& name,
                                    const std::string& descriptor) {
        const auto java_class = linker.FindClass(class_descriptor);
        REQUIRE_MESSAGE(java_class.has_value(), class_descriptor);
        const auto method =
            linker.FindDirectMethod(*java_class, name, descriptor);
        REQUIRE_MESSAGE(method.has_value(), name);
        return *method;
    }

    [[nodiscard]] VmCallOutcome CallStatic(
        const std::string& class_descriptor, const std::string& name,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        return interpreter.Call(Static(class_descriptor, name, descriptor),
                                arguments);
    }
};

struct IntrinsicVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    explicit IntrinsicVm(std::vector<IntrinsicClassDecl> catalog)
        : model(strings, arrays),
          linker(),
          interpreter(
              [this, &catalog]() -> DexClassLinker& {
                  auto core = CoreIntrinsicCatalog();
                  core.insert(core.end(),
                              std::make_move_iterator(catalog.begin()),
                              std::make_move_iterator(catalog.end()));
                  linker.RegisterIntrinsics(core);
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger) {}

    [[nodiscard]] VmMethodId Static(const std::string& class_descriptor,
                                    const std::string& name,
                                    const std::string& descriptor) {
        const auto java_class = linker.FindClass(class_descriptor);
        REQUIRE(java_class.has_value());
        const auto method =
            linker.FindDirectMethod(*java_class, name, descriptor);
        REQUIRE(method.has_value());
        return *method;
    }
};

void ExpectInt(const VmCallOutcome& outcome, const std::int32_t expected) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                    "unexpected exception: ", outcome.exception_message);
    REQUIRE(outcome.value.kind == VmValue::Kind::cat1);
    CHECK(outcome.value.AsInt() == expected);
}

template <typename VmType>
void ExpectException(const VmType& vm, const VmCallOutcome& outcome,
                     const std::string& descriptor) {
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
}

}  // namespace

TEST_CASE("dexvm core intrinsic catalog is unique and structurally stable") {
    const auto catalog = CoreIntrinsicCatalog();
    std::set<std::string> descriptors;
    const std::set<std::string> intentionally_unimplemented = {
        "Ljava/lang/System;.currentTimeMillis()J",
        "Ljava/lang/System;.nanoTime()J",
        "Ljava/lang/System;.load(Ljava/lang/String;)V",
        "Ljava/lang/System;.loadLibrary(Ljava/lang/String;)V",
        "Ljava/lang/System;.exit(I)V",
        "Ljava/util/Date;.<init>()V",
        "Ljava/util/Date;.getTime()J",
        "Ljava/util/Date;.getYear()I",
        "Ljava/lang/AssertionError;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/AssertionError;.<init>(Ljava/lang/Object;)V",
        "Ljava/lang/AssertionError;.<init>(Z)V",
        "Ljava/lang/AssertionError;.<init>(C)V",
        "Ljava/lang/AssertionError;.<init>(I)V",
        "Ljava/lang/AssertionError;.<init>(J)V",
        "Ljava/lang/AssertionError;.<init>(F)V",
        "Ljava/lang/AssertionError;.<init>(D)V",
        "Ljava/lang/ReflectiveOperationException;.<init>"
        "(Ljava/lang/Throwable;)V",
        "Ljava/lang/ReflectiveOperationException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/SecurityException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/SecurityException;.<init>(Ljava/lang/Throwable;)V",
        "Ljava/lang/TypeNotPresentException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/Appendable;.append(C)Ljava/lang/Appendable;",
        "Ljava/lang/Appendable;.append"
        "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;",
        "Ljava/lang/Appendable;.append"
        "(Ljava/lang/CharSequence;II)Ljava/lang/Appendable;",
        "Ljava/lang/AutoCloseable;.close()V",
        "Ljava/lang/CharSequence;.charAt(I)C",
        "Ljava/lang/CharSequence;.subSequence(II)Ljava/lang/CharSequence;",
        "Ljava/lang/CharSequence;.toString()Ljava/lang/String;",
        "Ljava/lang/Comparable;.compareTo(Ljava/lang/Object;)I",
        "Ljava/lang/Iterable;.iterator()Ljava/util/Iterator;",
        "Ljava/lang/Readable;.read(Ljava/nio/CharBuffer;)I",
        "Ljava/lang/Runnable;.run()V",
    };
    for (const auto& declaration : catalog) {
        CHECK(descriptors.insert(declaration.descriptor).second);
        for (const auto& method : declaration.methods) {
            if (!method.implementation) {
                CHECK(intentionally_unimplemented.contains(
                    declaration.descriptor + "." + method.name +
                    method.descriptor));
            }
        }
    }

    const auto signatures = [&catalog](const std::string& descriptor) {
        std::set<std::pair<std::string, std::string>> result;
        const auto declaration = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) {
                return candidate.descriptor == descriptor;
            });
        REQUIRE(declaration != catalog.end());
        for (const auto& method : declaration->methods) {
            result.emplace(method.name, method.descriptor);
        }
        return result;
    };

    CHECK(signatures("Ljava/lang/Object;") ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "()V"},
              {"clone", "()Ljava/lang/Object;"},
              {"equals", "(Ljava/lang/Object;)Z"},
              {"getClass", "()Ljava/lang/Class;"},
              {"hashCode", "()I"},
              {"notify", "()V"},
              {"notifyAll", "()V"},
              {"toString", "()Ljava/lang/String;"},
              {"wait", "()V"},
              {"wait", "(J)V"},
              {"wait", "(JI)V"},
          });
    CHECK(signatures("Ljava/lang/Cloneable;").empty());
    CHECK(signatures("Ljava/lang/Appendable;") ==
          std::set<std::pair<std::string, std::string>>{
              {"append", "(C)Ljava/lang/Appendable;"},
              {"append", "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;"},
              {"append",
               "(Ljava/lang/CharSequence;II)Ljava/lang/Appendable;"},
          });
    CHECK(signatures("Ljava/lang/AutoCloseable;") ==
          std::set<std::pair<std::string, std::string>>{{"close", "()V"}});
    CHECK(signatures("Ljava/lang/Iterable;") ==
          std::set<std::pair<std::string, std::string>>{
              {"iterator", "()Ljava/util/Iterator;"},
          });
    CHECK(signatures("Ljava/lang/Readable;") ==
          std::set<std::pair<std::string, std::string>>{
              {"read", "(Ljava/nio/CharBuffer;)I"},
          });
    CHECK(signatures("Ljava/lang/CharSequence;") ==
          std::set<std::pair<std::string, std::string>>{
              {"length", "()I"},
              {"charAt", "(I)C"},
              {"subSequence", "(II)Ljava/lang/CharSequence;"},
              {"toString", "()Ljava/lang/String;"},
          });
    CHECK(signatures("Ljava/lang/Comparable;") ==
          std::set<std::pair<std::string, std::string>>{
              {"compareTo", "(Ljava/lang/Object;)I"},
          });
    CHECK(signatures("Ljava/lang/Runnable;") ==
          std::set<std::pair<std::string, std::string>>{{"run", "()V"}});
    CHECK(signatures("Ljava/lang/StringBuilder;").size() == 17U);
    CHECK(signatures("Ljava/lang/String;").size() == 43U);
    CHECK(signatures("Ljava/lang/Integer;").size() == 37U);
}

TEST_CASE("dormant classes with missing hierarchy link only when reached") {
    Vm vm;

    // An unrelated usable class proves Link() no longer rejects the entire
    // DEX because an optional packaged class names an absent framework base.
    CHECK(vm.linker.ResolveDescriptor("LCounter;").IsValid());

    try {
        static_cast<void>(
            vm.linker.ResolveDescriptor("LDormantOptional;"));
        FAIL("reached class with missing hierarchy did not fail");
    } catch (const DexVmError& error) {
        const std::string message = error.what();
        CHECK(message.find("class hierarchy is not available: ") !=
              std::string::npos);
        CHECK(message.find("Landroid/optional/MissingActivity;") !=
              std::string::npos);
        CHECK(message.find("required by LDormantOptional;") !=
              std::string::npos);
    }
}

TEST_CASE("survey records a missing hierarchy only when its class is reached") {
    Vm vm;
    vm.linker.EnableGapSurvey();
    CHECK(vm.linker.GapSurveyHits().empty());

    const auto optional =
        vm.linker.ResolveDescriptor("LDormantOptional;");
    CHECK(optional.IsValid());
    const auto hits = vm.linker.GapSurveyHits();
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].owner_descriptor ==
          "Landroid/optional/MissingActivity;");
    CHECK(hits[0].member.empty());
}

TEST_CASE("dexvm API 19 primitive wrapper family inventory is complete") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto declaration = [&catalog](const std::string& descriptor)
        -> const IntrinsicClassDecl& {
        const auto found = std::find_if(catalog.begin(), catalog.end(),
            [&](const auto& candidate) { return candidate.descriptor == descriptor; });
        REQUIRE(found != catalog.end());
        CHECK(std::count_if(catalog.begin(), catalog.end(), [&](const auto& candidate) {
            return candidate.descriptor == descriptor; }) == 1);
        return *found;
    };
    const auto signatures = [](const IntrinsicClassDecl& java_class) {
        std::set<std::string> result;
        for (const auto& method : java_class.methods) {
            result.insert(method.name + method.descriptor);
            CHECK(static_cast<bool>(method.implementation));
        }
        return result;
    };
    const auto fields = [](const IntrinsicClassDecl& java_class) {
        std::set<std::string> result;
        for (const auto& field : java_class.fields) {
            result.insert(field.name + ":" + field.descriptor);
        }
        return result;
    };
    const auto expect = [&](const char* name, const char* superclass,
                            std::initializer_list<const char*> interfaces,
                            std::initializer_list<const char*> methods) {
        const auto& java_class = declaration("Ljava/lang/" + std::string(name) + ";");
        REQUIRE(java_class.superclass.has_value());
        CHECK(*java_class.superclass == superclass);
        CHECK(std::set<std::string>(java_class.interfaces.begin(), java_class.interfaces.end()) ==
              std::set<std::string>(interfaces.begin(), interfaces.end()));
        CHECK(signatures(java_class) == std::set<std::string>(methods.begin(), methods.end()));
    };

    expect("Number", "Ljava/lang/Object;", {"Ljava/io/Serializable;"}, {
        "<init>()V", "byteValue()B", "shortValue()S", "intValue()I",
        "longValue()J", "floatValue()F", "doubleValue()D"});
    expect("Byte", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"}, {
        "<init>(B)V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
        "compareTo(Ljava/lang/Byte;)I","compare(BB)I","decode(Ljava/lang/String;)Ljava/lang/Byte;","equals(Ljava/lang/Object;)Z","hashCode()I",
        "parseByte(Ljava/lang/String;)B","parseByte(Ljava/lang/String;I)B","toString()Ljava/lang/String;","toString(B)Ljava/lang/String;","toHexString(BZ)Ljava/lang/String;",
        "valueOf(B)Ljava/lang/Byte;","valueOf(Ljava/lang/String;)Ljava/lang/Byte;","valueOf(Ljava/lang/String;I)Ljava/lang/Byte;"});
    expect("Short", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"}, {
        "<init>(S)V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
        "compareTo(Ljava/lang/Short;)I","compare(SS)I","decode(Ljava/lang/String;)Ljava/lang/Short;","equals(Ljava/lang/Object;)Z","hashCode()I",
        "parseShort(Ljava/lang/String;)S","parseShort(Ljava/lang/String;I)S","toString()Ljava/lang/String;","toString(S)Ljava/lang/String;","reverseBytes(S)S",
        "valueOf(S)Ljava/lang/Short;","valueOf(Ljava/lang/String;)Ljava/lang/Short;","valueOf(Ljava/lang/String;I)Ljava/lang/Short;"});
    const auto integral_methods = [](const char* wrapper, const char* primitive,
                                     const char* parse, const char* property) {
        std::set<std::string> out{
            std::string("<init>(")+primitive+")V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
            std::string("compareTo(Ljava/lang/")+wrapper+";)I",std::string("compare(")+primitive+primitive+")I",std::string("decode(Ljava/lang/String;)Ljava/lang/")+wrapper+";",
            "equals(Ljava/lang/Object;)Z","hashCode()I",std::string(parse)+"(Ljava/lang/String;)"+primitive,std::string(parse)+"(Ljava/lang/String;I)"+primitive,
            "toString()Ljava/lang/String;",std::string("toString(")+primitive+")Ljava/lang/String;",std::string("toString(")+primitive+"I)Ljava/lang/String;",
            std::string("toBinaryString(")+primitive+")Ljava/lang/String;",std::string("toHexString(")+primitive+")Ljava/lang/String;",std::string("toOctalString(")+primitive+")Ljava/lang/String;",
            std::string("valueOf(")+primitive+")Ljava/lang/"+wrapper+";",std::string("valueOf(Ljava/lang/String;)Ljava/lang/")+wrapper+";",std::string("valueOf(Ljava/lang/String;I)Ljava/lang/")+wrapper+";",
            std::string(property)+"(Ljava/lang/String;)Ljava/lang/"+wrapper+";",std::string(property)+"(Ljava/lang/String;"+primitive+")Ljava/lang/"+wrapper+";",std::string(property)+"(Ljava/lang/String;Ljava/lang/"+wrapper+";)Ljava/lang/"+wrapper+";",
            std::string("highestOneBit(")+primitive+")"+primitive,std::string("lowestOneBit(")+primitive+")"+primitive,std::string("numberOfLeadingZeros(")+primitive+")I",std::string("numberOfTrailingZeros(")+primitive+")I",
            std::string("bitCount(")+primitive+")I",std::string("rotateLeft(")+primitive+"I)"+primitive,std::string("rotateRight(")+primitive+"I)"+primitive,
            std::string("reverseBytes(")+primitive+")"+primitive,std::string("reverse(")+primitive+")"+primitive,std::string("signum(")+primitive+")I"};
        return out;
    };
    const auto check_integral = [&](const char* wrapper, const char* primitive,
                                    const char* parse, const char* property) {
        const auto actual = signatures(declaration(
            "Ljava/lang/" + std::string(wrapper) + ";"));
        const auto expected = integral_methods(wrapper, primitive, parse, property);
        std::vector<std::string> missing;
        std::vector<std::string> extra;
        std::set_difference(expected.begin(), expected.end(), actual.begin(),
                            actual.end(), std::back_inserter(missing));
        std::set_difference(actual.begin(), actual.end(), expected.begin(),
                            expected.end(), std::back_inserter(extra));
        std::string missing_text;
        std::string extra_text;
        for (const auto& value : missing) missing_text += value + " | ";
        for (const auto& value : extra) extra_text += value + " | ";
        CAPTURE(std::string(wrapper));
        CAPTURE(missing_text);
        CAPTURE(extra_text);
        CHECK(actual == expected);
    };
    check_integral("Integer", "I", "parseInt", "getInteger");
    check_integral("Long", "J", "parseLong", "getLong");
    expect("Boolean", "Ljava/lang/Object;", {"Ljava/io/Serializable;","Ljava/lang/Comparable;"}, {
        "<init>(Z)V","<init>(Ljava/lang/String;)V","booleanValue()Z","compare(ZZ)I","compareTo(Ljava/lang/Boolean;)I","equals(Ljava/lang/Object;)Z","hashCode()I",
        "getBoolean(Ljava/lang/String;)Z","parseBoolean(Ljava/lang/String;)Z","toString()Ljava/lang/String;","toString(Z)Ljava/lang/String;","valueOf(Z)Ljava/lang/Boolean;","valueOf(Ljava/lang/String;)Ljava/lang/Boolean;"});
    expect("Float", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"}, {
        "<init>(F)V","<init>(D)V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
        "compare(FF)I","compareTo(Ljava/lang/Float;)I","equals(Ljava/lang/Object;)Z","hashCode()I","isInfinite()Z","isInfinite(F)Z","isNaN()Z","isNaN(F)Z",
        "parseFloat(Ljava/lang/String;)F","toString()Ljava/lang/String;","toString(F)Ljava/lang/String;","toHexString(F)Ljava/lang/String;","valueOf(F)Ljava/lang/Float;","valueOf(Ljava/lang/String;)Ljava/lang/Float;",
        "floatToIntBits(F)I","floatToRawIntBits(F)I","intBitsToFloat(I)F"});
    expect("Double", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"}, {
        "<init>(D)V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
        "compare(DD)I","compareTo(Ljava/lang/Double;)I","equals(Ljava/lang/Object;)Z","hashCode()I","isInfinite()Z","isInfinite(D)Z","isNaN()Z","isNaN(D)Z",
        "parseDouble(Ljava/lang/String;)D","toString()Ljava/lang/String;","toString(D)Ljava/lang/String;","toHexString(D)Ljava/lang/String;","valueOf(D)Ljava/lang/Double;","valueOf(Ljava/lang/String;)Ljava/lang/Double;",
        "doubleToLongBits(D)J","doubleToRawLongBits(D)J","longBitsToDouble(J)D"});

    CHECK(fields(declaration("Ljava/lang/Number;")).empty());
    CHECK(fields(declaration("Ljava/lang/Byte;")) == std::set<std::string>{
        "MAX_VALUE:B","MIN_VALUE:B","SIZE:I","TYPE:Ljava/lang/Class;","value:B"});
    CHECK(fields(declaration("Ljava/lang/Short;")) == std::set<std::string>{
        "MAX_VALUE:S","MIN_VALUE:S","SIZE:I","TYPE:Ljava/lang/Class;","value:S"});
    CHECK(fields(declaration("Ljava/lang/Integer;")) == std::set<std::string>{
        "MAX_VALUE:I","MIN_VALUE:I","SIZE:I","TYPE:Ljava/lang/Class;","value:I"});
    CHECK(fields(declaration("Ljava/lang/Long;")) == std::set<std::string>{
        "MAX_VALUE:J","MIN_VALUE:J","SIZE:I","TYPE:Ljava/lang/Class;","value:J"});
    const std::set<std::string> floating_fields{
        "MAX_EXPONENT:I","MAX_VALUE:X","MIN_EXPONENT:I","MIN_NORMAL:X",
        "MIN_VALUE:X","NEGATIVE_INFINITY:X","NaN:X","POSITIVE_INFINITY:X",
        "SIZE:I","TYPE:Ljava/lang/Class;","value:X"};
    const auto specialize_floating_fields = [&](const char primitive) {
        auto result = floating_fields;
        std::set<std::string> specialized;
        for (auto field : result) {
            if (field.ends_with(":X")) field.back() = primitive;
            specialized.insert(std::move(field));
        }
        return specialized;
    };
    CHECK(fields(declaration("Ljava/lang/Float;")) == specialize_floating_fields('F'));
    CHECK(fields(declaration("Ljava/lang/Double;")) == specialize_floating_fields('D'));
    CHECK(fields(declaration("Ljava/lang/Boolean;")) == std::set<std::string>{
        "FALSE:Ljava/lang/Boolean;","TRUE:Ljava/lang/Boolean;",
        "TYPE:Ljava/lang/Class;","value:Z"});

    const auto& character = declaration("Ljava/lang/Character;");
    CHECK(*character.superclass == "Ljava/lang/Object;");
    CHECK(character.interfaces.size() == 2U);
    CHECK(signatures(character) == std::set<std::string>{
        "<init>(C)V","charValue()C","valueOf(C)Ljava/lang/Character;",
        "compareTo(Ljava/lang/Character;)I","compare(CC)I","equals(Ljava/lang/Object;)Z","hashCode()I",
        "toString()Ljava/lang/String;","toString(C)Ljava/lang/String;","digit(CI)I","digit(II)I","forDigit(II)C",
        "isDigit(C)Z","isDigit(I)Z","isLetter(C)Z","isLetter(I)Z","isLetterOrDigit(C)Z","isLetterOrDigit(I)Z",
        "isLowerCase(C)Z","isLowerCase(I)Z","isUpperCase(C)Z","isUpperCase(I)Z","isWhitespace(C)Z","isWhitespace(I)Z",
        "isSpaceChar(C)Z","isSpaceChar(I)Z","isSpace(C)Z","isISOControl(C)Z","isISOControl(I)Z",
        "toLowerCase(C)C","toLowerCase(I)I","toUpperCase(C)C","toUpperCase(I)I",
        "isHighSurrogate(C)Z","isLowSurrogate(C)Z","isSurrogatePair(CC)Z","isValidCodePoint(I)Z","isBmpCodePoint(I)Z",
        "isSupplementaryCodePoint(I)Z","charCount(I)I","toCodePoint(CC)I","highSurrogate(I)C","lowSurrogate(I)C","reverseBytes(C)C"});
    CHECK(fields(character).size() == 66U);
}

TEST_CASE("dexvm primitive wrapper parsing bits and Character boundaries") {
    Vm vm;
    const auto string = [&](std::string_view value) {
        return VmValue::Ref(vm.interpreter.NewStringUtf8(value));
    };
    const auto call_on = [&](const char* owner, VmObjectRef receiver,
                             const char* name, const char* descriptor,
                             std::vector<VmValue> arguments = {}) {
        const auto java_class = vm.linker.FindClass(owner);
        REQUIRE(java_class.has_value());
        std::optional<VmMethodId> method;
        for (const auto candidate : vm.linker.Class(*java_class).vtable) {
            const auto& linked = vm.linker.Method(candidate);
            if (linked.name == name && linked.descriptor == descriptor) {
                method = candidate;
                break;
            }
        }
        CAPTURE(std::string(owner));
        CAPTURE(std::string(name));
        CAPTURE(std::string(descriptor));
        REQUIRE(method.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.interpreter.Call(*method, arguments);
    };
    const auto as_string = [&](const VmCallOutcome& outcome) {
        REQUIRE_FALSE(outcome.exception.IsValid());
        return vm.interpreter.StringUtf8(outcome.value.ref);
    };
    const auto expect_long = [](const VmCallOutcome& outcome,
                                std::int64_t expected) {
        REQUIRE_FALSE(outcome.exception.IsValid());
        REQUIRE(outcome.value.kind == VmValue::Kind::wide);
        CHECK(outcome.value.AsLong() == expected);
    };
    const auto static_bits = [&](const char* owner, const char* name,
                                 const char* descriptor) {
        const auto java_class = vm.linker.FindClass(owner);
        REQUIRE(java_class.has_value());
        const auto field = vm.linker.FindFieldRecursive(
            *java_class, name, descriptor);
        REQUIRE(field.has_value());
        const auto& linked = vm.linker.Field(*field);
        const auto& storage = vm.linker.Class(linked.owner).static_storage;
        std::uint64_t bits = storage[linked.slot];
        if (linked.is_wide) bits |= static_cast<std::uint64_t>(
            storage[linked.slot + 1]) << 32U;
        return bits;
    };
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;I)I", {string("7fffffff"), VmValue::Int(16)}), std::numeric_limits<std::int32_t>::max());
    ExpectException(vm, vm.CallStatic("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", {string("2147483648")}), "Ljava/lang/NumberFormatException;");
    const auto parsed_min = vm.CallStatic("Ljava/lang/Long;", "parseLong", "(Ljava/lang/String;)J", {string("-9223372036854775808")});
    REQUIRE_FALSE(parsed_min.exception.IsValid());
    CHECK(parsed_min.value.AsLong() == std::numeric_limits<std::int64_t>::min());
    ExpectException(vm, vm.CallStatic("Ljava/lang/Byte;", "parseByte", "(Ljava/lang/String;)B", {string("128")}), "Ljava/lang/NumberFormatException;");
    ExpectInt(vm.CallStatic("Ljava/lang/Short;", "reverseBytes", "(S)S", {VmValue::Int(0x0100)}), 1);
    ExpectException(vm, vm.CallStatic("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", {VmValue::Ref(VmObjectRef{0})}), "Ljava/lang/NumberFormatException;");
    ExpectException(vm, vm.CallStatic("Ljava/lang/Float;", "parseFloat",
                                      "(Ljava/lang/String;)F",
                                      {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
    ExpectException(vm, vm.CallStatic("Ljava/lang/Double;", "parseDouble",
                                      "(Ljava/lang/String;)D",
                                      {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
    ExpectException(vm, vm.CallStatic(
                            "Ljava/lang/Float;", "valueOf",
                            "(Ljava/lang/String;)Ljava/lang/Float;",
                            {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
    ExpectException(vm, vm.CallStatic(
                            "Ljava/lang/Double;", "valueOf",
                            "(Ljava/lang/String;)Ljava/lang/Double;",
                            {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");

    const std::string tiny_float_text =
        "0." + std::string(80, '0') + "1";
    const auto tiny_float = vm.CallStatic(
        "Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F",
        {string(tiny_float_text)});
    REQUIRE_FALSE(tiny_float.exception.IsValid());
    CHECK(tiny_float.value.AsFloat() == 0.0F);
    CHECK_FALSE(std::signbit(tiny_float.value.AsFloat()));

    const std::string tiny_double_text =
        "-0." + std::string(400, '0') + "1";
    const auto tiny_double = vm.CallStatic(
        "Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D",
        {string(tiny_double_text)});
    REQUIRE_FALSE(tiny_double.exception.IsValid());
    CHECK(tiny_double.value.AsDouble() == 0.0);
    CHECK(std::signbit(tiny_double.value.AsDouble()));

    ExpectException(vm, vm.CallStatic(
                            "Ljava/lang/Float;", "parseFloat",
                            "(Ljava/lang/String;)F", {string("0x1.0")}),
                    "Ljava/lang/NumberFormatException;");
    ExpectException(vm, vm.CallStatic(
                            "Ljava/lang/Double;", "parseDouble",
                            "(Ljava/lang/String;)D", {string("-0X1.0")}),
                    "Ljava/lang/NumberFormatException;");

    const auto hex_float = vm.CallStatic(
        "Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F",
        {string("0x1.0p0")});
    REQUIRE_FALSE(hex_float.exception.IsValid());
    CHECK(hex_float.value.AsFloat() == 1.0F);
    const auto hex_double = vm.CallStatic(
        "Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D",
        {string("-0x1.8p1")});
    REQUIRE_FALSE(hex_double.exception.IsValid());
    CHECK(hex_double.value.AsDouble() == -3.0);

    const std::string huge_negative_exponent =
        std::string(400, '9') + "e-1";
    const auto still_overflow = vm.CallStatic(
        "Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D",
        {string(huge_negative_exponent)});
    REQUIRE_FALSE(still_overflow.exception.IsValid());
    CHECK(std::isinf(still_overflow.value.AsDouble()));
    CHECK(still_overflow.value.AsDouble() > 0.0);

    const auto decoded = vm.CallStatic("Ljava/lang/Integer;", "decode", "(Ljava/lang/String;)Ljava/lang/Integer;", {string("-0x80000000")});
    REQUIRE_FALSE(decoded.exception.IsValid());
    ExpectInt(call_on("Ljava/lang/Integer;", decoded.value.ref, "intValue", "()I"), std::numeric_limits<std::int32_t>::min());
    CHECK(as_string(vm.CallStatic("Ljava/lang/Integer;", "toString", "(II)Ljava/lang/String;", {VmValue::Int(35), VmValue::Int(36)})) == "z");
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "rotateLeft", "(II)I", {VmValue::Int(1), VmValue::Int(-1)}), std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(0)}), 32);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "highestOneBit", "(I)I", {VmValue::Int(0)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "highestOneBit", "(I)I", {VmValue::Int(-1)}), std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "lowestOneBit", "(I)I", {VmValue::Int(std::numeric_limits<std::int32_t>::max())}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(1)}), 31);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(-1)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfTrailingZeros", "(I)I", {VmValue::Int(std::numeric_limits<std::int32_t>::min())}), 31);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "rotateLeft", "(II)I", {VmValue::Int(1), VmValue::Int(33)}), 2);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "reverse", "(I)I", {VmValue::Int(1)}), std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "reverseBytes", "(I)I", {VmValue::Int(0x01020304)}), 0x04030201);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "highestOneBit", "(J)J", {VmValue::Long(0)}), 0);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "highestOneBit", "(J)J", {VmValue::Long(-1)}), std::numeric_limits<std::int64_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Long;", "numberOfLeadingZeros", "(J)I", {VmValue::Long(1)}), 63);
    ExpectInt(vm.CallStatic("Ljava/lang/Long;", "numberOfTrailingZeros", "(J)I", {VmValue::Long(std::numeric_limits<std::int64_t>::min())}), 63);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "rotateLeft", "(JI)J", {VmValue::Long(1), VmValue::Int(65)}), 2);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "rotateRight", "(JI)J", {VmValue::Long(1), VmValue::Int(-1)}), 2);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "reverse", "(J)J", {VmValue::Long(1)}), std::numeric_limits<std::int64_t>::min());
    expect_long(vm.CallStatic("Ljava/lang/Long;", "reverseBytes", "(J)J", {VmValue::Long(0x0102030405060708LL)}), 0x0807060504030201LL);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "compare", "(FF)I", {VmValue::Float(-0.0F), VmValue::Float(0.0F)}), -1);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "floatToIntBits", "(F)I", {VmValue::Float(std::numeric_limits<float>::quiet_NaN())}), 0x7fc00000);
    const auto raw_nan = std::bit_cast<float>(0x7fc01234U);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "floatToRawIntBits", "(F)I", {VmValue::Float(raw_nan)}), 0x7fc01234);
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", {VmValue::Float(-0.0F)})) == "-0.0");
    for (const auto& [value, text] : std::vector<std::pair<float, std::string>>{
             {0.0F,"0.0"},{1.0F,"1.0"},{-1.0F,"-1.0"},{0.5F,"0.5"},
             {1.5F,"1.5"},{1.0e8F,"1.0E8"},{1.0e-4F,"1.0E-4"},
             {std::numeric_limits<float>::denorm_min(),"1.4E-45"},
             {std::bit_cast<float>(0x00000003U),"4.2E-45"}}) {
        CAPTURE(text);
        CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString",
            "(F)Ljava/lang/String;", {VmValue::Float(value)})) == text);
    }
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", {VmValue::Float(std::numeric_limits<float>::quiet_NaN())})) == "NaN");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", {VmValue::Float(-std::numeric_limits<float>::infinity())})) == "-Infinity");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toHexString",
                                  "(F)Ljava/lang/String;",
                                  {VmValue::Float(std::numeric_limits<float>::denorm_min())})) ==
          "0x0.000002p-126");
    CHECK(as_string(vm.CallStatic(
              "Ljava/lang/Float;", "toHexString", "(F)Ljava/lang/String;",
              {VmValue::Float(std::bit_cast<float>(0x007fffffU))})) ==
          "0x0.fffffep-126");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Double;", "toString", "(D)Ljava/lang/String;", {VmValue::Double(std::numeric_limits<double>::infinity())})) == "Infinity");
    CHECK(as_string(vm.CallStatic(
              "Ljava/lang/Double;", "toString", "(D)Ljava/lang/String;",
              {VmValue::Double(std::numeric_limits<double>::denorm_min())})) ==
          "4.9E-324");
    CHECK(as_string(vm.CallStatic(
              "Ljava/lang/Double;", "toHexString", "(D)Ljava/lang/String;",
              {VmValue::Double(std::numeric_limits<double>::denorm_min())})) ==
          "0x0.0000000000001p-1022");
    CHECK(as_string(vm.CallStatic(
              "Ljava/lang/Double;", "toHexString", "(D)Ljava/lang/String;",
              {VmValue::Double(
                  std::bit_cast<double>(0x000fffffffffffffULL))})) ==
          "0x0.fffffffffffffp-1022");
    const auto parsed_half = vm.CallStatic("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", {string("  +0.5  ")});
    REQUIRE_FALSE(parsed_half.exception.IsValid());
    CHECK(parsed_half.value.AsDouble() == 0.5);
    ExpectInt(vm.CallStatic("Ljava/lang/Double;", "compare", "(DD)I", {VmValue::Double(std::numeric_limits<double>::quiet_NaN()), VmValue::Double(std::numeric_limits<double>::infinity())}), 1);
    const auto raw_double_nan = std::bit_cast<double>(0x7ff8000000001234ULL);
    expect_long(vm.CallStatic("Ljava/lang/Double;", "doubleToRawLongBits", "(D)J", {VmValue::Double(raw_double_nan)}), static_cast<std::int64_t>(0x7ff8000000001234ULL));
    const auto infinity = vm.CallStatic("Ljava/lang/Float;", "valueOf", "(F)Ljava/lang/Float;", {VmValue::Float(std::numeric_limits<float>::infinity())});
    REQUIRE_FALSE(infinity.exception.IsValid());
    ExpectInt(call_on("Ljava/lang/Float;", infinity.value.ref, "intValue", "()I"), std::numeric_limits<std::int32_t>::max());
    const auto true_one = vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Z)Ljava/lang/Boolean;", {VmValue::Int(1)});
    const auto true_two = vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Boolean;", {string("TRUE")});
    REQUIRE_FALSE(true_one.exception.IsValid());
    REQUIRE_FALSE(true_two.exception.IsValid());
    CHECK(true_one.value.ref == true_two.value.ref);
    const auto false_one = vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Z)Ljava/lang/Boolean;", {VmValue::Int(0)});
    REQUIRE_FALSE(false_one.exception.IsValid());
    CHECK(false_one.value.ref != true_one.value.ref);
    ExpectInt(call_on("Ljava/lang/Boolean;", true_one.value.ref, "hashCode", "()I"), 1231);
    ExpectInt(call_on("Ljava/lang/Boolean;", false_one.value.ref, "compareTo", "(Ljava/lang/Boolean;)I", {VmValue::Ref(true_one.value.ref)}), -1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "toCodePoint", "(CC)I", {VmValue::Int(0xd800), VmValue::Int(0xdc00)}), 0x10000);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isValidCodePoint", "(I)Z", {VmValue::Int(0x110000)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isSupplementaryCodePoint", "(I)Z", {VmValue::Int(0x10ffff)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('Z'), VmValue::Int(36)}), 35);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('1'), VmValue::Int(2)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('a'), VmValue::Int(10)}), -1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "forDigit", "(II)C", {VmValue::Int(15), VmValue::Int(16)}), 'f');
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isLetterOrDigit", "(I)Z", {VmValue::Int('Q')}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "toLowerCase", "(I)I", {VmValue::Int('Q')}), 'q');
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isWhitespace", "(I)Z", {VmValue::Int('\n')}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isHighSurrogate", "(C)Z", {VmValue::Int(0xdbff)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isLowSurrogate", "(C)Z", {VmValue::Int(0xdfff)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "toCodePoint", "(CC)I", {VmValue::Int(0xdbff), VmValue::Int(0xdfff)}), 0x10ffff);
    static_cast<void>(
        vm.interpreter.SetSystemProperty("feature.enabled", "TrUe"));
    ExpectInt(vm.CallStatic("Ljava/lang/Boolean;", "getBoolean", "(Ljava/lang/String;)Z", {string("feature.enabled")}), 1);
    CHECK(static_bits("Ljava/lang/Byte;", "MAX_VALUE", "B") == 127U);
    CHECK(static_bits("Ljava/lang/Short;", "MIN_VALUE", "S") == 0xffff8000U);
    CHECK(static_bits("Ljava/lang/Integer;", "MIN_VALUE", "I") == 0x80000000U);
    CHECK(static_bits("Ljava/lang/Long;", "MAX_VALUE", "J") == 0x7fffffffffffffffULL);
    CHECK(static_bits("Ljava/lang/Float;", "NaN", "F") == 0x7fc00000U);
    CHECK(static_bits("Ljava/lang/Float;", "MIN_NORMAL", "F") == 0x00800000U);
    CHECK(static_bits("Ljava/lang/Double;", "MIN_NORMAL", "D") == 0x0010000000000000ULL);
    CHECK(static_bits("Ljava/lang/Character;", "MAX_CODE_POINT", "I") == 0x10ffffU);
    CHECK(VmObjectRef{static_cast<std::uint32_t>(static_bits(
        "Ljava/lang/Boolean;", "TRUE", "Ljava/lang/Boolean;"))} ==
        true_one.value.ref);
    for (const auto& [owner, primitive] :
         std::vector<std::pair<std::string, std::string>>{
             {"Ljava/lang/Byte;","B"},{"Ljava/lang/Short;","S"},
             {"Ljava/lang/Integer;","I"},{"Ljava/lang/Long;","J"},
             {"Ljava/lang/Float;","F"},{"Ljava/lang/Double;","D"},
             {"Ljava/lang/Boolean;","Z"},{"Ljava/lang/Character;","C"}}) {
        CAPTURE(owner);
        CHECK(VmObjectRef{static_cast<std::uint32_t>(static_bits(
                  owner.c_str(), "TYPE", "Ljava/lang/Class;"))} ==
              vm.model.ClassObject(vm.linker.ResolveDescriptor(primitive)));
    }
}

TEST_CASE("dexvm API 19 java.lang throwable inventory is complete") {
    const std::vector<std::pair<std::string, std::string>> inventory = {
        {"AbstractMethodError", "IncompatibleClassChangeError"},
        {"ArithmeticException", "RuntimeException"},
        {"ArrayIndexOutOfBoundsException", "IndexOutOfBoundsException"},
        {"ArrayStoreException", "RuntimeException"},
        {"AssertionError", "Error"},
        {"ClassCastException", "RuntimeException"},
        {"ClassCircularityError", "LinkageError"},
        {"ClassFormatError", "LinkageError"},
        {"ClassNotFoundException", "ReflectiveOperationException"},
        {"CloneNotSupportedException", "Exception"},
        {"EnumConstantNotPresentException", "RuntimeException"},
        {"Error", "Throwable"},
        {"Exception", "Throwable"},
        {"ExceptionInInitializerError", "LinkageError"},
        {"IllegalAccessError", "IncompatibleClassChangeError"},
        {"IllegalAccessException", "ReflectiveOperationException"},
        {"IllegalArgumentException", "RuntimeException"},
        {"IllegalMonitorStateException", "RuntimeException"},
        {"IllegalStateException", "RuntimeException"},
        {"IllegalThreadStateException", "IllegalArgumentException"},
        {"IncompatibleClassChangeError", "LinkageError"},
        {"IndexOutOfBoundsException", "RuntimeException"},
        {"InstantiationError", "IncompatibleClassChangeError"},
        {"InstantiationException", "ReflectiveOperationException"},
        {"InternalError", "VirtualMachineError"},
        {"InterruptedException", "Exception"},
        {"LinkageError", "Error"},
        {"NegativeArraySizeException", "RuntimeException"},
        {"NoClassDefFoundError", "LinkageError"},
        {"NoSuchFieldError", "IncompatibleClassChangeError"},
        {"NoSuchFieldException", "ReflectiveOperationException"},
        {"NoSuchMethodError", "IncompatibleClassChangeError"},
        {"NoSuchMethodException", "ReflectiveOperationException"},
        {"NullPointerException", "RuntimeException"},
        {"NumberFormatException", "IllegalArgumentException"},
        {"OutOfMemoryError", "VirtualMachineError"},
        {"ReflectiveOperationException", "Exception"},
        {"RuntimeException", "Exception"},
        {"SecurityException", "RuntimeException"},
        {"StackOverflowError", "VirtualMachineError"},
        {"StringIndexOutOfBoundsException", "IndexOutOfBoundsException"},
        {"ThreadDeath", "Error"},
        {"Throwable", "Object"},
        {"TypeNotPresentException", "RuntimeException"},
        {"UnknownError", "VirtualMachineError"},
        {"UnsatisfiedLinkError", "LinkageError"},
        {"UnsupportedClassVersionError", "ClassFormatError"},
        {"UnsupportedOperationException", "RuntimeException"},
        {"VerifyError", "LinkageError"},
        {"VirtualMachineError", "Error"},
    };
    REQUIRE(inventory.size() == 50U);

    const auto catalog = CoreIntrinsicCatalog();
    std::map<std::string, std::size_t> descriptor_counts;
    for (const auto& declaration : catalog) {
        ++descriptor_counts[declaration.descriptor];
    }
    for (const auto& [name, superclass] : inventory) {
        const auto descriptor = "Ljava/lang/" + name + ";";
        CAPTURE(descriptor);
        CHECK(descriptor_counts[descriptor] == 1U);
        const auto declaration = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) {
                return candidate.descriptor == descriptor;
            });
        REQUIRE(declaration != catalog.end());
        REQUIRE(declaration->superclass.has_value());
        CHECK(*declaration->superclass ==
              "Ljava/lang/" + superclass + ";");
    }
}

TEST_CASE("dexvm API 19 throwable representative shapes are source-faithful") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto declaration = [&catalog](const std::string& descriptor)
        -> const IntrinsicClassDecl& {
        const auto found = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) {
                return candidate.descriptor == descriptor;
            });
        REQUIRE(found != catalog.end());
        return *found;
    };
    const auto methods = [](const IntrinsicClassDecl& java_class) {
        std::set<std::pair<std::string, std::string>> result;
        for (const auto& method : java_class.methods) {
            result.emplace(method.name, method.descriptor);
        }
        return result;
    };
    const auto fields = [](const IntrinsicClassDecl& java_class) {
        std::set<std::pair<std::string, std::string>> result;
        for (const auto& field : java_class.fields) {
            if (!field.is_static) {
                result.emplace(field.name, field.descriptor);
            }
        }
        return result;
    };

    CHECK(methods(declaration("Ljava/lang/AssertionError;")) ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "()V"},
              {"<init>",
               "(Ljava/lang/String;Ljava/lang/Throwable;)V"},
              {"<init>", "(Ljava/lang/Object;)V"},
              {"<init>", "(Z)V"},
              {"<init>", "(C)V"},
              {"<init>", "(I)V"},
              {"<init>", "(J)V"},
              {"<init>", "(F)V"},
              {"<init>", "(D)V"},
          });

    const auto& class_not_found =
        declaration("Ljava/lang/ClassNotFoundException;");
    CHECK(fields(class_not_found) ==
          std::set<std::pair<std::string, std::string>>{
              {"ex", "Ljava/lang/Throwable;"}});
    CHECK(methods(class_not_found) ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "()V"},
              {"<init>", "(Ljava/lang/String;)V"},
              {"<init>",
               "(Ljava/lang/String;Ljava/lang/Throwable;)V"},
              {"getCause", "()Ljava/lang/Throwable;"},
              {"getException", "()Ljava/lang/Throwable;"},
          });

    const auto& enum_missing =
        declaration("Ljava/lang/EnumConstantNotPresentException;");
    CHECK(fields(enum_missing) ==
          std::set<std::pair<std::string, std::string>>{
              {"constantName", "Ljava/lang/String;"},
              {"enumType", "Ljava/lang/Class;"},
          });
    CHECK(methods(enum_missing) ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "(Ljava/lang/Class;Ljava/lang/String;)V"},
              {"constantName", "()Ljava/lang/String;"},
              {"enumType", "()Ljava/lang/Class;"},
          });

    const auto& initializer =
        declaration("Ljava/lang/ExceptionInInitializerError;");
    CHECK(fields(initializer) ==
          std::set<std::pair<std::string, std::string>>{
              {"exception", "Ljava/lang/Throwable;"}});
    CHECK(methods(initializer).contains(
        {"<init>", "(Ljava/lang/Throwable;)V"}));
    CHECK(methods(initializer).contains(
        {"getException", "()Ljava/lang/Throwable;"}));
    CHECK(methods(initializer).contains(
        {"getCause", "()Ljava/lang/Throwable;"}));

    const auto& type_missing =
        declaration("Ljava/lang/TypeNotPresentException;");
    CHECK(fields(type_missing) ==
          std::set<std::pair<std::string, std::string>>{
              {"typeName", "Ljava/lang/String;"}});
    CHECK(methods(type_missing) ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>",
               "(Ljava/lang/String;Ljava/lang/Throwable;)V"},
              {"typeName", "()Ljava/lang/String;"},
          });
}

TEST_CASE("dexvm java.lang throwable implementation is one family TU") {
    const std::vector<std::string> classes = {
        "AbstractMethodError", "ArithmeticException",
        "ArrayIndexOutOfBoundsException", "ArrayStoreException",
        "AssertionError", "ClassCastException", "ClassCircularityError",
        "ClassFormatError", "ClassNotFoundException",
        "CloneNotSupportedException", "EnumConstantNotPresentException",
        "Error", "Exception", "ExceptionInInitializerError",
        "IllegalAccessError", "IllegalAccessException",
        "IllegalArgumentException", "IllegalMonitorStateException",
        "IllegalStateException", "IllegalThreadStateException",
        "IncompatibleClassChangeError", "IndexOutOfBoundsException",
        "InstantiationError", "InstantiationException", "InternalError",
        "InterruptedException", "LinkageError", "NegativeArraySizeException",
        "NoClassDefFoundError", "NoSuchFieldError", "NoSuchFieldException",
        "NoSuchMethodError", "NoSuchMethodException", "NullPointerException",
        "NumberFormatException", "OutOfMemoryError",
        "ReflectiveOperationException", "RuntimeException",
        "SecurityException", "StackOverflowError",
        "StringIndexOutOfBoundsException", "ThreadDeath", "Throwable",
        "TypeNotPresentException", "UnknownError", "UnsatisfiedLinkError",
        "UnsupportedClassVersionError", "UnsupportedOperationException",
        "VerifyError", "VirtualMachineError",
    };
    const auto directory = std::filesystem::path(OGPLAY_SOURCE_DIR) / "src" /
                           "runtime" / "dexvm" / "intrinsics";
    CHECK(std::filesystem::is_regular_file(
        directory / "java_lang_throwables.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(
            directory / ("java_lang_" + name + ".cpp")));
    }
}

TEST_CASE("dexvm java.lang primitive wrappers are one family TU") {
    const std::vector<std::string> classes = {
        "Number", "Byte", "Short", "Integer", "Long", "Float", "Double",
        "Boolean", "Character",
    };
    const auto directory = std::filesystem::path(OGPLAY_SOURCE_DIR) / "src" /
                           "runtime" / "dexvm" / "intrinsics";
    CHECK(std::filesystem::is_regular_file(
        directory / "java_lang_primitive_wrappers.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(
            directory / ("java_lang_" + name + ".cpp")));
    }
    std::ifstream header(directory / "catalog.h", std::ios::binary);
    REQUIRE(header.good());
    const std::string source(std::istreambuf_iterator<char>(header), {});
    CHECK(source.find("AppendJavaLangPrimitiveWrappers") != std::string::npos);
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK(source.find("Declare_java_lang_" + name) == std::string::npos);
    }
}

TEST_CASE("dexvm API 19 java.lang interface inventory is complete") {
    const std::vector<std::string> inventory = {
        "Appendable", "AutoCloseable", "CharSequence", "Cloneable",
        "Comparable", "Iterable", "Readable", "Runnable",
    };
    REQUIRE(inventory.size() == 8U);

    const auto catalog = CoreIntrinsicCatalog();
    std::map<std::string, std::size_t> descriptor_counts;
    for (const auto& declaration : catalog) {
        ++descriptor_counts[declaration.descriptor];
    }
    for (const auto& name : inventory) {
        const auto descriptor = "Ljava/lang/" + name + ";";
        CAPTURE(descriptor);
        CHECK(descriptor_counts[descriptor] == 1U);
        const auto declaration = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) {
                return candidate.descriptor == descriptor;
            });
        REQUIRE(declaration != catalog.end());
        CHECK(declaration->is_interface);
        CHECK_FALSE(declaration->superclass.has_value());
    }
}

TEST_CASE("dexvm java.lang interfaces are one family TU") {
    const std::vector<std::string> classes = {
        "Appendable", "AutoCloseable", "CharSequence", "Cloneable",
        "Comparable", "Iterable", "Readable", "Runnable",
    };
    const auto directory = std::filesystem::path(OGPLAY_SOURCE_DIR) / "src" /
                           "runtime" / "dexvm" / "intrinsics";
    CHECK(std::filesystem::is_regular_file(
        directory / "java_lang_interfaces.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(
            directory / ("java_lang_" + name + ".cpp")));
    }
    std::ifstream header(directory / "catalog.h", std::ios::binary);
    REQUIRE(header.good());
    const std::string source(std::istreambuf_iterator<char>(header), {});
    CHECK(source.find("AppendJavaLangInterfaces") != std::string::npos);
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK(source.find("Declare_java_lang_" + name) == std::string::npos);
    }
}

TEST_CASE("dexvm core handlers call directly") {
    Vm hit;
    const auto hit_method =
        hit.Static("Ljava/lang/Math;", "abs", "(I)I");
    REQUIRE(hit.linker.Method(hit_method).implementation);
    ExpectInt(hit.interpreter.Call(
                  hit_method, std::vector<VmValue>{VmValue::Int(-9)}),
              9);
    ExpectInt(hit.interpreter.Call(
                  hit_method, std::vector<VmValue>{VmValue::Int(-11)}),
              11);
}

TEST_CASE("dexvm intrinsic builder binds implementations without a registry") {
    std::uint32_t clinit_calls = 0;
    auto builder = IntrinsicClassBuilder::Class("Lbuilder/Direct;", "Ljava/lang/Object;");
    builder.StaticMethod("answer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(42);
        })
        .FinalMethod("virtualAnswer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(43);
        })
        .VirtualMethod("overridableAnswer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(44);
        })
        .ConstantInt("COUNT", "I", 41)
        .ConstantString("NAME", "direct")
        .ClassInitializer([&clinit_calls](IntrinsicContext&) {
            ++clinit_calls;
            return VmValue::Void();
        });
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));

    ExpectInt(vm.interpreter.Call(vm.Static("Lbuilder/Direct;", "answer",
                                            "()I"),
                                  {}),
              42);
    CHECK(clinit_calls == 1U);
    ExpectInt(vm.interpreter.Call(vm.Static("Lbuilder/Direct;", "answer",
                                            "()I"),
                                  {}),
              42);
    CHECK(clinit_calls == 1U);

    const auto java_class = vm.linker.FindClass("Lbuilder/Direct;");
    REQUIRE(java_class.has_value());
    const auto instance =
        vm.interpreter.NewIntrinsicInstance("Lbuilder/Direct;");
    for (const auto& [name, expected] :
         std::vector<std::pair<std::string, std::int32_t>>{
             {"virtualAnswer", 43}, {"overridableAnswer", 44}}) {
        const auto index =
            vm.linker.FindVtableIndex(*java_class, name, "()I");
        REQUIRE(index.has_value());
        ExpectInt(vm.interpreter.Call(
                      vm.linker.Class(*java_class).vtable[*index],
                      std::vector<VmValue>{VmValue::Ref(instance)}),
                  expected);
    }

    const auto count = vm.linker.FindFieldRecursive(*java_class, "COUNT", "I");
    REQUIRE(count.has_value());
    CHECK(vm.linker.Class(*java_class).static_storage[
              vm.linker.Field(*count).slot] == 41U);
    const auto name = vm.linker.FindFieldRecursive(
        *java_class, "NAME", "Ljava/lang/String;");
    REQUIRE(name.has_value());
    const auto name_ref = VmObjectRef(vm.linker.Class(*java_class)
                                           .static_storage[
                                               vm.linker.Field(*name).slot]);
    CHECK(vm.interpreter.StringUtf8(name_ref) == "direct");
}

TEST_CASE("dexvm intrinsic builder rejects invalid declarations at build") {
    auto expect_invalid = [](IntrinsicClassBuilder builder) {
        bool rejected = false;
        try {
            static_cast<void>(std::move(builder).Build());
        } catch (const DexVmError& error) {
            rejected = true;
            CHECK(error.Reason() == DexVmErrorReason::internal_invariant);
        }
        CHECK(rejected);
    };

    auto duplicate = IntrinsicClassBuilder::Class("Lbuilder/Duplicate;");
    duplicate.UnimplementedStatic("answer", "()I")
        .UnimplementedStatic("answer", "()I");
    expect_invalid(std::move(duplicate));

    auto invalid_descriptor = IntrinsicClassBuilder::Class("Lbuilder/Invalid;");
    invalid_descriptor.UnimplementedStatic("answer", "(I");
    expect_invalid(std::move(invalid_descriptor));

    auto invalid_interface = IntrinsicClassBuilder::Interface("Lbuilder/Interface;");
    invalid_interface.InstanceField("state", "I");
    expect_invalid(std::move(invalid_interface));

    auto constant_range = IntrinsicClassBuilder::Class("Lbuilder/Range;");
    constant_range.ConstantInt("MAX", "B", 128);
    expect_invalid(std::move(constant_range));

    // Declaration-time validation rejects before Build() is reached.
    const auto reject_direct = [](auto declare) {
        bool rejected = false;
        try {
            declare();
        } catch (const DexVmError& error) {
            rejected = true;
            CHECK(error.Reason() == DexVmErrorReason::internal_invariant);
        }
        CHECK(rejected);
    };
    reject_direct([] {
        auto builder = IntrinsicClassBuilder::Class("Lbuilder/BadCtor;");
        builder.Constructor("()I",
                            [](IntrinsicContext&) { return VmValue::Int(1); });
    });
    reject_direct([] {
        auto builder = IntrinsicClassBuilder::Class("Lbuilder/Empty;");
        builder.StaticMethod("answer", "()I", {});
    });
    reject_direct([] {
        auto builder = IntrinsicClassBuilder::Class("Lbuilder/Reserved;");
        builder.FinalMethod("<init>", "()V",
                            [](IntrinsicContext&) { return VmValue::Void(); });
    });
}

TEST_CASE("dexvm intrinsic builder maps headers, members, and constants") {
    const auto root = IntrinsicClassBuilder::RootClass("Ljava/lang/Object;")
                          .Build();
    CHECK(root.descriptor == "Ljava/lang/Object;");
    CHECK_FALSE(root.superclass.has_value());
    CHECK_FALSE(root.is_interface);

    const auto callback = IntrinsicClassBuilder::Interface(
                              "Lbuilder/Callback;", {"Ljava/lang/Runnable;"})
                              .Build();
    CHECK(callback.is_interface);
    REQUIRE(callback.interfaces.size() == 1U);
    CHECK(callback.interfaces[0] == "Ljava/lang/Runnable;");

    auto builder = IntrinsicClassBuilder::Class(
        "Lbuilder/Header;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    builder.InstanceField("id", "I");
    builder.StaticField("counter", "I");
    builder.ConstantInt("MAX", "B", 127);
    builder.Constructor("()V",
                        [](IntrinsicContext&) { return VmValue::Void(); });
    builder.StaticMethod("create", "()Lbuilder/Header;",
        [](IntrinsicContext& context) { return VmValue::Ref(context.receiver); });
    builder.VirtualMethod("describe", "()Ljava/lang/String;",
        [](IntrinsicContext& context) { return VmValue::Ref(context.receiver); });
    builder.FinalMethod("id", "()I", [](IntrinsicContext&) { return VmValue::Int(7); });
    builder.UnimplementedConstructor("(I)V");
    builder.UnimplementedStatic("parse", "(Ljava/lang/String;)I");
    builder.UnimplementedVirtual("run", "()V");
    builder.UnimplementedFinal("close", "()V");
    const auto decl = std::move(builder).Build();
    CHECK_FALSE(decl.is_interface);
    REQUIRE(decl.superclass.has_value());
    CHECK(*decl.superclass == "Ljava/lang/Object;");
    REQUIRE(decl.interfaces.size() == 1U);
    CHECK(decl.interfaces[0] == "Ljava/lang/Runnable;");

    const auto find_method = [&decl](const std::string& name,
                                     const std::string& descriptor) {
        const auto it = std::find_if(
            decl.methods.begin(), decl.methods.end(),
            [&](const IntrinsicMethodDecl& method) {
                return method.name == name && method.descriptor == descriptor;
            });
        REQUIRE(it != decl.methods.end());
        return *it;
    };

    const auto init = find_method("<init>", "()V");
    CHECK_FALSE(init.is_static);
    CHECK_FALSE(init.overridable);
    CHECK(init.implementation);

    const auto create = find_method("create", "()Lbuilder/Header;");
    CHECK(create.is_static);
    CHECK_FALSE(create.overridable);
    CHECK(create.implementation);

    const auto describe = find_method("describe", "()Ljava/lang/String;");
    CHECK_FALSE(describe.is_static);
    CHECK(describe.overridable);
    CHECK(describe.implementation);

    const auto id = find_method("id", "()I");
    CHECK_FALSE(id.is_static);
    CHECK_FALSE(id.overridable);

    find_method("<init>", "(I)V");
    CHECK_FALSE(find_method("parse", "(Ljava/lang/String;)I").implementation);
    CHECK(find_method("run", "()V").overridable);
    CHECK_FALSE(find_method("close", "()V").overridable);

    const auto find_field = [&decl](const std::string& name) {
        const auto it = std::find_if(
            decl.fields.begin(), decl.fields.end(),
            [&](const IntrinsicFieldDecl& field) { return field.name == name; });
        REQUIRE(it != decl.fields.end());
        return *it;
    };
    CHECK_FALSE(find_field("id").is_static);
    CHECK(find_field("counter").is_static);
    const auto max = find_field("MAX");
    CHECK(max.has_constant);
    CHECK(max.integral == 127);
}

TEST_CASE("dexvm declaration miss uses the owner signature and repeats") {
    auto builder = IntrinsicClassBuilder::Class("Lbuilder/Missing;", "Ljava/lang/Object;");
    builder.UnimplementedStatic("answer", "()I");
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));
    const auto method = vm.Static("Lbuilder/Missing;", "answer", "()I");

    for (std::uint32_t call = 0; call < 2; ++call) {
        ExpectException(vm, vm.interpreter.Call(method, {}),
                        "Ljava/lang/UnsatisfiedLinkError;");
    }
    const auto hits = vm.ledger.Unimplemented();
    REQUIRE(hits.size() == 1U);
    CHECK(hits[0].id ==
          "dexvm.intrinsic.Lbuilder/Missing;.answer()I");
    CHECK(hits[0].count == 2U);
}

TEST_CASE("dexvm System properties are deterministic and mutable") {
    Vm vm;
    const auto get = vm.Static(
        "Ljava/lang/System;", "getProperty",
        "(Ljava/lang/String;)Ljava/lang/String;");
    const auto set = vm.Static(
        "Ljava/lang/System;", "setProperty",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    const auto string = [&vm](const std::string& value) {
        return VmValue::Ref(vm.interpreter.NewStringUtf8(value));
    };
    const auto get_property = [&vm, get, &string](const std::string& key) {
        return vm.interpreter.Call(
            get, std::vector<VmValue>{string(key)});
    };

    for (const auto& [key, expected] :
         std::vector<std::pair<std::string, std::string>>{
             {"file.separator", "/"},
             {"line.separator", "\n"},
             {"path.separator", ":"}}) {
        const auto outcome = get_property(key);
        REQUIRE_FALSE(outcome.exception.IsValid());
        REQUIRE(outcome.value.ref.IsValid());
        CHECK(vm.interpreter.StringUtf8(outcome.value.ref) == expected);
    }

    const auto missing = get_property("ogplay.missing");
    REQUIRE_FALSE(missing.exception.IsValid());
    CHECK_FALSE(missing.value.ref.IsValid());

    const auto first = vm.interpreter.Call(
        set, std::vector<VmValue>{string("ogplay.test"), string("first")});
    REQUIRE_FALSE(first.exception.IsValid());
    CHECK_FALSE(first.value.ref.IsValid());
    const auto first_read = get_property("ogplay.test");
    REQUIRE_FALSE(first_read.exception.IsValid());
    REQUIRE(first_read.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(first_read.value.ref) == "first");

    const auto second = vm.interpreter.Call(
        set, std::vector<VmValue>{string("ogplay.test"), string("second")});
    REQUIRE_FALSE(second.exception.IsValid());
    REQUIRE(second.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(second.value.ref) == "first");
    const auto second_read = get_property("ogplay.test");
    REQUIRE_FALSE(second_read.exception.IsValid());
    REQUIRE(second_read.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(second_read.value.ref) == "second");

    ExpectException(
        vm,
        vm.interpreter.Call(
            get, std::vector<VmValue>{VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NullPointerException;");
    ExpectException(vm,
                    vm.interpreter.Call(
                        get, std::vector<VmValue>{string("")}),
                    "Ljava/lang/IllegalArgumentException;");
    ExpectException(
        vm,
        vm.interpreter.Call(
            set, std::vector<VmValue>{string("ogplay.null"),
                                      VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NullPointerException;");
}

TEST_CASE("dexvm arithmetic edge semantics") {
    Vm vm;
    // OP_DIV_INT.cpp: MIN_INT / -1 == MIN_INT (no trap).
    ExpectInt(vm.CallStatic("LArith;", "divide", "(II)I",
                            {VmValue::Int(std::numeric_limits<
                                          std::int32_t>::min()),
                             VmValue::Int(-1)}),
              std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("LArith;", "divide", "(II)I",
                            {VmValue::Int(7), VmValue::Int(-2)}),
              -3);
    // OP_REM_INT.cpp: MIN_INT % -1 == 0.
    ExpectInt(vm.CallStatic("LArith;", "remainder", "(II)I",
                            {VmValue::Int(std::numeric_limits<
                                          std::int32_t>::min()),
                             VmValue::Int(-1)}),
              0);
    // OP_DIV_INT.cpp: divide by zero throws ArithmeticException.
    const auto division = vm.CallStatic(
        "LArith;", "divide", "(II)I", {VmValue::Int(1), VmValue::Int(0)});
    ExpectException(vm, division, "Ljava/lang/ArithmeticException;");
    // In-method catch handler observes the exception.
    ExpectInt(vm.CallStatic("LArith;", "divideCaught", "(II)I",
                            {VmValue::Int(1), VmValue::Int(0)}),
              -99);
    // OP_CMPL_FLOAT.cpp: NaN biases to -1; OP_CMPG_FLOAT.cpp biases to +1.
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    ExpectInt(vm.CallStatic("LArith;", "cmplFloat", "(FF)I",
                            {VmValue::Float(nan), VmValue::Float(0.0f)}),
              -1);
    ExpectInt(vm.CallStatic("LArith;", "cmpgFloat", "(FF)I",
                            {VmValue::Float(nan), VmValue::Float(0.0f)}),
              1);
    ExpectInt(vm.CallStatic("LArith;", "cmplFloat", "(FF)I",
                            {VmValue::Float(2.0f), VmValue::Float(1.0f)}),
              1);
    // OP_SHL_INT.cpp: shift distance masks to 5 bits (33 -> 1).
    ExpectInt(vm.CallStatic("LArith;", "shifts", "(II)I",
                            {VmValue::Int(3), VmValue::Int(33)}),
              6);
    // OP_USHR_INT.cpp: logical shift of negative operand.
    ExpectInt(vm.CallStatic("LArith;", "ushr", "(II)I",
                            {VmValue::Int(-1), VmValue::Int(28)}),
              15);
    // OP_FLOAT_TO_INT.cpp: NaN -> 0, +inf -> MAX_INT.
    ExpectInt(vm.CallStatic("LArith;", "floatToInt", "(F)I",
                            {VmValue::Float(nan)}),
              0);
    ExpectInt(vm.CallStatic("LArith;", "floatToInt", "(F)I",
                            {VmValue::Float(
                                std::numeric_limits<float>::infinity())}),
              std::numeric_limits<std::int32_t>::max());
    // OP_INT_TO_SHORT.cpp: truncation with sign extension.
    ExpectInt(vm.CallStatic("LArith;", "intToShort", "(I)I",
                            {VmValue::Int(0x18000)}),
              static_cast<std::int32_t>(
                  static_cast<std::int16_t>(0x8000)));
    // OP_MUL_LONG.cpp: 64-bit wraparound multiply.
    const auto product = vm.CallStatic(
        "LArith;", "longMul", "(JJ)J",
        {VmValue::Long(0x100000001LL), VmValue::Long(3)});
    REQUIRE(product.value.kind == VmValue::Kind::wide);
    CHECK(product.value.AsLong() == 0x300000003LL);
}

TEST_CASE("dexvm control flow and switches") {
    Vm vm;
    ExpectInt(vm.CallStatic("LFlow;", "loopSum", "(I)I", {VmValue::Int(10)}),
              45);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(1)}), 1);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(2)}), 22);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(9)}), 0);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I", {VmValue::Int(-5)}),
              111);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I",
                            {VmValue::Int(1000)}),
              222);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I", {VmValue::Int(3)}),
              -1);
}

TEST_CASE("dexvm arrays and implicit exceptions") {
    Vm vm;
    ExpectInt(vm.CallStatic("LFlow;", "sumArray", "()I"), 24);
    ExpectException(vm, vm.CallStatic("LFlow;", "outOfBounds", "()I"),
                    "Ljava/lang/ArrayIndexOutOfBoundsException;");
    ExpectException(vm, vm.CallStatic("LFlow;", "npe", "()I"),
                    "Ljava/lang/NullPointerException;");
}

TEST_CASE("dexvm objects, fields and dispatch") {
    Vm vm;
    // Instance fields via <init>/iget/iput and virtual dispatch.
    ExpectInt(vm.CallStatic("LTask;", "exercise", "()I"), 77);
    ExpectInt(vm.CallStatic("LTypes;", "isRunnable", "()I"), 1);
    ExpectException(vm, vm.CallStatic("LTypes;", "badCast", "()V"),
                    "Ljava/lang/ClassCastException;");
    ExpectInt(vm.CallStatic("LTypes;", "strings", "()I"), 6);
}

TEST_CASE("dexvm Object.clone is a shallow copy gated by Cloneable") {
    Vm vm;
    // Cloneable check and payload copy follow libcore Object.java plus
    // AOSP vm/alloc/Alloc.cpp dvmCloneObject at the pinned baseline.
    ExpectInt(vm.CallStatic("LCloneProbe;", "cloneFields", "()I"), 1);
    const auto denied = vm.CallStatic("LCloneProbe;", "clonePlainObject",
                                      "()Ljava/lang/Object;");
    ExpectException(vm, denied, "Ljava/lang/CloneNotSupportedException;");
    CHECK(denied.exception_message == "Class doesn't implement Cloneable");
    ExpectInt(vm.CallStatic("LCloneProbe;", "cloneInts", "()I"), 1);
    ExpectInt(vm.CallStatic("LCloneProbe;", "cloneObjects", "()I"), 1);
    const auto ints = vm.linker.ResolveDescriptor("[I");
    const auto cloneable =
        vm.linker.ResolveDescriptor("Ljava/lang/Cloneable;");
    const auto serializable =
        vm.linker.ResolveDescriptor("Ljava/io/Serializable;");
    CHECK(vm.linker.IsAssignable(cloneable, ints));
    CHECK(vm.linker.IsAssignable(serializable, ints));
}

TEST_CASE("dexvm virtual and super dispatch through subclass") {
    Vm vm;
    // Construct LDoubler and call describe() virtually via LCounter ref.
    const auto doubler_class = vm.linker.FindClass("LDoubler;");
    REQUIRE(doubler_class.has_value());
    vm.linker.EnsureClassLinked(*doubler_class);
    const auto init =
        vm.linker.FindDirectMethod(*doubler_class, "<init>", "(I)V");
    REQUIRE(init.has_value());
    const auto instance = vm.model.NewInstance(
        *doubler_class, vm.linker.Class(*doubler_class).instance_slots);
    const auto construct = vm.interpreter.Call(
        *init, std::vector<VmValue>{VmValue::Ref(instance), VmValue::Int(4)});
    REQUIRE(!construct.exception.IsValid());

    const auto index = vm.linker.FindVtableIndex(*doubler_class, "describe",
                                                 "()I");
    REQUIRE(index.has_value());
    const auto target = vm.linker.Class(*doubler_class).vtable[*index];
    const auto outcome = vm.interpreter.Call(
        target, std::vector<VmValue>{VmValue::Ref(instance)});
    ExpectInt(outcome, 200);  // super 100 * 2 via invoke-super

    // get() reads the field written by the chained constructors.
    const auto get_index =
        vm.linker.FindVtableIndex(*doubler_class, "get", "()I");
    REQUIRE(get_index.has_value());
    ExpectInt(vm.interpreter.Call(
                  vm.linker.Class(*doubler_class).vtable[*get_index],
                  std::vector<VmValue>{VmValue::Ref(instance)}),
              4);
}

TEST_CASE("dexvm clinit runs once before static access") {
    Vm vm;
    ExpectInt(vm.CallStatic("LClinitUser;", "read", "()I"), 55);
    CHECK(vm.interpreter.Stats().classes_initialized >= 1);
    const auto before = vm.interpreter.Stats().classes_initialized;
    ExpectInt(vm.CallStatic("LClinitUser;", "read", "()I"), 55);
    CHECK(vm.interpreter.Stats().classes_initialized == before);
    // Static initial value from encoded_array.
    const auto counter_class = vm.linker.FindClass("LCounter;");
    REQUIRE(counter_class.has_value());
    const auto outcome =
        vm.interpreter.EnsureClassInitialized(*counter_class);
    REQUIRE(!outcome.exception.IsValid());
    CHECK(vm.linker.Class(*counter_class).static_storage[0] == 7);
}

TEST_CASE("dexvm exceptions across frames with real messages") {
    Vm vm;
    // Custom VM exception subclass: caught by Exception handler in the
    // caller frame; message length of "boom" is 4.
    ExpectInt(vm.CallStatic("LThrower;", "catchAcrossFrames", "()I"), 4);

    const auto uncaught = vm.CallStatic("LThrower;", "uncaught", "()I");
    ExpectException(vm, uncaught, "LMyError;");
    CHECK(uncaught.exception_message == "boom");
    REQUIRE(!uncaught.exception_stack.empty());
    CHECK(uncaught.exception_stack[0].method_name == "fail");
}

TEST_CASE("dexvm stack overflow is a real catchable error") {
    Vm vm;
    const auto outcome =
        vm.CallStatic("LFlow;", "recurse", "(I)I", {VmValue::Int(0)});
    ExpectException(vm, outcome, "Ljava/lang/StackOverflowError;");
}

TEST_CASE("dexvm tick budget exhaustion is a fatal structured error") {
    InterpreterConfig config;
    config.tick_budget = 100;
    Vm vm(config);
    CHECK_THROWS_AS(static_cast<void>(vm.CallStatic(
                        "LFlow;", "recurse", "(I)I", {VmValue::Int(0)})),
                    DexVmError);
}

TEST_CASE("dexvm heap budget exhaustion surfaces OutOfMemoryError") {
    JavaObjectModelConfig model_config;
    model_config.heap_budget_bytes = 40;
    Vm vm(InterpreterConfig{}, model_config);
    const auto outcome = vm.CallStatic("LFlow;", "sumArray", "()I");
    ExpectException(vm, outcome, "Ljava/lang/OutOfMemoryError;");
}

TEST_CASE("dexvm GC watermark bounds allocation and zero disables collection") {
    JavaObjectModelConfig enabled;
    enabled.heap_budget_bytes = 128;
    enabled.gc_watermark_percent = 75;
    Vm collecting(InterpreterConfig{}, enabled);
    ExpectInt(collecting.CallStatic("LFlow;", "gcChurn", "()I"), 10);
    CHECK(collecting.interpreter.Stats().gc_collections > 0);
    CHECK(collecting.interpreter.Stats().gc_freed_bytes > 0);
    CHECK(collecting.model.AllocatedBytes() <= enabled.heap_budget_bytes);

    JavaObjectModelConfig disabled = enabled;
    disabled.gc_watermark_percent = 0;
    Vm gc_a(InterpreterConfig{}, disabled);
    const auto outcome = gc_a.CallStatic("LFlow;", "gcChurn", "()I");
    ExpectException(gc_a, outcome, "Ljava/lang/OutOfMemoryError;");
    CHECK(gc_a.interpreter.Stats().gc_collections == 0);
}

TEST_CASE("dexvm execution contexts isolate mutable interpreter state") {
    Vm vm;
    const auto first = vm.interpreter.CreateExecutionContext();
    const auto second = vm.interpreter.CreateExecutionContext();

    const auto loop = vm.Static("LFlow;", "loopSum", "(I)I");
    ExpectInt(vm.interpreter.Call(
                  first, loop, std::vector<VmValue>{VmValue::Int(10)}),
              45);
    const auto first_after_loop = vm.interpreter.ExecutionSnapshot(first);
    CHECK(first_after_loop.frame_depth == 0);
    CHECK(first_after_loop.ticks > 0);

    ExpectInt(vm.interpreter.Call(
                  second, loop, std::vector<VmValue>{VmValue::Int(3)}),
              3);
    const auto second_after_loop = vm.interpreter.ExecutionSnapshot(second);
    CHECK(second_after_loop.frame_depth == 0);
    CHECK(second_after_loop.ticks > 0);
    CHECK(vm.interpreter.ExecutionSnapshot(first).ticks ==
          first_after_loop.ticks);

    const auto uncaught = vm.interpreter.Call(
        first, vm.Static("LThrower;", "uncaught", "()I"), {});
    ExpectException(vm, uncaught, "LMyError;");
    CHECK(!vm.interpreter.ExecutionSnapshot(first).has_pending_exception);
    ExpectInt(vm.interpreter.Call(
                  second, loop, std::vector<VmValue>{VmValue::Int(2)}),
              1);
    CHECK(!vm.interpreter.ExecutionSnapshot(second).has_pending_exception);

    const auto retain = vm.Static("LExecutionContextProbe;", "retainMonitor",
                                  "()V");
    const auto retained = vm.interpreter.Call(first, retain, {});
    CHECK(!retained.exception.IsValid());
    CHECK(vm.interpreter.ExecutionSnapshot(first).held_monitor_count == 1);
    CHECK(vm.interpreter.ExecutionSnapshot(second).held_monitor_count == 0);

    Vm other;
    CHECK_THROWS_AS(
        static_cast<void>(other.interpreter.ExecutionSnapshot(first)),
        DexVmError);
}
