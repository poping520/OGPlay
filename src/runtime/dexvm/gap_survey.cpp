#include "ogplay/runtime/dexvm/gap_survey.h"

#include <algorithm>
#include <vector>

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] std::string Quoted(const std::string_view value) {
    std::string result = "\"";
    for (const auto character : value) {
        if (character == '"' || character == '\\') result.push_back('\\');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

}  // namespace

std::string RenderGapSurveyJson(const std::span<const GapSurveyHit> hits,
                                const std::string_view title) {
    std::vector<const GapSurveyHit*> classes;
    std::vector<const GapSurveyHit*> members;
    std::uint64_t total_hits = 0;
    for (const auto& hit : hits) {
        (hit.member.empty() ? classes : members).push_back(&hit);
        total_hits += hit.hits;
    }
    // Hottest first: call counts are the priority signal for the next batch.
    const auto by_hits = [](const GapSurveyHit* left,
                            const GapSurveyHit* right) {
        if (left->hits != right->hits) return left->hits > right->hits;
        if (left->owner_descriptor != right->owner_descriptor) {
            return left->owner_descriptor < right->owner_descriptor;
        }
        return left->member < right->member;
    };
    std::ranges::sort(classes, by_hits);
    std::ranges::sort(members, by_hits);

    std::string json = "{\n  \"schema\": 1,\n  \"survey\": true,\n";
    json += "  \"note\": \"diagnostic survey run: unresolved platform "
            "classes and methods answered neutrally, so this is a gap work "
            "queue and never a compatibility result\",\n";
    json += "  \"title\": " + Quoted(title) + ",\n";
    json += "  \"summary\": {\"missing_classes\": " +
            std::to_string(classes.size()) + ", \"missing_members\": " +
            std::to_string(members.size()) + ", \"stub_hits\": " +
            std::to_string(total_hits) + "},\n";
    json += "  \"missing_classes\": [";
    for (std::size_t index = 0; index < classes.size(); ++index) {
        json += (index == 0 ? "\n    " : ",\n    ");
        json += "{\"class\": " + Quoted(classes[index]->owner_descriptor) +
                ", \"hits\": " + std::to_string(classes[index]->hits) + "}";
    }
    json += classes.empty() ? "],\n" : "\n  ],\n";
    json += "  \"missing_members\": [";
    for (std::size_t index = 0; index < members.size(); ++index) {
        json += (index == 0 ? "\n    " : ",\n    ");
        json += "{\"class\": " + Quoted(members[index]->owner_descriptor) +
                ", \"member\": " + Quoted(members[index]->member) +
                ", \"hits\": " + std::to_string(members[index]->hits) + "}";
    }
    json += members.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return json;
}

}  // namespace ogplay::runtime::dexvm
