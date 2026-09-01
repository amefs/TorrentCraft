#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/tracker.hpp>

namespace torrentutils::core {

/** Stateless editing and interchange operations for immutable BEP 12 tracker lists. */
class TrackerEngine
{
  public:
    /** Appends a validated tracker to an existing tier. */
    [[nodiscard]] static Result<TrackerList>
    add_to_tier(const TrackerList& trackers, std::size_t tier_index, TrackerUrl tracker);

    /** Appends a validated, non-empty tier. */
    [[nodiscard]] static Result<TrackerList> add_tier(const TrackerList& trackers,
                                                      const TrackerTier& tier);

    /** Removes one tracker and removes its tier when it becomes empty. */
    [[nodiscard]] static Result<TrackerList>
    remove_tracker(const TrackerList& trackers, std::size_t tier_index, std::size_t tracker_index);

    /** Moves one tracker to the end of another tier. */
    [[nodiscard]] static Result<TrackerList> move_tracker(const TrackerList& trackers,
                                                          std::size_t source_tier_index,
                                                          std::size_t source_tracker_index,
                                                          std::size_t destination_tier_index);

    /** Reorders one tracker within its tier; destination is evaluated after removal. */
    [[nodiscard]] static Result<TrackerList>
    move_tracker_within_tier(const TrackerList& trackers, std::size_t tier_index,
                             std::size_t source_tracker_index, std::size_t destination_index);

    /** Reorders a tier; destination is evaluated after removing the source tier. */
    [[nodiscard]] static Result<TrackerList> move_tier(const TrackerList& trackers,
                                                       std::size_t source_tier_index,
                                                       std::size_t destination_index);

    /** Returns a fully constructed replacement list. */
    [[nodiscard]] static Result<TrackerList> replace(TrackerList replacement);

    /** Imports qBittorrent-style tier text with optional whole-line comments. */
    [[nodiscard]] static Result<TrackerList> import_text(std::string_view input);

    /** Exports tiers as newline-separated text with one blank line between tiers. */
    [[nodiscard]] static std::string export_text(const TrackerList& trackers);

    /** Imports the strict torrentutils.tracker-list/v1 JSON interchange object. */
    [[nodiscard]] static Result<TrackerList> import_json(std::string_view input);

    /** Exports compact torrentutils.tracker-list/v1 JSON. */
    [[nodiscard]] static std::string export_json(const TrackerList& trackers);
};

} // namespace torrentutils::core
