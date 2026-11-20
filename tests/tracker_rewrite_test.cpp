#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <torrentutils/core/tracker_rewrite.hpp>
#include <utility>
#include <vector>

namespace {

using torrentutils::core::ErrorCode;
using torrentutils::core::kMaximumTrackerRewriteRuleBytes;
using torrentutils::core::kMaximumTrackerRewriteRules;
using torrentutils::core::preview_tracker_rewrite;
using torrentutils::core::rewrite_trackers;
using torrentutils::core::TrackerList;
using torrentutils::core::TrackerRewriteRules;
using torrentutils::core::TrackerRewriteRuleSpec;
using torrentutils::core::TrackerRewriteStatus;
using torrentutils::core::TrackerRewriteSyntax;
using torrentutils::core::TrackerTier;
using torrentutils::core::TrackerUrl;

using Tiers = std::vector<std::vector<std::string>>;

[[nodiscard]] TrackerRewriteRules rules(const std::vector<std::string>& expressions)
{
    auto parsed = TrackerRewriteRules::parse(expressions);
    if (!parsed)
    {
        throw std::runtime_error("rules failed to parse: " + parsed.error().message);
    }
    return std::move(parsed).value();
}

[[nodiscard]] TrackerList list(const Tiers& tiers)
{
    std::vector<TrackerTier> result;
    result.reserve(tiers.size());
    for (const auto& urls : tiers)
    {
        std::vector<TrackerUrl> trackers;
        trackers.reserve(urls.size());
        for (const auto& url : urls)
        {
            trackers.push_back(TrackerUrl::parse(url).value());
        }
        result.push_back(TrackerTier::create(std::move(trackers)).value());
    }
    return TrackerList::create(std::move(result)).value();
}

[[nodiscard]] Tiers values(const TrackerList& trackers)
{
    Tiers result;
    for (const auto& tier : trackers.tiers())
    {
        auto& urls = result.emplace_back();
        for (const auto& tracker : tier.trackers())
        {
            urls.push_back(tracker.value());
        }
    }
    return result;
}

[[nodiscard]] const TrackerList& require_result(const std::optional<TrackerList>& result)
{
    if (!result)
    {
        throw std::logic_error("expected a valid rewrite result");
    }
    return *result;
}

[[nodiscard]] std::string apply(const std::string& expression, const std::string& value)
{
    return rules({expression}).apply(value);
}

TEST_CASE("given_domain_change_rule_when_rewritten_then_passkey_is_preserved",
          "[unit][tracker][rewrite]")
{
    const auto trackers = list({{"https://tracker.old.org/announce/abc123?passkey=XYZ",
                                 "udp://tracker.old.org:6969/announce"},
                                {"https://backup.example/announce/abc123"}});

    const auto result = rewrite_trackers(
        trackers, rules({R"(s#^(https?://)tracker\.old\.org(/.*)$#\1tracker.new.org\2#)"}));

    REQUIRE(result);
    CHECK(values(result.value()) == Tiers{{"https://tracker.new.org/announce/abc123?passkey=XYZ",
                                           "udp://tracker.old.org:6969/announce"},
                                          {"https://backup.example/announce/abc123"}});
}

TEST_CASE("given_sed_replacement_tokens_when_applied_then_groups_and_literals_are_inserted",
          "[unit][tracker][rewrite]")
{
    CHECK(apply("s/old/[&]/", "https://old.example") == "https://[old].example");
    CHECK(apply(R"(s/old/\0-\0/)", "https://old.example") == "https://old-old.example");
    CHECK(apply(R"(s/(o)(l)d/\2\1/)", "https://old.example") == "https://lo.example");
    CHECK(apply(R"(s/old/a\&b/)", "https://old.example") == "https://a&b.example");
    CHECK(apply(R"(s/old/a\\b/)", "https://old.example") == R"(https://a\b.example)");
    CHECK(apply(R"(s/(?P<host>old)/new-\1/)", "https://old.example") == "https://new-old.example");
    CHECK(apply(R"(s/(?:o|x)ld/new/)", "https://old.example") == "https://new.example");
}

TEST_CASE("given_any_delimiter_when_escaped_then_it_is_literal_in_pattern_and_replacement",
          "[unit][tracker][rewrite]")
{
    CHECK(apply(R"(s|a\|b|x|)", "https://a|b") == "https://x");
    CHECK(apply(R"(s#example#a\#b#)", "https://example") == "https://a#b");
    CHECK(apply(R"(s/\/announce/\/scrape/)", "https://t.example/announce") ==
          "https://t.example/scrape");
}

TEST_CASE("given_flags_when_applied_then_global_and_case_insensitive_are_honoured",
          "[unit][tracker][rewrite]")
{
    CHECK(apply("s/a/x/", "https://a.a.a") == "https://x.a.a");
    CHECK(apply("s/a/x/g", "https://a.a.a") == "https://x.x.x");
    CHECK(apply("s/OLD/new/", "https://old.example") == "https://old.example");
    CHECK(apply("s/OLD/new/i", "https://old.example") == "https://new.example");
    CHECK(apply("s/A/x/gi", "https://a.A.a") == "https://x.x.x");
    CHECK(apply("s/(?i)OLD/new/", "https://old.example") == "https://new.example");
}

TEST_CASE("given_several_rules_when_applied_then_each_uses_the_previous_result",
          "[unit][tracker][rewrite]")
{
    const auto chained = rules({"s#http://#https://#", "s#https://old#https://new#"});

    CHECK(chained.size() == 2U);
    CHECK(chained.apply("http://old.example/announce") == "https://new.example/announce");
}

TEST_CASE("given_malformed_rules_when_parsed_then_errors_name_only_the_rule_index",
          "[unit][tracker][rewrite]")
{
    const std::vector<std::string> malformed{"s/secret/x",    "x/secret/y/",   "s\\secret\\y\\",
                                             "s/secret/y/q",  "s/secret/y/gg", "s/(secret/y/",
                                             "s/secret/\\2/", "s/secret/\\n/", "s//y/",
                                             "s/secret/y/\\", "sasecretayaa"};
    for (const auto& expression : malformed)
    {
        const auto parsed = TrackerRewriteRules::parse({"s/ok/fine/", expression});
        INFO(expression);
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().code == ErrorCode::ValidationFailed);
        REQUIRE(parsed.error().issues.size() == 1U);
        CHECK(parsed.error().issues.front().field == "rule[1]");
        CHECK(parsed.error().message.find("secret") == std::string::npos);
        CHECK(parsed.error().issues.front().message.find("secret") == std::string::npos);
    }

