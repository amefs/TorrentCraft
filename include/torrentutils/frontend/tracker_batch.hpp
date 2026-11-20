#pragma once

/// @file
/// @brief Tracker edit folders: torrent enumeration and the shared per-file batch step.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/task.hpp>
#include <torrentutils/core/tracker.hpp>
#include <torrentutils/core/tracker_rewrite.hpp>
#include <variant>
#include <vector>

namespace torrentutils::frontend {

/** One `.torrent` file found in a tracker edit folder. */
struct TorrentBatchFile
{
    std::filesystem::path path;
    /** Path relative to the enumerated root; mirrored below an output folder. */
    std::filesystem::path relative;
    std::optional<std::uintmax_t> size;
};

/**
 * Lists regular `*.torrent` files below `root`, sorted by relative path.
 *
 * Only the top level is listed unless `recursive` is set. Directory symlinks are never
 * followed. A regular file `root` yields that single file.
 */
[[nodiscard]] core::Result<std::vector<TorrentBatchFile>>
enumerate_torrent_files(const std::filesystem::path& root, bool recursive,
                        const core::CancellationToken& cancellation = {});

/** Target path of a batch file below `output_dir`, preserving its relative subfolder. */
[[nodiscard]] std::filesystem::path batch_output_path(const std::filesystem::path& output_dir,
                                                      const TorrentBatchFile& file);

/** Whole-list replacement or sed-style rewrite applied to every file of a batch. */
using TrackerBatchEdit = std::variant<core::TrackerList, core::TrackerRewriteRules>;

struct TrackerBatchOptions
{
    /** Empty means edit in place. */
    std::filesystem::path output_dir;
    bool dry_run{};
    bool overwrite{};
    bool backup{};
};

enum class TrackerBatchFileStatus
{
    Changed,
    Unchanged,
    Failed,
};

struct TrackerBatchFileResult
{
    TrackerBatchFileStatus status{TrackerBatchFileStatus::Failed};
    /** Per-URL outcome; present for a rewrite once the torrent loaded. */
    std::optional<core::TrackerRewritePreview> preview;
    std::optional<core::Error> error;
    /** Where the edited torrent was (or, in a dry run, would be) written. */
    std::filesystem::path target;

    [[nodiscard]] bool cancelled() const noexcept;
};

/**
 * Loads, edits, and (unless dry-running) atomically saves one torrent of a tracker edit
 * folder. Unchanged torrents are never written. Failures are reported, not thrown, so callers
 * can continue with the next file; a cancelled result should stop the batch.
 */
[[nodiscard]] TrackerBatchFileResult
apply_tracker_batch_file(const core::TorrentService& service, const TorrentBatchFile& file,
                         const TrackerBatchEdit& edit, const TrackerBatchOptions& options,
                         const core::TaskContext& context = {});

} // namespace torrentutils::frontend
