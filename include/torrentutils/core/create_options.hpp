#pragma once

#include <cstdint>
#include <optional>
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

/** Unvalidated input accepted by CreateOptions::create(). */
struct CreateOptionsInput
{
    TorrentFormat format{TorrentFormat::Hybrid};
    PieceLengthStrategy piece_length_strategy{PieceLengthStrategy::Auto};
    FileOrderPolicy file_order_policy{FileOrderPolicy::Lexicographical};
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

    /** Resolves the deterministic piece length for a regular payload size. */
    [[nodiscard]] std::uint32_t piece_length_for(std::uint64_t regular_payload_size) const noexcept;

  private:
    CreateOptions(TorrentFormat format, PieceLengthStrategy piece_length_strategy,
                  FileOrderPolicy file_order_policy,
                  std::optional<std::uint32_t> fixed_piece_length, bool is_private,
                  TrackerList trackers, std::vector<WebSeedUrl> web_seeds);

    TorrentFormat format_;
    PieceLengthStrategy piece_length_strategy_;
    FileOrderPolicy file_order_policy_;
    std::optional<std::uint32_t> fixed_piece_length_;
    bool is_private_{};
    TrackerList trackers_;
    std::vector<WebSeedUrl> web_seeds_;
};

} // namespace torrentutils::core