    REQUIRE_FALSE(TrackerRewriteRules::parse({}));
}

TEST_CASE("given_oversized_rules_when_parsed_then_resource_limits_apply",
          "[unit][tracker][rewrite]")
{
    const std::vector<std::string> too_many(kMaximumTrackerRewriteRules + 1U, "s/a/b/");
    const auto many = TrackerRewriteRules::parse(too_many);
    REQUIRE_FALSE(many);
    CHECK(many.error().code == ErrorCode::ResourceLimitExceeded);

    const auto long_rule = TrackerRewriteRules::parse(
        {"s/" + std::string(kMaximumTrackerRewriteRuleBytes, 'a') + "/b/"});
    REQUIRE_FALSE(long_rule);
    CHECK(long_rule.error().code == ErrorCode::ResourceLimitExceeded);
}

TEST_CASE("given_invalid_rewritten_url_when_previewed_then_list_is_rejected_with_reason",
          "[unit][tracker][rewrite]")
{
    const auto trackers = list({{"https://old.example/a"}, {"https://other.example/b"}});
    const auto rewrite = rules({"s#https://old#ftp://old#"});

    const auto preview = preview_tracker_rewrite(trackers, rewrite);

    CHECK_FALSE(preview.valid());
    REQUIRE(preview.changes.size() == 2U);
    CHECK(preview.changes[0].status == TrackerRewriteStatus::Invalid);
    CHECK_FALSE(preview.changes[0].reason.empty());
    CHECK(preview.changes[1].status == TrackerRewriteStatus::Unchanged);

    const auto result = rewrite_trackers(trackers, rewrite);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::ValidationFailed);
    REQUIRE(result.error().issues.size() == 1U);
    CHECK(result.error().issues.front().field == "tracker.tiers[0].trackers[0]");
}

