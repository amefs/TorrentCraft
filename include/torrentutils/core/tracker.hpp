#pragma once

#include <string>
#include <torrentutils/core/result.hpp>
#include <vector>

namespace torrentutils::core {

/** Validated tracker URL preserving the caller's accepted spelling. */
class TrackerUrl
{
  public:
    [[nodiscard]] static Result<TrackerUrl> parse(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;

    friend bool operator==(const TrackerUrl& lhs, const TrackerUrl& rhs) noexcept;
    friend bool operator!=(const TrackerUrl& lhs, const TrackerUrl& rhs) noexcept;

  private:
    friend class TrackerTier;

    [[nodiscard]] const std::string& comparison_key() const noexcept;

    TrackerUrl(std::string value, std::string comparison_key);

    std::string value_;
    std::string comparison_key_;
};

/** Ordered, non-empty tracker tier with tier-local deduplication. */
class TrackerTier
{
  public:
    [[nodiscard]] static Result<TrackerTier> create(std::vector<TrackerUrl> trackers);

    [[nodiscard]] const std::vector<TrackerUrl>& trackers() const noexcept;

  private:
    explicit TrackerTier(std::vector<TrackerUrl> trackers);

    std::vector<TrackerUrl> trackers_;
};

/** Ordered BEP 12 tracker tiers. An empty list represents no trackers. */
class TrackerList
{
  public:
    [[nodiscard]] static Result<TrackerList> create(std::vector<TrackerTier> tiers);

    [[nodiscard]] const std::vector<TrackerTier>& tiers() const noexcept;

  private:
    explicit TrackerList(std::vector<TrackerTier> tiers);

    std::vector<TrackerTier> tiers_;
};

} // namespace torrentutils::core
