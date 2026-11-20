#include <algorithm>
#include <string>
#include <system_error>
#include <torrentutils/frontend/tracker_batch.hpp>
#include <utility>
#include <vector>

namespace torrentutils::frontend {

namespace {

using core::Error;
using core::ErrorCode;

[[nodiscard]] Error cancelled_error()
{
    return Error{ErrorCode::Cancelled, "tracker batch was cancelled", {}};
}

[[nodiscard]] Error enumeration_error(const std::error_code& error)
{
    return Error{
        ErrorCode::IoFailure, "failed to enumerate torrent folder: " + error.message(), {}};
}

[[nodiscard]] bool is_torrent_file(const std::filesystem::directory_entry& entry)
{
    std::error_code error;
    return entry.is_regular_file(error) && !error && entry.path().extension() == ".torrent";
}

[[nodiscard]] TorrentBatchFile batch_file(const std::filesystem::directory_entry& entry,
                                          const std::filesystem::path& root)
{
    std::error_code size_error;
    const auto size = entry.file_size(size_error);
    return {entry.path(), entry.path().lexically_relative(root),
            size_error ? std::nullopt : std::optional<std::uintmax_t>{size}};
}

template <typename Iterator>
[[nodiscard]] core::Result<std::vector<TorrentBatchFile>>
collect(Iterator iterator, const std::filesystem::path& root,
        const core::CancellationToken& cancellation)
{
    std::vector<TorrentBatchFile> files;
    std::error_code error;
    for (const Iterator end; iterator != end; iterator.increment(error))
    {
        if (error)
        {
            break;
        }
        if (cancellation.is_cancelled())
        {
            return core::Result<std::vector<TorrentBatchFile>>::failure(cancelled_error());
        }
        if (is_torrent_file(*iterator))
        {
            files.push_back(batch_file(*iterator, root));
        }
    }
    if (error)
    {
        return core::Result<std::vector<TorrentBatchFile>>::failure(enumeration_error(error));
    }
    std::sort(files.begin(), files.end(),
              [](const auto& left, const auto& right) { return left.relative < right.relative; });
    return core::Result<std::vector<TorrentBatchFile>>::success(std::move(files));
}

[[nodiscard]] TrackerBatchFileResult failed(Error error,
                                            std::optional<core::TrackerRewritePreview> preview = {})
{
    TrackerBatchFileResult result;
    result.status = TrackerBatchFileStatus::Failed;
    result.preview = std::move(preview);
    result.error = std::move(error);
    return result;
}

} // namespace

core::Result<std::vector<TorrentBatchFile>>
enumerate_torrent_files(const std::filesystem::path& root, const bool recursive,
                        const core::CancellationToken& cancellation)
{
    std::error_code error;
    const auto status = std::filesystem::status(root, error);
    if (error || !std::filesystem::exists(status))
    {
        return core::Result<std::vector<TorrentBatchFile>>::failure(
            Error{ErrorCode::FileNotFound, "torrent path does not exist", {}});
    }
    if (!std::filesystem::is_directory(status))
    {
        const std::filesystem::directory_entry entry(root, error);
        if (error || !is_torrent_file(entry))
        {
            return core::Result<std::vector<TorrentBatchFile>>::failure(
                Error{ErrorCode::ValidationFailed, "path is not a .torrent file", {}});
        }
        auto file = batch_file(entry, root.parent_path());
        file.relative = root.filename();
        return core::Result<std::vector<TorrentBatchFile>>::success({std::move(file)});
    }

    if (recursive)
    {
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::none, error);
        if (error)
        {
            return core::Result<std::vector<TorrentBatchFile>>::failure(enumeration_error(error));
        }
        return collect(std::move(iterator), root, cancellation);
    }
    std::filesystem::directory_iterator iterator(root, error);
    if (error)
    {
        return core::Result<std::vector<TorrentBatchFile>>::failure(enumeration_error(error));
    }
    return collect(std::move(iterator), root, cancellation);
}

std::filesystem::path batch_output_path(const std::filesystem::path& output_dir,
                                        const TorrentBatchFile& file)
{
    return output_dir / file.relative;
}

bool TrackerBatchFileResult::cancelled() const noexcept
{
    return error && error->code == ErrorCode::Cancelled;
}

TrackerBatchFileResult apply_tracker_batch_file(const core::TorrentService& service,
                                                const TorrentBatchFile& file,
                                                const TrackerBatchEdit& edit,
                                                const TrackerBatchOptions& options,
                                                const core::TaskContext& context)
{
    if (context.cancellation.is_cancelled())
    {
        return failed(cancelled_error());
    }
    auto loaded = service.load(file.path, {}, context);
    if (!loaded)
    {
        return failed(std::move(loaded).error());
    }

    std::optional<core::TrackerRewritePreview> preview;
    const auto* rules = std::get_if<core::TrackerRewriteRules>(&edit);
    if (rules != nullptr)
    {
        preview = core::preview_tracker_rewrite(loaded.value().document().trackers(), *rules);
    }
    const core::EditAction action =
        rules != nullptr
            ? core::EditAction{core::RewriteTrackers{*rules}}
            : core::EditAction{core::ReplaceTrackers{std::get<core::TrackerList>(edit)}};

    auto edited = service.edit(loaded.value(), {action});
    if (!edited)
    {
        return failed(std::move(edited).error(), std::move(preview));
    }
    if (edited.value().disposition == core::EditDisposition::NeedRebuild)
    {
        return failed(Error{ErrorCode::ValidationFailed,
                            "tracker change requires re-creating the torrent",
                            {}},
                      std::move(preview));
    }

    TrackerBatchFileResult result;
    result.preview = std::move(preview);
    result.target =
        options.output_dir.empty() ? file.path : batch_output_path(options.output_dir, file);
    if (edited.value().disposition == core::EditDisposition::NoChange)
    {
        result.status = TrackerBatchFileStatus::Unchanged;
        return result;
    }
    result.status = TrackerBatchFileStatus::Changed;
    if (options.output_dir.empty() &&
        loaded.value().source_state() == core::LoadedTorrentSourceState::SymlinkFollowed)
    {
        // Saving in place refuses a symlinked source; report it here so a dry run agrees.
        return failed(
            Error{ErrorCode::ValidationFailed,
                  "a symlinked torrent cannot be edited in place; choose an output folder",
                  {}},
            std::move(result.preview));
    }
    if (options.dry_run)
    {
        return result;
    }
    if (context.cancellation.is_cancelled())
    {
        return failed(cancelled_error(), std::move(result.preview));
    }

    core::SaveRequest request;
    request.backup = options.backup;
    if (!options.output_dir.empty())
    {
        request.mode = core::SaveTargetMode::NewPath;
        request.destination = result.target;
        request.allow_overwrite = options.overwrite;
        std::error_code directory_error;
        std::filesystem::create_directories(result.target.parent_path(), directory_error);
        if (directory_error)
        {
            return failed(Error{ErrorCode::IoFailure,
                                "failed to create output folder: " + directory_error.message(),
                                {}},
                          std::move(result.preview));
        }
    }
    auto saved = service.save(edited.value().loaded, request, context);
    if (!saved)
    {
        return failed(std::move(saved).error(), std::move(result.preview));
    }
    return result;
}

} // namespace torrentutils::frontend