TEST_CASE("given_rewrite_collapsing_a_tier_when_previewed_then_duplicates_are_merged",
          "[unit][tracker][rewrite]")
{
    const auto trackers = list(
        {{"https://a.example/announce", "https://b.example/announce", "https://c.example/announce"},
         {"https://a.example/announce"}});

    const auto preview = preview_tracker_rewrite(trackers, rules({"s#//[ab]\\.#//x.#"}));

    REQUIRE(preview.result.has_value());
    CHECK(preview.changed());
    CHECK(values(require_result(preview.result)) ==
          Tiers{{"https://x.example/announce", "https://c.example/announce"},
                {"https://x.example/announce"}});
    REQUIRE(preview.changes.size() == 4U);
    CHECK(preview.changes[0].status == TrackerRewriteStatus::Changed);
    CHECK(preview.changes[1].status == TrackerRewriteStatus::Merged);
    CHECK(preview.changes[2].status == TrackerRewriteStatus::Unchanged);
    CHECK(preview.changes[3].status == TrackerRewriteStatus::Changed);
    CHECK(preview.count(TrackerRewriteStatus::Merged) == 1U);
}

TEST_CASE("given_rule_without_matches_when_previewed_then_nothing_changes",
          "[unit][tracker][rewrite]")
{
    const auto trackers = list({{"https://a.example/announce"}});

    const auto preview = preview_tracker_rewrite(trackers, rules({"s/nomatch/x/g"}));

    REQUIRE(preview.result.has_value());
    CHECK_FALSE(preview.changed());
    CHECK(values(require_result(preview.result)) == values(trackers));

    const auto empty = preview_tracker_rewrite(TrackerList::create({}).value(), rules({"s/a/b/"}));
    REQUIRE(empty.valid());
    CHECK(empty.changes.empty());
}

[[nodiscard]] std::string first_issue(const std::vector<std::string>& expressions)
{
    const auto parsed = TrackerRewriteRules::parse(expressions);
    if (parsed)
    {
        throw std::logic_error("expected rules to be rejected");
    }
    return parsed.error().issues.empty() ? std::string{} : parsed.error().issues.front().message;
}

TEST_CASE("given_uncompilable_patterns_when_parsed_then_reason_is_categorised_without_text",
          "[unit][tracker][rewrite]")
{
    struct Case
    {
        std::string expression;
        std::string reason;
    };
    const std::vector<Case> cases{
        {R"(s/secret\q/x/)", "bad escape sequence"},
        {R"(s/(secret)\1/x/)", "bad escape sequence"},
        {"s/[secret/x/", "malformed character class"},
        {"s/[z-a]secret/x/", "malformed character class"},
        {"s/[[:secret:]]/x/", "malformed character class"},
        {"s/secret)/x/", "unbalanced parentheses"},
        {"s/(secret/x/", "unbalanced parentheses"},
        {"s/*secret/x/", "invalid repetition"},
        {"s/secret{1001}/x/", "invalid repetition"},
        {"s/secret**/x/", "invalid repetition"},
        {"s/(?=secret)/x/", "unsupported group or assertion syntax"},
        {"s/(?<!secret)/x/", "unsupported group or assertion syntax"},
        {"s/(?P<secret one>a)/x/", "invalid named capture group"},
        {"s/secret\xff/x/", "invalid UTF-8"},
        {R"(s/\pL{1000}\pL{1000}\pL{1000}\pL{1000}secret/x/)", "pattern is too large"},
    };
    for (const auto& test : cases)
    {
        INFO(test.expression);
        const auto parsed = TrackerRewriteRules::parse({test.expression});
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().code == ErrorCode::ValidationFailed);
        REQUIRE(parsed.error().issues.size() == 1U);
        CHECK(parsed.error().issues.front().field == "rule[0]");
        CHECK(parsed.error().issues.front().message.find(test.reason) != std::string::npos);
        CHECK(parsed.error().message.find("secret") == std::string::npos);
        CHECK(parsed.error().issues.front().message.find("secret") == std::string::npos);
    }
}

