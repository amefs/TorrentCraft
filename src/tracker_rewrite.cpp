#include <algorithm>
#include <cctype>
#include <cstdint>
#include <re2/re2.h>
#include <string>
#include <string_view>
#include <torrentutils/core/tracker_rewrite.hpp>
#include <utility>
#include <vector>

namespace torrentutils::core {

namespace {

constexpr std::int64_t kMaximumRuleProgramBytes = std::int64_t{8} * 1024 * 1024;
constexpr std::string_view kRegexMetaCharacters = R"(\.+*?()|[]{}^$)";

[[nodiscard]] std::string rule_field(const std::size_t index)
{
    return "rule[" + std::to_string(index) + "]";
}

[[nodiscard]] Error rule_error(const ErrorCode code, const std::size_t index, std::string message)
{
    Error error;
    error.code = code;
    error.message = "tracker rewrite " + rule_field(index) + " " + message;
    error.issues.push_back({rule_field(index), std::move(message)});
    return error;
}

[[nodiscard]] bool is_valid_delimiter(const char delimiter) noexcept
{
    const auto byte = static_cast<unsigned char>(delimiter);
    return byte > 0x20U && byte < 0x7fU && std::isalnum(byte) == 0 && delimiter != '\\';
}

/** Splits `s<d>pattern<d>replacement<d>flags` into its raw (still escaped) parts. */
struct RawRule
{
    std::string pattern;
    std::string replacement;
    std::string flags;
};

[[nodiscard]] Result<RawRule> split_rule(const std::string& expression, const std::size_t index)
{
    if (expression.size() < 4U || expression.front() != 's')
    {
        return Result<RawRule>::failure(rule_error(
            ErrorCode::ValidationFailed, index,
            "must have the form s<delimiter>pattern<delimiter>replacement<delimiter>flags"));
    }
    const char delimiter = expression[1];
    if (!is_valid_delimiter(delimiter))
    {
        return Result<RawRule>::failure(
            rule_error(ErrorCode::ValidationFailed, index,
                       "delimiter must be a punctuation character other than backslash"));
    }

    std::vector<std::string> parts(1);
    for (std::size_t position = 2; position < expression.size(); ++position)
    {
        const char character = expression[position];
        if (parts.size() == 3U)
        {
            parts.back().push_back(character);
            continue;
        }
        if (character == '\\')
        {
            if (position + 1U >= expression.size())
            {
                return Result<RawRule>::failure(
                    rule_error(ErrorCode::ValidationFailed, index, "ends with a lone backslash"));
            }
            parts.back().push_back(character);
            parts.back().push_back(expression[++position]);
            continue;
        }
        if (character == delimiter)
        {
            parts.emplace_back();
            continue;
        }
        parts.back().push_back(character);
    }
    if (parts.size() != 3U)
    {
        return Result<RawRule>::failure(
            rule_error(ErrorCode::ValidationFailed, index, "is missing a closing delimiter"));
    }
    if (parts[0].empty())
    {
        return Result<RawRule>::failure(
            rule_error(ErrorCode::ValidationFailed, index, "has an empty pattern"));
    }
    return Result<RawRule>::success(
        {std::move(parts[0]), std::move(parts[1]), std::move(parts[2])});
}

/** Converts an escaped delimiter in the pattern into the literal delimiter character. */
[[nodiscard]] std::string translate_pattern(const std::string& pattern, const char delimiter)
{
    std::string result;
    result.reserve(pattern.size());
    for (std::size_t position = 0; position < pattern.size(); ++position)
    {
        if (pattern[position] == '\\' && position + 1U < pattern.size() &&
            pattern[position + 1U] == delimiter)
        {
            if (kRegexMetaCharacters.find(delimiter) != std::string_view::npos)
            {
                result.push_back('\\');
            }
            result.push_back(delimiter);
            ++position;
            continue;
        }
        result.push_back(pattern[position]);
        if (pattern[position] == '\\' && position + 1U < pattern.size())
        {
            result.push_back(pattern[++position]);
        }
    }
    return result;
}

/** Converts a sed replacement into an RE2 rewrite string. */
[[nodiscard]] Result<std::string>
translate_replacement(const std::string& replacement, const char delimiter, const std::size_t index)
{
    std::string result;
    result.reserve(replacement.size() + 4U);
    for (std::size_t position = 0; position < replacement.size(); ++position)
    {
        const char character = replacement[position];
        if (character == '&')
        {
            result += "\\0";
            continue;
        }
        if (character != '\\')
        {
            result.push_back(character);
            continue;
        }
        // split_rule guarantees an escaped character follows every backslash.
        const char escaped = replacement[++position];
        if (std::isdigit(static_cast<unsigned char>(escaped)) != 0)
        {
            result.push_back('\\');
            result.push_back(escaped);
        }
        else if (escaped == '\\')
        {
            result += "\\\\";
        }
        else if (escaped == '&' || escaped == delimiter)
        {
            result.push_back(escaped);
        }
        else
        {
            return Result<std::string>::failure(
                rule_error(ErrorCode::ValidationFailed, index,
                           "replacement contains an unsupported escape sequence"));
        }
    }
    return Result<std::string>::success(std::move(result));
}

/** Describes an RE2 compile failure without echoing pattern text, which may hold a passkey. */
[[nodiscard]] std::string pattern_error_text(const RE2::ErrorCode code)
{
    switch (code)
    {
    case RE2::ErrorBadEscape:
        return "bad escape sequence";
    case RE2::ErrorBadCharClass:
    case RE2::ErrorBadCharRange:
    case RE2::ErrorMissingBracket:
        return "malformed character class";
    case RE2::ErrorMissingParen:
    case RE2::ErrorUnexpectedParen:
        return "unbalanced parentheses";
    case RE2::ErrorTrailingBackslash:
        return "trailing backslash";
    case RE2::ErrorRepeatArgument:
    case RE2::ErrorRepeatSize:
    case RE2::ErrorRepeatOp:
        return "invalid repetition";
    case RE2::ErrorBadPerlOp:
        return "unsupported group or assertion syntax";
    case RE2::ErrorBadUTF8:
        return "invalid UTF-8";
    case RE2::ErrorBadNamedCapture:
        return "invalid named capture group";
    case RE2::ErrorPatternTooLarge:
        return "pattern is too large";
    default:
        return "pattern cannot be compiled";
    }
}

} // namespace

class TrackerRewriteRules::Rule
{
  public:
    Rule(const std::string& pattern, const RE2::Options& options, std::string rewrite, bool global)
        : regex_(pattern, options), rewrite_(std::move(rewrite)), global_(global)
    {
    }

