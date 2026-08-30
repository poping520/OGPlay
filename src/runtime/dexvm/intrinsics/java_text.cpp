#include "catalog.h"
#include "shared.h"

#include <string>
#include <string_view>
#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

constexpr std::string_view kPatternCharacters = "GyMdkHmsSEDFwWahKzZLc";

void ValidateSimpleDateFormatPattern(const std::string_view pattern) {
  bool quoted{};
  for (std::size_t index = 0; index < pattern.size(); ++index) {
    const auto character = pattern[index];
    if (character == '\'') {
      if (index + 1 < pattern.size() && pattern[index + 1] == '\'') {
        ++index;
      } else {
        quoted = !quoted;
      }
      continue;
    }
    const auto ascii_letter =
        (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z');
    if (!quoted && ascii_letter &&
        kPatternCharacters.find(character) == std::string_view::npos) {
      throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                        "Unknown pattern character '" +
                            std::string(1, character) + "'"};
    }
  }
  if (quoted) {
    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                      "Unterminated quote"};
  }
}

IntrinsicClassDecl DeclareFormat() {
  return std::move(IntrinsicClassBuilder::Class(
                       "Ljava/text/Format;", "Ljava/lang/Object;",
                       {"Ljava/io/Serializable;", "Ljava/lang/Cloneable;"},
                       0x0401U))
      .Build();
}

IntrinsicClassDecl DeclareDateFormat() {
  return std::move(IntrinsicClassBuilder::Class(
                       "Ljava/text/DateFormat;", "Ljava/text/Format;", {},
                       0x0401U))
      .Build();
}

IntrinsicClassDecl DeclareSimpleDateFormat() {
  auto builder = IntrinsicClassBuilder::Class(
      "Ljava/text/SimpleDateFormat;", "Ljava/text/DateFormat;");
  const auto pattern = builder.BoundInstanceField(
      "pattern", "Ljava/lang/String;", 0x0002U);
  // 使用指定的非本地化模式和 Locale 创建日期格式器。
  builder.Constructor(
      "(Ljava/lang/String;Ljava/util/Locale;)V",
      [pattern](IntrinsicContext& context) {
        IntrinsicCall call(context);
        static_cast<void>(call.NonNullRef(1, "locale"));
        const auto pattern_ref = call.NonNullRef(0, "pattern");
        ValidateSimpleDateFormatPattern(call.Vm().StringUtf8(pattern_ref));
        call.SetRef(pattern, pattern_ref);
        return VmValue::Void();
      });
  return std::move(builder).Build();
}

}  // namespace

void AppendJavaText(std::vector<IntrinsicClassDecl>& catalog) {
  catalog.push_back(DeclareFormat());
  catalog.push_back(DeclareDateFormat());
  catalog.push_back(DeclareSimpleDateFormat());
}

}  // namespace ogplay::runtime::dexvm::intrinsics