TEST_CASE("given_rule_syntax_edge_cases_when_parsed_then_each_problem_is_named",
          "[unit][tracker][rewrite]")
{
    CHECK(first_issue({""}).find("must have the form") != std::string::npos);
    CHECK(first_issue({"s/a"}).find("must have the form") != std::string::npos);
    CHECK(first_issue({R"(s/a\)"}).find("lone backslash") != std::string::npos);
    CHECK(first_issue({"s a b "}).find("delimiter") != std::string::npos);
    CHECK(first_issue({"s\x01"
                       "a\x01"
                       "b\x01"})
              .find("delimiter") != std::string::npos);
    CHECK(first_issue({"s\xc3\xa9"
                       "a\xc3\xa9"
                       "b\xc3\xa9"})
              .find("delimiter") != std::string::npos);
    CHECK(first_issue({"s/a/b"}).find("closing delimiter") != std::string::npos);
    CHECK(first_issue({"s///"}).find("empty pattern") != std::string::npos);
    CHECK(first_issue({"s/a/b/gig"}).find("flag") != std::string::npos);
    CHECK(first_issue({"s/a/b/ii"}).find("flag") != std::string::npos);
    CHECK(first_issue({"s/a/b/ G"}).find("flag") != std::string::npos);
    CHECK(first_issue({R"(s/a/\t/)"}).find("escape") != std::string::npos);
    CHECK(first_issue({R"(s/a/\1/)"}).find("capture group") != std::string::npos);
    CHECK(first_issue({R"(s/(a)(b)/\3/)"}).find("capture group") != std::string::npos);

    const auto later = TrackerRewriteRules::parse({"s/a/b/", "s/c/d/", "s/e/f/x"});
    REQUIRE_FALSE(later);
    CHECK(later.error().issues.front().field == "rule[2]");
    CHECK(later.error().message.find("rule[2]") != std::string::npos);

    const auto empty = TrackerRewriteRules::parse({});
    REQUIRE_FALSE(empty);
    CHECK(empty.error().code == ErrorCode::ValidationFailed);
    CHECK(empty.error().issues.front().field == "rule[0]");

    const auto limit =
        TrackerRewriteRules::parse(std::vector<std::string>(kMaximumTrackerRewriteRules, "s/a/b/"));
    CHECK(limit);
    const auto exact = TrackerRewriteRules::parse(
        {"s/" + std::string(kMaximumTrackerRewriteRuleBytes - 5U, 'a') + "/b/"});
    CHECK(exact);
}

TEST_CASE("given_delimiters_that_are_regex_metacharacters_when_escaped_then_they_match_literally",
          "[unit][tracker][rewrite]")
{
    // An escaped delimiter is always literal, while the unescaped character keeps its regex
    // meaning.
    CHECK(apply(R"(s.a\.b.x.)", "https://a.b-axb") == "https://x-axb");
    CHECK(apply(R"(s.a\.b.x.g)", "https://axb") == "https://axb");
    CHECK(apply(R"(s+a\+b+x+)", "https://a+b") == "https://x");
    CHECK(apply(R"(s(a\(b(x()", "https://a(b") == "https://x");
    CHECK(apply(R"(s$a\$$x$)", "https://a$") == "https://x");
    CHECK(apply(R"(s^\^a^x^)", "https://^a") == "https://x");
    CHECK(apply(R"(s,a\,b,x\,y,)", "https://a,b") == "https://x,y");
    CHECK(apply(R"(s#a\#b#x#)", "https://a#b") == "https://x");
    CHECK(apply(R"(s#a\.b#x#)", "https://a.b-axb") == "https://x-axb");
    CHECK(apply(R"(s#a\.b#x#)", "https://axb") == "https://axb");
    // An escaped backslash ends with the delimiter instead of escaping it.
    CHECK(apply(R"(s/a\\/x/)", R"(https://a\)") == "https://x");
}