    [[nodiscard]] const RE2& regex() const noexcept
    {
        return regex_;
    }
    [[nodiscard]] const std::string& rewrite() const noexcept
    {
        return rewrite_;
    }

    void apply(std::string& value) const
    {
        if (global_)
        {
            RE2::GlobalReplace(&value, regex_, rewrite_);
        }
        else
        {
            RE2::Replace(&value, regex_, rewrite_);
        }
    }

  private:
    RE2 regex_;
    std::string rewrite_;
    bool global_{};
};

TrackerRewriteRules::TrackerRewriteRules(std::vector<std::shared_ptr<const Rule>> rules)
    : rules_(std::move(rules))
{
}

namespace {

using RulePointer = std::shared_ptr<const TrackerRewriteRules::Rule>;

[[nodiscard]] Result<void> check_rule_count(const std::size_t count)
{
    if (count == 0U)
    {
        return Result<void>::failure(
            rule_error(ErrorCode::ValidationFailed, 0, "is required; at least one rule is needed"));
    }
    if (count > kMaximumTrackerRewriteRules)
    {
        return Result<void>::failure(rule_error(
            ErrorCode::ResourceLimitExceeded, kMaximumTrackerRewriteRules,
            "exceeds the limit of " + std::to_string(kMaximumTrackerRewriteRules) + " rules"));
    }
    return Result<void>::success();
}

[[nodiscard]] Result<void> check_rule_size(const std::size_t size, const std::size_t index)
{
    if (size > kMaximumTrackerRewriteRuleBytes)
    {
        return Result<void>::failure(
            rule_error(ErrorCode::ResourceLimitExceeded, index,
                       "exceeds " + std::to_string(kMaximumTrackerRewriteRuleBytes) + " bytes"));
    }
    return Result<void>::success();
}

/** Compiles one RE2 pattern and rewrite string shared by the sed and structured front ends. */
[[nodiscard]] Result<RulePointer> compile_rule(const std::string& pattern, std::string rewrite,
                                               const bool global, const bool ignore_case,
                                               const std::size_t index)
{
    RE2::Options options;
    options.set_encoding(RE2::Options::EncodingUTF8);
    options.set_log_errors(false);
    options.set_case_sensitive(!ignore_case);
    options.set_max_mem(kMaximumRuleProgramBytes);
    auto rule = std::make_shared<const TrackerRewriteRules::Rule>(pattern, options,
                                                                  std::move(rewrite), global);
    if (!rule->regex().ok())
    {
        return Result<RulePointer>::failure(rule_error(
            ErrorCode::ValidationFailed, index,
            "has an invalid pattern: " + pattern_error_text(rule->regex().error_code())));
    }
    std::string rewrite_error;
    if (!rule->regex().CheckRewriteString(rule->rewrite(), &rewrite_error))
    {
        return Result<RulePointer>::failure(
            rule_error(ErrorCode::ValidationFailed, index,
                       "replacement refers to a capture group the pattern does not define"));
    }
    return Result<RulePointer>::success(std::move(rule));
}

/** Escapes text so that RE2 inserts it literally as a rewrite string. */
[[nodiscard]] std::string literal_rewrite(const std::string& replacement)
{
    std::string result;
    result.reserve(replacement.size());
    for (const char character : replacement)
    {
        if (character == '\\')
        {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    return result;
}

/** Converts a `$1`/`$&`/`$$` replacement into an RE2 rewrite string. */
[[nodiscard]] std::string dollar_rewrite(const std::string& replacement)
{
    std::string result;
    result.reserve(replacement.size() + 4U);
    for (std::size_t position = 0; position < replacement.size(); ++position)
    {
        const char character = replacement[position];
        if (character == '\\')
        {
            result += "\\\\";
            continue;
        }
        const char next = position + 1U < replacement.size() ? replacement[position + 1U] : '\0';
        if (character != '$' ||
            (next != '$' && next != '&' && std::isdigit(static_cast<unsigned char>(next)) == 0))
        {
            result.push_back(character);
            continue;
        }
        ++position;
        if (next == '$')
        {
            result.push_back('$');
        }
        else
        {
            result.push_back('\\');
            result.push_back(next == '&' ? '0' : next);
        }
    }
    return result;
}

} // namespace

Result<TrackerRewriteRules> TrackerRewriteRules::parse(const std::vector<std::string>& expressions)
{
    if (auto count = check_rule_count(expressions.size()); !count)
    {
        return Result<TrackerRewriteRules>::failure(std::move(count).error());
    }

    std::vector<RulePointer> rules;
    rules.reserve(expressions.size());
    for (std::size_t index = 0; index < expressions.size(); ++index)
    {
        const auto& expression = expressions[index];
        if (auto size = check_rule_size(expression.size(), index); !size)
        {
            return Result<TrackerRewriteRules>::failure(std::move(size).error());
        }
        auto raw = split_rule(expression, index);
        if (!raw)
        {
            return Result<TrackerRewriteRules>::failure(std::move(raw).error());
        }
        const auto& parts = raw.value();

        bool global = false;
        bool ignore_case = false;
        for (const char flag : parts.flags)
        {
            bool& target = flag == 'g' ? global : ignore_case;
            if ((flag != 'g' && flag != 'i') || target)
            {
                return Result<TrackerRewriteRules>::failure(
                    rule_error(ErrorCode::ValidationFailed, index,
                               "has an unsupported or repeated flag; only g and i are allowed"));
            }
            target = true;
        }

        const char delimiter = expression[1];
        auto rewrite = translate_replacement(parts.replacement, delimiter, index);
        if (!rewrite)
        {
            return Result<TrackerRewriteRules>::failure(std::move(rewrite).error());
        }
        auto rule = compile_rule(translate_pattern(parts.pattern, delimiter),
                                 std::move(rewrite).value(), global, ignore_case, index);
        if (!rule)
        {
            return Result<TrackerRewriteRules>::failure(std::move(rule).error());
        }
        rules.push_back(std::move(rule).value());
    }
    return Result<TrackerRewriteRules>::success(TrackerRewriteRules(std::move(rules)));
}

Result<TrackerRewriteRules>
TrackerRewriteRules::compile(const std::vector<TrackerRewriteRuleSpec>& specs)
{
    if (auto count = check_rule_count(specs.size()); !count)
    {
        return Result<TrackerRewriteRules>::failure(std::move(count).error());
    }

    std::vector<RulePointer> rules;
    rules.reserve(specs.size());
    for (std::size_t index = 0; index < specs.size(); ++index)
    {
        const auto& spec = specs[index];
        if (spec.find.empty())
        {
            return Result<TrackerRewriteRules>::failure(
                rule_error(ErrorCode::ValidationFailed, index, "needs text to find"));
        }
        if (auto size = check_rule_size(std::max(spec.find.size(), spec.replace.size()), index);
            !size)
        {
            return Result<TrackerRewriteRules>::failure(std::move(size).error());
        }
        const bool regex = spec.syntax == TrackerRewriteSyntax::Regex;
        auto rule =
            compile_rule(regex ? spec.find : RE2::QuoteMeta(spec.find),
                         regex ? dollar_rewrite(spec.replace) : literal_rewrite(spec.replace),
                         spec.replace_all, !spec.match_case, index);
        if (!rule)
        {
            return Result<TrackerRewriteRules>::failure(std::move(rule).error());
        }
        rules.push_back(std::move(rule).value());
    }
    return Result<TrackerRewriteRules>::success(TrackerRewriteRules(std::move(rules)));
}

std::size_t TrackerRewriteRules::size() const noexcept
{
    return rules_.size();
}

std::string TrackerRewriteRules::apply(const std::string& value) const
{
    std::string result = value;
    // An over-long URL is already an invalid result. Running the rules on it first could
    // multiply its size (an empty match with a long replacement on every byte).
    if (result.size() > kMaximumRewrittenTrackerUrlBytes)
    {
        return result;
    }
    for (const auto& rule : rules_)
    {
        rule->apply(result);
        if (result.size() > kMaximumRewrittenTrackerUrlBytes)
        {
            break;
        }
    }
    return result;
}

bool TrackerRewritePreview::valid() const noexcept
{
    return result.has_value();
}

bool TrackerRewritePreview::changed() const noexcept
{
    return std::any_of(changes.begin(), changes.end(), [](const TrackerRewriteChange& change) {
        return change.status != TrackerRewriteStatus::Unchanged;
    });
}

std::size_t TrackerRewritePreview::count(const TrackerRewriteStatus status) const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(changes.begin(), changes.end(), [status](const TrackerRewriteChange& change) {
            return change.status == status;
        }));
}

TrackerRewritePreview preview_tracker_rewrite(const TrackerList& trackers,
                                              const TrackerRewriteRules& rules)
{
    TrackerRewritePreview preview;
    std::vector<TrackerTier> tiers;
    bool valid = true;

    for (std::size_t tier_index = 0; tier_index < trackers.tiers().size(); ++tier_index)
    {
        const auto first_change = preview.changes.size();
        std::vector<TrackerUrl> rewritten;
        const auto& source = trackers.tiers()[tier_index].trackers();
        for (std::size_t index = 0; index < source.size(); ++index)
        {
            TrackerRewriteChange change;
            change.tier = tier_index;
            change.index = index;
            change.from = source[index].value();
            change.to = rules.apply(change.from);
            change.status = change.to == change.from ? TrackerRewriteStatus::Unchanged
                                                     : TrackerRewriteStatus::Changed;
            if (change.to.size() > kMaximumRewrittenTrackerUrlBytes)
            {
                change.status = TrackerRewriteStatus::Invalid;
                change.reason = "exceeds " + std::to_string(kMaximumRewrittenTrackerUrlBytes) +
                                " bytes after rewriting";
                change.to.clear();
            }
            else if (auto url = TrackerUrl::parse(change.to))
            {
                rewritten.push_back(std::move(url).value());
            }
            else
            {
                change.status = TrackerRewriteStatus::Invalid;
                change.reason = url.error().issues.empty() ? url.error().message
                                                           : url.error().issues.front().message;
            }
            valid = valid && change.status != TrackerRewriteStatus::Invalid;
            preview.changes.push_back(std::move(change));
        }
        if (!valid || rewritten.empty())
        {
            continue;
        }

        auto tier = TrackerTier::create(rewritten);
        if (!tier)
        {
            valid = false;
            continue;
        }
        // Tier-local deduplication keeps the first spelling in order, so walking the kept
        // trackers alongside the rewritten ones identifies every merged entry.
        const auto& kept = tier.value().trackers();
        std::size_t kept_index = 0;
        for (auto position = first_change; position < preview.changes.size(); ++position)
        {
            auto& change = preview.changes[position];
            if (kept_index < kept.size() && kept[kept_index].value() == change.to)
            {
                ++kept_index;
            }
            else
            {
                change.status = TrackerRewriteStatus::Merged;
            }
        }
        tiers.push_back(std::move(tier).value());
    }

    if (valid)
    {
        auto list = TrackerList::create(std::move(tiers));
        if (list)
        {
            preview.result = std::move(list).value();
        }
    }
    return preview;
}

Result<TrackerList> rewrite_trackers(const TrackerList& trackers, const TrackerRewriteRules& rules)
{
    auto preview = preview_tracker_rewrite(trackers, rules);
    if (preview.result)
    {
        return Result<TrackerList>::success(std::move(*preview.result));
    }

    Error error;
    error.code = ErrorCode::ValidationFailed;
    error.message = "tracker rewrite produced an invalid tracker URL";
    for (const auto& change : preview.changes)
    {
        if (change.status == TrackerRewriteStatus::Invalid)
        {
            error.issues.push_back({"tracker.tiers[" + std::to_string(change.tier) + "].trackers[" +
                                        std::to_string(change.index) + "]",
                                    change.reason});
        }
    }
    return Result<TrackerList>::failure(std::move(error));
}

} // namespace torrentutils::core
