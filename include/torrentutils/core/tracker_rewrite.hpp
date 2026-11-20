#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/tracker.hpp>
#include <vector>

namespace torrentutils::core {

/** Maximum number of rules accepted by one tracker rewrite. */
inline constexpr std::size_t kMaximumTrackerRewriteRules = 64;
/** Maximum byte length of one sed-style rule, or of a structured rule's find or replace text. */
inline constexpr std::size_t kMaximumTrackerRewriteRuleBytes = 4096;
/** Maximum byte length of a tracker URL produced by a rewrite. */
inline constexpr std::size_t kMaximumRewrittenTrackerUrlBytes = 8192;

/** How the find text of a structured tracker rewrite rule is interpreted. */
enum class TrackerRewriteSyntax
{
    /** Find and replace text are used literally. */
    Literal,
    /**
     * Find is an RE2 pattern. In the replacement, `$1`-`$9` insert a capture group, `$0` and
     * `$&` the whole match, and `$$` a literal `$`; any other character is literal.
     */
    Regex,
};

/** One find-and-replace rule, as entered in a structured editor such as the GUI. */
struct TrackerRewriteRuleSpec
{
    std::string find;
    std::string replace;
    TrackerRewriteSyntax syntax{TrackerRewriteSyntax::Literal};
    bool match_case{true};
    /** Replace every match instead of only the first one. */
    bool replace_all{true};
};

/**
 * Ordered, compiled tracker rewrite rules.
 *
 * Rules come either from sed-style text (`parse`) or from structured find-and-replace specs
 * (`compile`). Patterns are searched, not anchored, and rules are applied in order, each to
 * the result of the previous one.
 *
 * A sed-style rule has the form `s<d>pattern<d>replacement<d>flags`, where `<d>` is any
 * single punctuation byte other than backslash. Patterns use RE2 syntax. In the replacement,
 * `\0`-`\9` and `&` insert the whole match or a capture group, while `\&`, `\\`, and `\<d>`
 * insert literal characters. Flags are `g` (replace every match) and `i` (ignore case).
 *
 * Failures are validation errors whose issue field is `rule[N]` (zero-based); messages never
 * repeat the rule text because rules may contain tracker passkeys.
 */
class TrackerRewriteRules
{
  public:
    [[nodiscard]] static Result<TrackerRewriteRules>
    parse(const std::vector<std::string>& expressions);

    /** Compiles structured find-and-replace rules; an empty `find` is invalid. */
    [[nodiscard]] static Result<TrackerRewriteRules>
    compile(const std::vector<TrackerRewriteRuleSpec>& specs);

    [[nodiscard]] std::size_t size() const noexcept;

    /** Applies every rule, in order, to one URL string. */
    [[nodiscard]] std::string apply(const std::string& value) const;

    class Rule;

  private:
    explicit TrackerRewriteRules(std::vector<std::shared_ptr<const Rule>> rules);

    std::vector<std::shared_ptr<const Rule>> rules_;
};

enum class TrackerRewriteStatus
{
    Unchanged,
    Changed,
    /** The rewritten URL duplicates an earlier tracker in the same tier and is dropped. */
    Merged,
    /** The rewritten value is not a valid tracker URL. */
    Invalid,
};

struct TrackerRewriteChange
{
    std::size_t tier{};
    std::size_t index{};
    std::string from;
    std::string to;
    TrackerRewriteStatus status{TrackerRewriteStatus::Unchanged};
    std::string reason;
};

/** Per-URL outcome of applying tracker rewrite rules to one tracker list. */
struct TrackerRewritePreview
{
    std::vector<TrackerRewriteChange> changes;
    /** Present only when every rewritten URL is valid. */
    std::optional<TrackerList> result;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool changed() const noexcept;
    [[nodiscard]] std::size_t count(TrackerRewriteStatus status) const noexcept;
};

/** Computes the rewrite of every tracker URL without failing on invalid results. */
[[nodiscard]] TrackerRewritePreview preview_tracker_rewrite(const TrackerList& trackers,
                                                            const TrackerRewriteRules& rules);

/** Rewrites every tracker URL; any invalid result fails the whole list. */
[[nodiscard]] Result<TrackerList> rewrite_trackers(const TrackerList& trackers,
                                                   const TrackerRewriteRules& rules);

} // namespace torrentutils::core