TEST_CASE("given_common_tracker_migrations_when_rewritten_then_urls_match_sed_behaviour",
          "[unit][tracker][rewrite]")
{
    // Upgrade the scheme only at the start of the URL.
    CHECK(apply("s#^http://#https://#", "http://t.example/a?u=http://x") ==
          "https://t.example/a?u=http://x");
    // Append a query parameter with an anchored empty match and a literal ampersand.
    CHECK(apply(R"(s#$#\&ipv6=1#)", "https://t.example/a?passkey=K") ==
          "https://t.example/a?passkey=K&ipv6=1");
    // Replace a passkey value but keep its name through a group.
    CHECK(apply(R"(s#(passkey=)[0-9a-f]+#\1NEW#)", "https://t.example/a?passkey=abc123&x=1") ==
          "https://t.example/a?passkey=NEW&x=1");
    // Swap the host and port order with several groups.
    CHECK(apply(R"(s#^udp://([^:/]+):([0-9]+)#udp://\1.new:\2#)",
                "udp://t.example:6969/announce") == "udp://t.example.new:6969/announce");
    // Remove a path segment with an empty replacement.
    CHECK(apply("s#/legacy##", "https://t.example/legacy/announce") ==
          "https://t.example/announce");
    // Unicode-aware matching treats a multi-byte character as one character.
    CHECK(apply("s/./X/g", "\xc3\xa9t\xc3\xa9") == "XXX");
    CHECK(apply("s/\xc3\xa9/e/gi", "\xc3\x89t\xc3\xa9") == "ete");
    // Global empty matches insert between every character, as sed does.
    CHECK(apply("s/x*/-/g", "ab") == "-a-b-");
    CHECK(apply("s/x*/-/", "ab") == "-ab");
    // Alternation, counted repetition and character classes.
    CHECK(apply("s/(old|legacy)-(tr){1,2}/new/g", "https://old-tr.example/legacy-trtr") ==
          "https://new.example/new");
    CHECK(apply(R"(s/[[:digit:]]+/N/g)", "udp://t1.example:6969") == "udp://tN.example:N");
    CHECK(apply(R"(s/\d+/N/)", "udp://t1.example:6969") == "udp://tN.example:6969");
    // Case-insensitive matching inserts the original spelling through &.
    CHECK(apply("s/TRACKER/[&]/i", "https://Tracker.example") == "https://[Tracker].example");
    // Flags may appear in either order.
    CHECK(apply("s/A/x/ig", "https://a.A") == "https://x.x");
}

TEST_CASE("given_rewrite_onto_a_later_tracker_when_previewed_then_the_later_entry_is_merged",
          "[unit][tracker][rewrite]")
{
    const auto trackers = list({{"https://b.example/announce", "https://a.example/announce",
                                 "https://c.example/announce"}});

    const auto preview = preview_tracker_rewrite(trackers, rules({"s#//b\\.#//a.#"}));

    REQUIRE(preview.valid());
    REQUIRE(preview.changes.size() == 3U);
    CHECK(preview.changes[0].status == TrackerRewriteStatus::Changed);
    CHECK(preview.changes[0].to == "https://a.example/announce");
    CHECK(preview.changes[1].status == TrackerRewriteStatus::Merged);
    CHECK(preview.changes[2].status == TrackerRewriteStatus::Unchanged);
    CHECK(preview.count(TrackerRewriteStatus::Changed) == 1U);
    CHECK(values(require_result(preview.result)) ==
          Tiers{{"https://a.example/announce", "https://c.example/announce"}});
}

TEST_CASE("given_several_invalid_results_when_rewritten_then_every_position_is_reported",
          "[unit][tracker][rewrite]")
{
    const auto trackers =
        list({{"https://ok.example/a", "udp://bad.example:80/a"},
              {"https://ok.example/b"},
              {"https://ok.example/c", "https://ok.example/d", "udp://bad.example:81/b"}});
    const auto rewrite = rules({"s#^udp://#gopher://#"});

    const auto preview = preview_tracker_rewrite(trackers, rewrite);
    CHECK_FALSE(preview.valid());
    CHECK(preview.changed());
    REQUIRE(preview.changes.size() == 6U);
    CHECK(preview.count(TrackerRewriteStatus::Invalid) == 2U);
    CHECK(preview.count(TrackerRewriteStatus::Unchanged) == 4U);
    CHECK(preview.changes[1].to == "gopher://bad.example:80/a");

    const auto result = rewrite_trackers(trackers, rewrite);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().issues.size() == 2U);
    CHECK(result.error().issues[0].field == "tracker.tiers[0].trackers[1]");
    CHECK(result.error().issues[1].field == "tracker.tiers[2].trackers[2]");
    CHECK_FALSE(result.error().issues[1].message.empty());

    // A rewrite that empties a URL is invalid rather than silently dropping the tracker.
    const auto emptied = preview_tracker_rewrite(trackers, rules({"s#.*##"}));
    CHECK_FALSE(emptied.valid());
    CHECK(emptied.count(TrackerRewriteStatus::Invalid) == 6U);
}

