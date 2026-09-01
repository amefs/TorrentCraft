#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <torrentutils/core/tracker_engine.hpp>
#include <utility>
#include <vector>

namespace {

using torrentutils::core::ErrorCode;
using torrentutils::core::TrackerEngine;
using torrentutils::core::TrackerList;
using torrentutils::core::TrackerTier;
using torrentutils::core::TrackerUrl;

[[nodiscard]] TrackerUrl tracker(const std::string& value)
{
    auto parsed = TrackerUrl::parse(value);
    REQUIRE(parsed);
    return std::move(parsed).value();
}

[[nodiscard]] TrackerTier tier(std::initializer_list<std::string> values)
{
    std::vector<TrackerUrl> trackers;
    trackers.reserve(values.size());
    for (const auto& value : values)
    {
        trackers.push_back(tracker(value));
    }
    auto result = TrackerTier::create(std::move(trackers));
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] TrackerList list(std::initializer_list<TrackerTier> tiers)
{
    auto result = TrackerList::create({tiers});
    REQUIRE(result);
    return std::move(result).value();
}

[[nodiscard]] const std::string& endpoint(const TrackerList& trackers, const std::size_t tier_index,
                                          const std::size_t tracker_index)
{
    return trackers.tiers()[tier_index].trackers()[tracker_index].value();
}

[[nodiscard]] std::string fixture(const std::string& name)
{
    const auto path =
        std::filesystem::path(TORRENTUTILS_TEST_SOURCE_DIR) / "fixtures" / "tracker" / name;
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::string value{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    return value;
}

} // namespace

TEST_CASE("given_tracker_edits_when_applied_then_original_is_unchanged_and_result_is_normalized",
          "[unit][tracker]")
{
    const auto original =
        list({tier({"https://first.example/announce", "https://second.example/announce"}),
              tier({"udp://third.example:6969/announce"})});

    auto appended =
        TrackerEngine::add_to_tier(original, 0, tracker("HTTPS://FIRST.EXAMPLE:443/announce"));
    REQUIRE(appended);
    REQUIRE(original.tiers()[0].trackers().size() == 2);
    REQUIRE(appended.value().tiers()[0].trackers().size() == 2);

    auto moved = TrackerEngine::move_tracker(appended.value(), 0, 1, 1);
    REQUIRE(moved);
    REQUIRE(moved.value().tiers().size() == 2);
    REQUIRE(endpoint(moved.value(), 0, 0) == "https://first.example/announce");
    REQUIRE(endpoint(moved.value(), 1, 1) == "https://second.example/announce");

    auto removed = TrackerEngine::remove_tracker(moved.value(), 0, 0);
    REQUIRE(removed);
    REQUIRE(removed.value().tiers().size() == 1);
    REQUIRE(removed.value().tiers()[0].trackers().size() == 2);
}

TEST_CASE(
    "given_tier_addition_and_singleton_cross_tier_move_when_applied_then_indices_are_adjusted",
    "[unit][tracker]")
{
    const auto empty = list({});
    auto added = TrackerEngine::add_tier(empty, tier({"https://one.example/a"}));
    REQUIRE(added);
    REQUIRE(added.value().tiers().size() == 1);
    REQUIRE(endpoint(added.value(), 0, 0) == "https://one.example/a");

    const auto original =
        list({tier({"https://move.example/a"}), tier({"https://middle.example/a"}),
              tier({"https://target.example/a"})});
    auto moved = TrackerEngine::move_tracker(original, 0, 0, 2);
    REQUIRE(moved);
    REQUIRE(moved.value().tiers().size() == 2);
    REQUIRE(endpoint(moved.value(), 0, 0) == "https://middle.example/a");
    REQUIRE(endpoint(moved.value(), 1, 0) == "https://target.example/a");
    REQUIRE(endpoint(moved.value(), 1, 1) == "https://move.example/a");
}

TEST_CASE("given_tracker_reorder_when_indices_are_valid_then_order_is_deterministic",
          "[unit][tracker]")
{
    const auto original =
        list({tier({"https://one.example/a", "https://two.example/a", "https://three.example/a"}),
              tier({"udp://four.example:6969/a"}), tier({"https://five.example/a"})});

    auto reordered = TrackerEngine::move_tracker_within_tier(original, 0, 0, 2);
    REQUIRE(reordered);
    REQUIRE(endpoint(reordered.value(), 0, 0) == "https://two.example/a");
    REQUIRE(endpoint(reordered.value(), 0, 2) == "https://one.example/a");

    auto tiers = TrackerEngine::move_tier(reordered.value(), 0, 2);
    REQUIRE(tiers);
    REQUIRE(endpoint(tiers.value(), 0, 0) == "udp://four.example:6969/a");
    REQUIRE(endpoint(tiers.value(), 2, 0) == "https://two.example/a");
}

TEST_CASE("given_invalid_edit_indices_when_applied_then_validation_failure_is_returned",
          "[unit][tracker]")
{
    const auto original = list({tier({"https://one.example/a"})});

    const auto bad_tier = TrackerEngine::add_to_tier(original, 1, tracker("https://two.example/a"));
    REQUIRE_FALSE(bad_tier);
    REQUIRE(bad_tier.error().code == ErrorCode::ValidationFailed);

    const auto bad_tracker = TrackerEngine::remove_tracker(original, 0, 1);
    REQUIRE_FALSE(bad_tracker);
    REQUIRE(bad_tracker.error().code == ErrorCode::ValidationFailed);

    const auto same_tier = TrackerEngine::move_tracker(original, 0, 0, 0);
    REQUIRE_FALSE(same_tier);
    REQUIRE(same_tier.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE(
    "given_qbittorrent_style_text_when_imported_then_tiers_comments_and_whitespace_are_handled",
    "[unit][tracker]")
{
    auto input = fixture("qbittorrent-import.txt");
    constexpr std::string_view kFirstEndpoint = "https://one.example/announce";
    const auto first_endpoint = input.find(kFirstEndpoint);
    REQUIRE(first_endpoint != std::string::npos);
    input.insert(first_endpoint + kFirstEndpoint.size(), " \t");

    auto imported = TrackerEngine::import_text(input);
    REQUIRE(imported);
    REQUIRE(imported.value().tiers().size() == 2);
    REQUIRE(endpoint(imported.value(), 0, 0) == "https://one.example/announce");
    REQUIRE(endpoint(imported.value(), 1, 0) == "udp://two.example:6969/announce");
    REQUIRE(endpoint(imported.value(), 1, 1) == "https://three.example/announce");
    REQUIRE(TrackerEngine::export_text(imported.value()) == fixture("canonical.txt"));
    REQUIRE(TrackerEngine::export_json(imported.value()) == fixture("canonical.json"));
}

TEST_CASE("given_invalid_text_when_imported_then_all_discoverable_issues_are_reported_atomically",
          "[unit][tracker]")
{
    auto imported = TrackerEngine::import_text("ftp://unsupported.example/a\nnot-a-url\n");
    REQUIRE_FALSE(imported);
    REQUIRE(imported.error().code == ErrorCode::ValidationFailed);
    REQUIRE(imported.error().issues.size() == 2);
    REQUIRE(imported.error().issues[0].field == "tracker.text.lines[0]");
    REQUIRE(imported.error().issues[1].field == "tracker.text.lines[1]");
}

TEST_CASE("given_invalid_utf8_text_when_imported_then_validation_failure_is_returned",
          "[unit][tracker]")
{
    std::string input = "https://tracker.example/";
    input.push_back(static_cast<char>(0xff));

    auto imported = TrackerEngine::import_text(input);
    REQUIRE_FALSE(imported);
    REQUIRE(imported.error().code == ErrorCode::ValidationFailed);
    REQUIRE(imported.error().issues.front().field == "tracker.text");
    REQUIRE(imported.error().issues.front().message == "input must be valid UTF-8");
}

TEST_CASE("given_text_input_limits_when_imported_then_exact_limit_succeeds_and_over_limit_fails",
          "[unit][tracker]")
{
    std::string at_limit(1024U * 1024U - 2U, 'x');
    at_limit.insert(at_limit.begin(), '#');
    at_limit.push_back('\n');
    REQUIRE(at_limit.size() == std::size_t{1024U} * 1024U);
    auto accepted = TrackerEngine::import_text(at_limit);
    REQUIRE(accepted);
    REQUIRE(accepted.value().tiers().empty());

    at_limit.push_back('#');
    auto rejected = TrackerEngine::import_text(at_limit);
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("given_strict_json_when_imported_then_compact_json_round_trips", "[unit][tracker]")
{
    const std::string input =
        R"({"format":"torrentutils.tracker-list/v1","tiers":[["https://one.example/a?x=%2F"],["udp://two.example:6969/a"]]})";

    auto imported = TrackerEngine::import_json(input);
    REQUIRE(imported);
    REQUIRE(TrackerEngine::export_json(imported.value()) == input);
}

TEST_CASE("given_invalid_json_schema_when_imported_then_all_schema_and_url_issues_are_reported",
          "[unit][tracker]")
{
    const std::string input =
        R"({"format":"wrong","extra":true,"tiers":[[],["ftp://bad.example/a",7]]})";
    auto imported = TrackerEngine::import_json(input);

    REQUIRE_FALSE(imported);
    REQUIRE(imported.error().code == ErrorCode::ValidationFailed);
    REQUIRE(imported.error().issues.size() == 5);
    REQUIRE(imported.error().issues[0].field == "tracker.json.extra");
    REQUIRE(imported.error().issues[1].field == "tracker.json.format");
    REQUIRE(imported.error().issues[2].field == "tracker.json.tiers[0]");
    REQUIRE(imported.error().issues[3].field == "tracker.json.tiers[1][0]");
    REQUIRE(imported.error().issues[4].field == "tracker.json.tiers[1][1]");
}

TEST_CASE("given_json_resource_and_syntax_boundaries_when_imported_then_limits_are_enforced",
          "[unit][tracker]")
{
    auto syntax = TrackerEngine::import_json("{");
    REQUIRE_FALSE(syntax);
    REQUIRE(syntax.error().code == ErrorCode::ValidationFailed);
    REQUIRE(syntax.error().message == "invalid JSON syntax");

    std::string at_depth_limit(64, '[');
    at_depth_limit += "0";
    at_depth_limit.append(64, ']');
    auto accepted_depth = TrackerEngine::import_json(at_depth_limit);
    REQUIRE_FALSE(accepted_depth);
    REQUIRE(accepted_depth.error().message == "root must be an object");

    std::string over_depth_limit(65, '[');
    over_depth_limit += "0";
    over_depth_limit.append(65, ']');
    auto rejected_depth = TrackerEngine::import_json(over_depth_limit);
    REQUIRE_FALSE(rejected_depth);
    REQUIRE(rejected_depth.error().message == "JSON nesting exceeds the limit of 64");

    const std::string prefix =
        R"({"format":"torrentutils.tracker-list/v1","tiers":[["https://tracker.example/)";
    const std::string suffix = R"("]]})";
    std::string at_size_limit = prefix;
    at_size_limit.append(std::size_t{1024U} * 1024U - prefix.size() - suffix.size(), 'a');
    at_size_limit += suffix;
    REQUIRE(at_size_limit.size() == std::size_t{1024U} * 1024U);
    auto accepted_size = TrackerEngine::import_json(at_size_limit);
    REQUIRE(accepted_size);

