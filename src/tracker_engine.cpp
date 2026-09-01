#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <torrentutils/core/tracker_engine.hpp>
#include <unordered_set>
#include <utility>
#include <vector>

namespace torrentutils::core {
namespace {

constexpr std::size_t kMaximumImportBytes = std::size_t{1024U} * 1024U;
constexpr int kMaximumJsonDepth = 64;
constexpr std::string_view kJsonFormat = "torrentutils.tracker-list/v1";

using TrackerVectors = std::vector<std::vector<TrackerUrl>>;

[[nodiscard]] Error validation_error(std::string field, std::string message)
{
    Error error;
    error.code = ErrorCode::ValidationFailed;
    error.message = message;
    error.issues.push_back({std::move(field), std::move(message)});
    return error;
}

[[nodiscard]] Error validation_errors(std::vector<FieldIssue> issues)
{
    Error error;
    error.code = ErrorCode::ValidationFailed;
    error.message = "tracker input validation failed";
    error.issues = std::move(issues);
    return error;
}

[[nodiscard]] Result<TrackerList> make_list(TrackerVectors tiers)
{
    std::vector<TrackerTier> result;
    result.reserve(tiers.size());
    for (auto& endpoints : tiers)
    {
        if (endpoints.empty())
        {
            continue;
        }
        auto tier = TrackerTier::create(std::move(endpoints));
        if (!tier)
        {
            return Result<TrackerList>::failure(tier.error());
        }
        result.push_back(std::move(tier).value());
    }
    return TrackerList::create(std::move(result));
}

[[nodiscard]] TrackerVectors copy_tiers(const TrackerList& trackers)
{
    TrackerVectors result;
    result.reserve(trackers.tiers().size());
    for (const auto& tier : trackers.tiers())
    {
        result.push_back(tier.trackers());
    }
    return result;
}

[[nodiscard]] Result<TrackerList> invalid_tier_index(std::size_t index)
{
    return Result<TrackerList>::failure(validation_error(
        "tracker.tiers[" + std::to_string(index) + "]", "tier index is out of range"));
}

[[nodiscard]] Result<TrackerList> invalid_tracker_index(std::size_t tier_index,
                                                        std::size_t tracker_index)
{
    return Result<TrackerList>::failure(
        validation_error("tracker.tiers[" + std::to_string(tier_index) + "].trackers[" +
                             std::to_string(tracker_index) + "]",
                         "tracker index is out of range"));
}

[[nodiscard]] bool is_ascii_whitespace(const unsigned char character) noexcept
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

[[nodiscard]] std::string_view trim_ascii(std::string_view value) noexcept
{
    while (!value.empty() && is_ascii_whitespace(static_cast<unsigned char>(value.front())))
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && is_ascii_whitespace(static_cast<unsigned char>(value.back())))
    {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] bool is_valid_utf8(const std::string_view value) noexcept
{
    for (std::size_t index = 0; index < value.size();)
    {
        const auto first = static_cast<std::uint8_t>(value[index]);
        if (first <= 0x7fU)
        {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        std::uint32_t code_point = 0;
        if ((first & 0xe0U) == 0xc0U)
        {
            continuation_count = 1;
            code_point = first & 0x1fU;
        }
        else if ((first & 0xf0U) == 0xe0U)
        {
            continuation_count = 2;
            code_point = first & 0x0fU;
        }
        else if ((first & 0xf8U) == 0xf0U)
        {
            continuation_count = 3;
            code_point = first & 0x07U;
        }
        else
        {
            return false;
        }

        if (index + continuation_count >= value.size())
        {
            return false;
        }
        for (std::size_t continuation = 1; continuation <= continuation_count; ++continuation)
        {
            const auto byte = static_cast<std::uint8_t>(value[index + continuation]);
            if ((byte & 0xc0U) != 0x80U)
            {
                return false;
            }
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if ((continuation_count == 1 && code_point < 0x80U) ||
            (continuation_count == 2 && code_point < 0x800U) ||
            (continuation_count == 3 && code_point < 0x10000U) || code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU))
        {
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

[[nodiscard]] std::string text_line_field(const std::size_t line_number)
{
    return "tracker.text.lines[" + std::to_string(line_number) + "]";
}

[[nodiscard]] std::string json_tracker_field(const std::size_t tier_index,
                                             const std::size_t tracker_index)
{
    return "tracker.json.tiers[" + std::to_string(tier_index) + "][" +
           std::to_string(tracker_index) + "]";
}

[[nodiscard]] std::string tracker_validation_message(const Error& error)
{
    return error.issues.empty() ? std::string("must be a valid tracker URL")
                                : error.issues.front().message;
}

} // namespace

Result<TrackerList> TrackerEngine::add_to_tier(const TrackerList& trackers,
                                               const std::size_t tier_index, TrackerUrl tracker)
{
    auto tiers = copy_tiers(trackers);
    if (tier_index >= tiers.size())
    {
        return invalid_tier_index(tier_index);
    }
    tiers[tier_index].push_back(std::move(tracker));
    return make_list(std::move(tiers));
}

Result<TrackerList> TrackerEngine::add_tier(const TrackerList& trackers, const TrackerTier& tier)
{
    auto tiers = copy_tiers(trackers);
    tiers.push_back(tier.trackers());
    return make_list(std::move(tiers));
}

Result<TrackerList> TrackerEngine::remove_tracker(const TrackerList& trackers,
                                                  const std::size_t tier_index,
                                                  const std::size_t tracker_index)
{
    auto tiers = copy_tiers(trackers);
    if (tier_index >= tiers.size())
    {
        return invalid_tier_index(tier_index);
    }
    if (tracker_index >= tiers[tier_index].size())
    {
        return invalid_tracker_index(tier_index, tracker_index);
    }
    tiers[tier_index].erase(tiers[tier_index].begin() + static_cast<std::ptrdiff_t>(tracker_index));
    if (tiers[tier_index].empty())
    {
        tiers.erase(tiers.begin() + static_cast<std::ptrdiff_t>(tier_index));
    }
    return make_list(std::move(tiers));
}

Result<TrackerList> TrackerEngine::move_tracker(const TrackerList& trackers,
                                                const std::size_t source_tier_index,
                                                const std::size_t source_tracker_index,
                                                const std::size_t destination_tier_index)
{
    auto tiers = copy_tiers(trackers);
    if (source_tier_index >= tiers.size())
    {
        return invalid_tier_index(source_tier_index);
    }
    if (destination_tier_index >= tiers.size())
    {
        return invalid_tier_index(destination_tier_index);
    }
    if (source_tracker_index >= tiers[source_tier_index].size())
    {
        return invalid_tracker_index(source_tier_index, source_tracker_index);
    }
    if (source_tier_index == destination_tier_index)
    {
        return Result<TrackerList>::failure(validation_error(
            "tracker.move", "use move_tracker_within_tier to reorder trackers in the same tier"));
    }

    auto tracker = std::move(tiers[source_tier_index][source_tracker_index]);
    tiers[source_tier_index].erase(tiers[source_tier_index].begin() +
                                   static_cast<std::ptrdiff_t>(source_tracker_index));

    auto destination_index = destination_tier_index;
    if (tiers[source_tier_index].empty())
    {
        tiers.erase(tiers.begin() + static_cast<std::ptrdiff_t>(source_tier_index));
        if (source_tier_index < destination_tier_index)
        {
            --destination_index;
        }
    }
    tiers[destination_index].push_back(std::move(tracker));
    return make_list(std::move(tiers));
}

Result<TrackerList> TrackerEngine::move_tracker_within_tier(const TrackerList& trackers,
                                                            const std::size_t tier_index,
                                                            const std::size_t source_tracker_index,
                                                            const std::size_t destination_index)
{
    auto tiers = copy_tiers(trackers);
    if (tier_index >= tiers.size())
    {
        return invalid_tier_index(tier_index);
    }
    auto& tier = tiers[tier_index];
    if (source_tracker_index >= tier.size())
    {
        return invalid_tracker_index(tier_index, source_tracker_index);
    }
    auto tracker = std::move(tier[source_tracker_index]);
    tier.erase(tier.begin() + static_cast<std::ptrdiff_t>(source_tracker_index));
    if (destination_index > tier.size())
    {
        return Result<TrackerList>::failure(
            validation_error("tracker.tiers[" + std::to_string(tier_index) + "].destination",
                             "destination index is out of range"));
    }
    tier.insert(tier.begin() + static_cast<std::ptrdiff_t>(destination_index), std::move(tracker));
    return make_list(std::move(tiers));
}

Result<TrackerList> TrackerEngine::move_tier(const TrackerList& trackers,
                                             const std::size_t source_tier_index,
                                             const std::size_t destination_index)
{
    auto tiers = copy_tiers(trackers);
    if (source_tier_index >= tiers.size())
    {
        return invalid_tier_index(source_tier_index);
    }
    auto tier = std::move(tiers[source_tier_index]);
    tiers.erase(tiers.begin() + static_cast<std::ptrdiff_t>(source_tier_index));
    if (destination_index > tiers.size())
    {
        return Result<TrackerList>::failure(
            validation_error("tracker.tiers.destination", "destination index is out of range"));
    }
    tiers.insert(tiers.begin() + static_cast<std::ptrdiff_t>(destination_index), std::move(tier));
    return make_list(std::move(tiers));
}

Result<TrackerList> TrackerEngine::replace(TrackerList replacement)
{
    return Result<TrackerList>::success(std::move(replacement));
}

Result<TrackerList> TrackerEngine::import_text(const std::string_view input)
{
    if (input.size() > kMaximumImportBytes)
    {
        return Result<TrackerList>::failure(
            validation_error("tracker.text", "input exceeds the 1 MiB limit"));
    }

    std::vector<FieldIssue> issues;
    if (!is_valid_utf8(input))
    {
        issues.push_back({"tracker.text", "input must be valid UTF-8"});
    }

    TrackerVectors tiers(1);
    std::size_t tier_index = 0;
    std::size_t line_number = 0;
    std::size_t start = 0;
    while (start <= input.size())
    {
        const auto end = input.find('\n', start);
        const auto length = end == std::string_view::npos ? input.size() - start : end - start;
        const auto line = trim_ascii(input.substr(start, length));
        if (line.empty())
        {
            ++tier_index;
            if (tier_index == tiers.size())
            {
                tiers.emplace_back();
            }
        }
        else if (line.front() != '#')
        {
            auto tracker = TrackerUrl::parse(std::string(line));
            if (!tracker)
            {
                issues.push_back(
                    {text_line_field(line_number), tracker_validation_message(tracker.error())});
            }
            else
            {
                tiers[tier_index].push_back(std::move(tracker).value());
            }
        }

        ++line_number;
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }

    if (!issues.empty())
    {
        return Result<TrackerList>::failure(validation_errors(std::move(issues)));
    }
    return make_list(std::move(tiers));
}

std::string TrackerEngine::export_text(const TrackerList& trackers)
{
    std::ostringstream output;
    for (std::size_t tier_index = 0; tier_index < trackers.tiers().size(); ++tier_index)
    {
        if (tier_index != 0)
        {
            output << '\n';
        }
        for (const auto& tracker : trackers.tiers()[tier_index].trackers())
        {
            output << tracker.value() << '\n';
        }
    }
    return output.str();
}

Result<TrackerList> TrackerEngine::import_json(const std::string_view input)
{
    if (input.size() > kMaximumImportBytes)
    {
        return Result<TrackerList>::failure(
            validation_error("tracker.json", "input exceeds the 1 MiB limit"));
    }

    bool exceeded_depth = false;
    std::unordered_set<std::string> root_fields;
    std::vector<FieldIssue> parse_issues;
    auto document = nlohmann::json::parse(
        input.begin(), input.end(),
        [&exceeded_depth, &root_fields, &parse_issues](
            const int depth, const nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
            if (depth > kMaximumJsonDepth)
            {
                exceeded_depth = true;
                return false;
            }
            if (depth == 1 && event == nlohmann::json::parse_event_t::key)
            {
                const auto& field = parsed.get_ref<const nlohmann::json::string_t&>();
                if (!root_fields.insert(field).second)
                {
                    parse_issues.push_back(
                        {"tracker.json." + field, "must not appear more than once"});
                }
            }
            return true;
        },
        false);
    if (document.is_discarded())
    {
        return Result<TrackerList>::failure(
            validation_error("tracker.json", "invalid JSON syntax"));
    }
    if (exceeded_depth)
    {
        return Result<TrackerList>::failure(
            validation_error("tracker.json", "JSON nesting exceeds the limit of 64"));
    }

    std::vector<FieldIssue> issues = std::move(parse_issues);
    if (!document.is_object())
    {
        return Result<TrackerList>::failure(
            validation_error("tracker.json", "root must be an object"));
    }

    for (auto iterator = document.cbegin(); iterator != document.cend(); ++iterator)
    {
        if (iterator.key() != "format" && iterator.key() != "tiers")
        {
            issues.push_back({"tracker.json." + iterator.key(), "is not supported by v1"});
        }
    }

    const auto format = document.find("format");
    if (format == document.end())
    {
        issues.push_back({"tracker.json.format", "is required"});
    }
    else if (!format->is_string())
    {
        issues.push_back({"tracker.json.format", "must be a string"});
    }
    else if (format->get<std::string>() != kJsonFormat)
    {
        issues.push_back({"tracker.json.format", "must equal torrentutils.tracker-list/v1"});
    }

    TrackerVectors tiers;
    const auto tier_values = document.find("tiers");
    if (tier_values == document.end())
    {
        issues.push_back({"tracker.json.tiers", "is required"});
    }
    else if (!tier_values->is_array())
    {
        issues.push_back({"tracker.json.tiers", "must be an array"});
    }
    else
    {
        tiers.resize(tier_values->size());
        for (std::size_t tier_index = 0; tier_index < tier_values->size(); ++tier_index)
        {
            const auto& tier = (*tier_values)[tier_index];
            if (!tier.is_array())
            {
                issues.push_back(
                    {"tracker.json.tiers[" + std::to_string(tier_index) + "]", "must be an array"});
                continue;
            }
            if (tier.empty())
            {
                issues.push_back({"tracker.json.tiers[" + std::to_string(tier_index) + "]",
                                  "must not be empty"});
                continue;
            }
            for (std::size_t tracker_index = 0; tracker_index < tier.size(); ++tracker_index)
            {
                const auto& value = tier[tracker_index];
                if (!value.is_string())
                {
                    issues.push_back(
                        {json_tracker_field(tier_index, tracker_index), "must be a string"});
                    continue;
                }
                auto tracker = TrackerUrl::parse(value.get<std::string>());
                if (!tracker)
                {
                    issues.push_back({json_tracker_field(tier_index, tracker_index),
                                      tracker_validation_message(tracker.error())});
                    continue;
                }
                tiers[tier_index].push_back(std::move(tracker).value());
            }
        }
    }

    if (!issues.empty())
    {
        return Result<TrackerList>::failure(validation_errors(std::move(issues)));
    }
    return make_list(std::move(tiers));
}

std::string TrackerEngine::export_json(const TrackerList& trackers)
{
    nlohmann::json tiers = nlohmann::json::array();
    for (const auto& tier : trackers.tiers())
    {
        nlohmann::json values = nlohmann::json::array();
        for (const auto& tracker : tier.trackers())
        {
            values.push_back(tracker.value());
        }
        tiers.push_back(std::move(values));
    }

    nlohmann::json document;
    document["format"] = kJsonFormat;
    document["tiers"] = std::move(tiers);
    return document.dump();
}

} // namespace torrentutils::core