TEST_CASE("given_a_rule_growing_past_the_url_limit_when_chained_then_later_rules_are_skipped",
          "[unit][tracker][rewrite]")
{
    const auto growth = rules(
        {"s/a/aaaaaaaa/g", "s/a/aaaaaaaa/g", "s/a/aaaaaaaa/g", "s/a/aaaaaaaa/g", "s/^/never-/"});
    const auto grown = growth.apply("https://aaaa.example");
    CHECK(grown.size() > torrentutils::core::kMaximumRewrittenTrackerUrlBytes);
    CHECK(grown.rfind("never-", 0) != 0U);

    const auto trackers = list({{"https://aaaa.example/announce"}});
    const auto preview = preview_tracker_rewrite(trackers, growth);
    REQUIRE(preview.changes.size() == 1U);
    CHECK(preview.changes[0].status == TrackerRewriteStatus::Invalid);
    CHECK(preview.changes[0].to.empty());
    CHECK(preview.changes[0].reason.find("bytes") != std::string::npos);
}

TEST_CASE("given_runaway_global_rewrite_when_previewed_then_url_length_is_bounded",
          "[unit][tracker][rewrite]")
{
    const auto trackers = list({{"https://" + std::string(200, 'a') + ".example/announce"}});
    const std::vector<std::string> growth(8, "s/a/aaaa/g");

    const auto preview = preview_tracker_rewrite(trackers, rules(growth));

    CHECK_FALSE(preview.valid());
    REQUIRE(preview.changes.size() == 1U);
    CHECK(preview.changes[0].status == TrackerRewriteStatus::Invalid);
}

TEST_CASE("given_overlong_url_when_rewritten_then_rules_are_not_run_on_it",
          "[unit][tracker][rewrite]")
{
    // An empty match with a long replacement would otherwise grow the URL by ~4000x.
    const auto input = "https://a.example/" + std::string(std::size_t{64} * 1024U, 'x');
    const auto runaway = rules({"s/x*/" + std::string(4000, 'y') + "/g"});

    CHECK(runaway.apply(input) == input);
}

[[nodiscard]] TrackerRewriteRuleSpec literal(std::string find, std::string replace)
{
    return {std::move(find), std::move(replace), TrackerRewriteSyntax::Literal, true, true};
}

[[nodiscard]] TrackerRewriteRuleSpec regex(std::string find, std::string replace)
{
    return {std::move(find), std::move(replace), TrackerRewriteSyntax::Regex, true, true};
}

[[nodiscard]] TrackerRewriteRules compiled(const std::vector<TrackerRewriteRuleSpec>& specs)
{
    auto result = TrackerRewriteRules::compile(specs);
    if (!result)
    {
        throw std::runtime_error("rules failed to compile: " + result.error().message);
    }
    return std::move(result).value();
}

[[nodiscard]] std::string compile_issue(const std::vector<TrackerRewriteRuleSpec>& specs)
{
    const auto result = TrackerRewriteRules::compile(specs);
    if (result)
    {
        throw std::runtime_error("rules unexpectedly compiled");
    }
    const auto& issue = result.error().issues.front();
    return issue.field + ": " + issue.message;
}

TEST_CASE("given_literal_rules_when_applied_then_text_matches_and_inserts_verbatim",
          "[unit][tracker][rewrite]")
{
    // Regex metacharacters in find and sed or dollar markers in replace are plain text.
    CHECK(compiled({literal("a.b", "x")}).apply("https://a.b/aXb") == "https://x/aXb");
    CHECK(compiled({literal("(?", R"(\1&$1$$)")}).apply("u(?v") == R"(u\1&$1$$v)");
    CHECK(compiled({literal(R"(\d)", "^")}).apply(R"(1\d2)") == "1^2");
    CHECK(compiled({literal("passkey=", "")}).apply("a?passkey=k") == "a?k");

    // Every match is replaced by default; replace_all off replaces only the first one.
    CHECK(compiled({literal("o", "0")}).apply("foo") == "f00");
    auto first_only = literal("o", "0");
    first_only.replace_all = false;
    CHECK(compiled({first_only}).apply("foo") == "f0o");

    // Match case is on by default; turning it off matches any case.
    CHECK(compiled({literal("ABC", "x")}).apply("abc ABC") == "abc x");
    auto any_case = literal("ABC", "x");
    any_case.match_case = false;
    CHECK(compiled({any_case}).apply("abc ABC") == "x x");
}

