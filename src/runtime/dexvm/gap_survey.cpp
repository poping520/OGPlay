#include "ogplay/runtime/dexvm/gap_survey.h"

#include <algorithm>
#include <array>
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

const char* GapSurveySourceName(const GapSurveySource source) noexcept {
    switch (source) {
        case GapSurveySource::skeleton: return "skeleton";
        case GapSurveySource::intrinsic: return "intrinsic";
        case GapSurveySource::bootdex: return "bootdex";
        case GapSurveySource::missing: return "missing";
    }
    return "missing";
}

const char* GapSurveyChannelName(const GapSurveyChannel channel) noexcept {
    switch (channel) {
        case GapSurveyChannel::java: return "java";
        case GapSurveyChannel::jni: return "jni";
        case GapSurveyChannel::reflection: return "reflection";
    }
    return "java";
}

}  // namespace

std::string RenderGapSurveyJson(const std::span<const GapSurveyHit> hits,
                                const std::string_view title) {
    std::vector<const GapSurveyHit*> classes;
    std::vector<const GapSurveyHit*> members;
    std::uint64_t total_hits = 0;
    std::array<std::uint64_t, 4> source_members{};
    std::array<std::uint64_t, kGapSurveyChannelCount> channel_hits{};
    for (const auto& hit : hits) {
        (hit.member.empty() ? classes : members).push_back(&hit);
        total_hits += hit.hits;
        if (!hit.member.empty()) {
            ++source_members[static_cast<std::size_t>(hit.source)];
            for (std::size_t c = 0; c < kGapSurveyChannelCount; ++c) {
                channel_hits[c] += hit.channel_hits[c];
            }
        }
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
    // Earliest first: an early hit is more likely the real first fault of a
    // normal run, because neutral answers may divert everything after it.
    const auto by_first = [&by_hits](const GapSurveyHit* left,
                                     const GapSurveyHit* right) {
        if (left->first_hit_index != right->first_hit_index) {
            return left->first_hit_index < right->first_hit_index;
        }
        return by_hits(left, right);
    };
    std::ranges::sort(classes, by_hits);
    std::ranges::sort(members, by_hits);
    auto first_order = members;
    std::ranges::sort(first_order, by_first);

    std::string json = "{\n  \"schema\": 2,\n  \"survey\": true,\n";
    json += "  \"note\": \"diagnostic survey run: called unimplemented "
            "members and unresolved platform classes were answered neutrally "
            "(0/null/void) and recorded, so this is a gap work queue and "
            "never a compatibility result; behavior after the first neutral "
            "answer may already have diverged from a normal run, so only the "
            "earliest hits can be trusted as real first faults\",\n";
    json += "  \"title\": " + Quoted(title) + ",\n";
    json += "  \"summary\": {\"missing_classes\": " +
            std::to_string(classes.size()) + ", \"missing_members\": " +
            std::to_string(members.size()) + ", \"stub_hits\": " +
            std::to_string(total_hits) + ", \"members_by_source\": {";
    for (std::size_t source = 0; source < source_members.size(); ++source) {
        json += (source == 0 ? "" : ", ");
        json += "\"" +
                std::string(GapSurveySourceName(static_cast<GapSurveySource>(source))) +
                "\": " + std::to_string(source_members[source]);
    }
    json += "}, \"member_hits_by_channel\": {";
    for (std::size_t c = 0; c < kGapSurveyChannelCount; ++c) {
        json += (c == 0 ? "" : ", ");
        json += "\"" +
                std::string(GapSurveyChannelName(static_cast<GapSurveyChannel>(c))) +
                "\": " + std::to_string(channel_hits[c]);
    }
    json += "}},\n";
    json += "  \"missing_classes\": [";
    for (std::size_t index = 0; index < classes.size(); ++index) {
        json += (index == 0 ? "\n    " : ",\n    ");
        json += "{\"class\": " + Quoted(classes[index]->owner_descriptor) +
                ", \"hits\": " + std::to_string(classes[index]->hits) + "}";
    }
    json += classes.empty() ? "],\n" : "\n  ],\n";
    // Full records, hottest first (the historical order).
    json += "  \"missing_members\": [";
    for (std::size_t index = 0; index < members.size(); ++index) {
        const auto& hit = *members[index];
        json += (index == 0 ? "\n    " : ",\n    ");
        json += "{\"class\": " + Quoted(hit.owner_descriptor) +
                ", \"member\": " + Quoted(hit.member) + ", \"kind\": \"" +
                (hit.kind == GapSurveyKind::field ? "field" : "method") +
                "\", \"source\": \"" + GapSurveySourceName(hit.source) +
                "\", \"channel\": \"" + GapSurveyChannelName(hit.first_channel) +
                "\", \"channel_hits\": {";
        for (std::size_t c = 0; c < kGapSurveyChannelCount; ++c) {
            json += (c == 0 ? "" : ", ");
            json += "\"" +
                    std::string(GapSurveyChannelName(static_cast<GapSurveyChannel>(c))) +
                    "\": " + std::to_string(hit.channel_hits[c]);
        }
        json += "}, \"first_hit_index\": " + std::to_string(hit.first_hit_index) +
                ", \"first_uptime_ms\": " + std::to_string(hit.first_uptime_ms) +
                ", \"hits\": " + std::to_string(hit.hits) +
                ", \"first_caller_bootdex\": " +
                (hit.first_caller_bootdex ? "true" : "false") +
                ", \"first_stack\": [";
        for (std::size_t frame = 0; frame < hit.first_stack.size(); ++frame) {
            json += (frame == 0 ? "" : ", ") + Quoted(hit.first_stack[frame]);
        }
        json += "]}";
    }
    json += members.empty() ? "],\n" : "\n  ],\n";
    // Same members, earliest first hit first; references missing_members.
    json += "  \"members_by_first_hit\": [";
    for (std::size_t index = 0; index < first_order.size(); ++index) {
        json += (index == 0 ? "\n    " : ",\n    ");
        json += "{\"class\": " + Quoted(first_order[index]->owner_descriptor) +
                ", \"member\": " + Quoted(first_order[index]->member) +
                ", \"first_hit_index\": " +
                std::to_string(first_order[index]->first_hit_index) + "}";
    }
    json += first_order.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return json;
}

}  // namespace ogplay::runtime::dexvm
