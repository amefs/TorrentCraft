#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <torrentutils/core/metadata.hpp>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/torrent_format.hpp>
#include <torrentutils/core/tracker.hpp>
#include <vector>

namespace torrentutils::core {

/** Strategy for selecting the piece length of a newly created torrent. */
enum class PieceLengthStrategy
{
    Auto,
    Fixed
};

/** Deterministic policy for ordering files in a newly created torrent. */
enum class FileOrderPolicy
{
    Lexicographical,
    CanonicalAlignment,
    Natural,
    BreadthFirst
};

/** User-selected policy for excluding filesystem entries from a directory torrent. */
enum class FileFilterMode
{
    Disabled,
    CommonArtifacts,
    CustomRules
};

/** The kind of filesystem entry represented by a filter report item. */
enum class FilteredEntryKind
{
    File,
    Directory,
    Symlink,
    Other
};

/** One entry excluded while preparing a directory torrent. */
struct FilteredEntry
{
    std::string relative_path;
    FilteredEntryKind kind{FilteredEntryKind::File};
    std::uint64_t bytes{};
    std::uint64_t descendant_count{};
    std::uint64_t descendant_bytes{};
    std::string matched_rule;
};

/** Complete, deterministic report of entries excluded from a create operation. */
struct FilterReport
{
    std::vector<FilteredEntry> entries;

    [[nodiscard]] std::uint64_t total_bytes() const noexcept
    {
        std::uint64_t total{};
        for (const auto& entry : entries)
        {
            total += entry.bytes + entry.descendant_bytes;
        }
        return total;
    }
};

/** Unvalidated input for a file filter. */
struct FileFilterInput
{
    FileFilterMode mode{FileFilterMode::Disabled};
    bool case_sensitive{};
    std::vector<std::string> patterns;
};

/** Validated immutable file filter used by torrent creation. */
class FileFilter
{
  public:
    [[nodiscard]] static Result<FileFilter> create(FileFilterInput input = {});

    [[nodiscard]] FileFilterMode mode() const noexcept;
    [[nodiscard]] bool case_sensitive() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool matches(std::string_view relative_path, bool directory) const;
    [[nodiscard]] std::string matched_rule(std::string_view relative_path, bool directory) const;

  private:
    FileFilter(FileFilterMode mode, bool case_sensitive, std::vector<std::string> patterns);

    FileFilterMode mode_;
    bool case_sensitive_{};
    std::vector<std::string> patterns_;
};

/** Unvalidated input accepted by CreateOptions::create(). */
struct CreateOptionsInput
{
    TorrentFormat format{TorrentFormat::Hybrid};
    PieceLengthStrategy piece_length_strategy{PieceLengthStrategy::Auto};
    FileOrderPolicy file_order_policy{FileOrderPolicy::Lexicographical};
    FileFilterInput file_filter;
    std::optional<std::uint32_t> fixed_piece_length;
    bool is_private{};
    std::vector<TrackerTier> tracker_tiers;
    std::vector<WebSeedUrl> web_seeds;
};

/** Validated, immutable options for a future torrent creation operation. */
class CreateOptions
{
  public:
    [[nodiscard]] static Result<CreateOptions> create(CreateOptionsInput input = {});

    [[nodiscard]] TorrentFormat format() const noexcept;
    [[nodiscard]] PieceLengthStrategy piece_length_strategy() const noexcept;
    [[nodiscard]] FileOrderPolicy file_order_policy() const noexcept;
    [[nodiscard]] const std::optional<std::uint32_t>& fixed_piece_length() const noexcept;
    [[nodiscard]] bool is_private() const noexcept;
    [[nodiscard]] const TrackerList& trackers() const noexcept;
    [[nodiscard]] const std::vector<WebSeedUrl>& web_seeds() const noexcept;
    [[nodiscard]] const FileFilter& file_filter() const noexcept;

    /** Resolves the deterministic piece length for a regular payload size. */
    [[nodiscard]] std::uint32_t piece_length_for(std::uint64_t regular_payload_size) const noexcept;

  private:
    CreateOptions(TorrentFormat format, PieceLengthStrategy piece_length_strategy,
                  FileOrderPolicy file_order_policy, FileFilter file_filter,
                  std::optional<std::uint32_t> fixed_piece_length, bool is_private,
                  TrackerList trackers, std::vector<WebSeedUrl> web_seeds);

    TorrentFormat format_;
    PieceLengthStrategy piece_length_strategy_;
    FileOrderPolicy file_order_policy_;
    FileFilter file_filter_;
    std::optional<std::uint32_t> fixed_piece_length_;
    bool is_private_{};
    TrackerList trackers_;
    std::vector<WebSeedUrl> web_seeds_;
};

} // namespace torrentutils::core