TEST_CASE("given_regex_rules_when_applied_then_dollar_references_are_expanded",
          "[unit][tracker][rewrite]")
{
    const auto apply = [](const std::string& find, const std::string& replace,
                          const std::string& value) {
        return compiled({regex(find, replace)}).apply(value);
    };
    CHECK(apply("^udp://(.+):6969", "https://$1", "udp://t.example:6969") == "https://t.example");
    CHECK(apply("t+", "[$0|$&]", "att") == "a[tt|tt]");
    CHECK(apply("(a)(b)", "$2$1", "ab") == "ba");
    CHECK(apply("x", "$$1", "x") == "$1");
    // A $ not followed by a digit, & or $ is literal, as is a backslash.
    CHECK(apply("x", R"($ $a \1 $)", "x") == R"($ $a \1 $)");
    CHECK(apply("(?i)HOST", "h", "Host") == "h");

    auto any_case = regex("old", "new");
    any_case.match_case = false;
    CHECK(compiled({any_case}).apply("OLD.example") == "new.example");
}

TEST_CASE("given_structured_and_sed_rules_when_equivalent_then_results_agree",
          "[unit][tracker][rewrite]")
{
    const auto trackers = list({{"udp://tracker.example:6969/announce?passkey=K1",
                                 "https://tracker.example/announce?passkey=K1"}});
    const auto sed = rules({R"(s#^udp://(tracker\.example):6969#https://\1#)",
                            R"(s#tracker\.example#tracker.new.example#g)"});
    const auto structured = compiled({regex(R"(^udp://(tracker\.example):6969)", "https://$1"),
                                      literal("tracker.example", "tracker.new.example")});

    const auto expected = preview_tracker_rewrite(trackers, sed);
    const auto actual = preview_tracker_rewrite(trackers, structured);
    REQUIRE(expected.result.has_value());
    REQUIRE(actual.result.has_value());
    if (expected.result && actual.result)
    {
        CHECK(values(*actual.result) == values(*expected.result));
    }
    CHECK(actual.count(TrackerRewriteStatus::Merged) == 1U);
}

TEST_CASE("given_unusable_structured_rules_when_compiled_then_the_rule_is_named_without_text",
          "[unit][tracker][rewrite]")
{
    CHECK(TrackerRewriteRules::compile({}).error().code == ErrorCode::ValidationFailed);
    CHECK(compile_issue({literal("a", "b"), literal("", "secret")}) ==
          "rule[1]: needs text to find");
    CHECK(compile_issue({regex("(secret", "x")}) ==
          "rule[0]: has an invalid pattern: unbalanced parentheses");
    CHECK(compile_issue({regex("(secret)", "$7")}) ==
          "rule[0]: replacement refers to a capture group the pattern does not define");
    // A literal find never fails to compile, even with regex syntax in it.
    CHECK(TrackerRewriteRules::compile({literal("(secret", "$7")}));

    const std::string huge(kMaximumTrackerRewriteRuleBytes + 1U, 'x');
    const auto too_long_find = TrackerRewriteRules::compile({literal(huge, "x")});
    CHECK(too_long_find.error().code == ErrorCode::ResourceLimitExceeded);
    const auto too_long_replace = TrackerRewriteRules::compile({literal("x", huge)});
    CHECK(too_long_replace.error().code == ErrorCode::ResourceLimitExceeded);

    const std::vector<TrackerRewriteRuleSpec> too_many(kMaximumTrackerRewriteRules + 1U,
                                                       literal("a", "b"));
    CHECK(TrackerRewriteRules::compile(too_many).error().code == ErrorCode::ResourceLimitExceeded);
    CHECK(compiled(
              std::vector<TrackerRewriteRuleSpec>(kMaximumTrackerRewriteRules, literal("a", "b")))
              .size() == kMaximumTrackerRewriteRules);
}

} // namespace