    at_size_limit.push_back(' ');
    auto rejected_size = TrackerEngine::import_json(at_size_limit);
    REQUIRE_FALSE(rejected_size);
    REQUIRE(rejected_size.error().code == ErrorCode::ValidationFailed);
    REQUIRE(rejected_size.error().message == "input exceeds the 1 MiB limit");
}

TEST_CASE("given_invalid_utf8_json_when_imported_then_stable_validation_failure_is_returned",
          "[unit][tracker]")
{
    std::string input =
        R"({"format":"torrentutils.tracker-list/v1","tiers":[["https://tracker.example/)";
    input.push_back(static_cast<char>(0xff));
    input += R"("]]})";

    auto imported = TrackerEngine::import_json(input);
    REQUIRE_FALSE(imported);
    REQUIRE(imported.error().code == ErrorCode::ValidationFailed);
    REQUIRE(imported.error().message == "invalid JSON syntax");
    REQUIRE(imported.error().issues.size() == 1);
    REQUIRE(imported.error().issues.front().field == "tracker.json");
}

TEST_CASE("given_duplicate_json_fields_when_imported_then_each_duplicate_is_rejected",
          "[unit][tracker]")
{
    const std::string input =
        R"({"format":"wrong","format":"torrentutils.tracker-list/v1","tiers":[["ftp://bad.example/a"]],"tiers":[],"extension":1,"extension":2})";
    auto imported = TrackerEngine::import_json(input);

    REQUIRE_FALSE(imported);
    REQUIRE(imported.error().code == ErrorCode::ValidationFailed);
    REQUIRE(imported.error().issues.size() == 4);
    REQUIRE(imported.error().issues[0].field == "tracker.json.format");
    REQUIRE(imported.error().issues[0].message == "must not appear more than once");
    REQUIRE(imported.error().issues[1].field == "tracker.json.tiers");
    REQUIRE(imported.error().issues[1].message == "must not appear more than once");
    REQUIRE(imported.error().issues[2].field == "tracker.json.extension");
    REQUIRE(imported.error().issues[2].message == "must not appear more than once");
    REQUIRE(imported.error().issues[3].field == "tracker.json.extension");
    REQUIRE(imported.error().issues[3].message == "is not supported by v1");
}

TEST_CASE("given_tracker_replacement_when_applied_then_candidate_is_returned", "[unit][tracker]")
{
    const auto replacement = list({tier({"https://replacement.example/a"})});
    auto result = TrackerEngine::replace(replacement);
    REQUIRE(result);
    REQUIRE(endpoint(result.value(), 0, 0) == "https://replacement.example/a");
}
