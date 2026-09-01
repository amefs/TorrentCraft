#include "metadata_engine.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <torrentutils/core/application.hpp>
#include <torrentutils/core/tracker_engine.hpp>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace torrentutils::core {
namespace {
constexpr std::uintmax_t max_torrent_bytes = static_cast<std::uintmax_t>(128U) * 1024U * 1024U;
constexpr std::string_view retained_unknown_prefix = "retained unknown field: ";
constexpr std::string_view retained_extension_prefix = "retained extension field: ";

[[nodiscard]] Error filesystem_error(const std::error_code& error, std::string message)
{
    const auto permission_denied =
        error == std::errc::permission_denied || error == std::errc::operation_not_permitted;
    return {
        permission_denied ? ErrorCode::AccessDenied : ErrorCode::IoFailure, std::move(message), {}};
}

[[nodiscard]] Error stream_error(const int error, std::string message)
{
    const auto permission_denied = error == EACCES || error == EPERM;
    return {
        permission_denied ? ErrorCode::AccessDenied : ErrorCode::IoFailure, std::move(message), {}};
}

[[nodiscard]] std::vector<LoadDiagnostic> load_diagnostics(const TorrentDocument& document)
{
    std::vector<LoadDiagnostic> diagnostics;
    for (const auto& warning : document.warnings())
    {
        LoadDiagnosticCode code{};
        std::string_view prefix{};
        if (warning.message.compare(0, retained_unknown_prefix.size(), retained_unknown_prefix) ==
            0)
        {
            code = LoadDiagnosticCode::RetainedUnsupportedField;
            prefix = retained_unknown_prefix;
        }
        else if (warning.message.compare(0, retained_extension_prefix.size(),
                                         retained_extension_prefix) == 0)
        {
            code = LoadDiagnosticCode::RetainedExtensionField;
            prefix = retained_extension_prefix;
        }
        else
        {
            continue;
        }
        const auto key_text = warning.message.substr(prefix.size());
        DiagnosticKeyBytes key;
        key.reserve(key_text.size());
        std::transform(key_text.begin(), key_text.end(), std::back_inserter(key),
                       [](const char byte) { return static_cast<std::uint8_t>(byte); });
        diagnostics.push_back({code,
                               warning.field == "top-level" ? LoadDiagnosticScope::TopLevel
                                                            : LoadDiagnosticScope::Info,
                               std::move(key)});
    }
    const auto less = [](const LoadDiagnostic& left, const LoadDiagnostic& right) {
        if (left.code != right.code)
        {
            return left.code < right.code;
        }
        if (left.scope != right.scope)
        {
            return left.scope < right.scope;
        }
        return left.key < right.key;
    };
    std::sort(diagnostics.begin(), diagnostics.end(), less);
    diagnostics.erase(std::unique(diagnostics.begin(), diagnostics.end(),
                                  [](const LoadDiagnostic& left, const LoadDiagnostic& right) {
                                      return left.code == right.code && left.scope == right.scope &&
                                             left.key == right.key;
                                  }),
                      diagnostics.end());
    return diagnostics;
}

class PendingFile
{
  public:
    explicit PendingFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~PendingFile()
    {
        if (!committed_)
        {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

    void release() noexcept
    {
        committed_ = true;
    }

  private:
    std::filesystem::path path_;
    bool committed_{};
};

[[nodiscard]] std::filesystem::path temporary_sibling(const std::filesystem::path& target)
{
    static std::atomic<std::uint64_t> sequence{};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    auto name = target.filename();
    name +=
        (".torrentutils.tmp." + std::to_string(tick) + "." + std::to_string(sequence.fetch_add(1)));
    return target.parent_path() / name;
}

[[nodiscard]] std::error_code atomic_replace(const std::filesystem::path& source,
                                             const std::filesystem::path& target)
{
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
    {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }
    return {};
#else
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return error;
#endif
}

[[nodiscard]] Result<std::vector<std::uint8_t>> read_bounded_file(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
    {
        return Result<std::vector<std::uint8_t>>::failure(
            filesystem_error(error, "failed to inspect torrent source size"));
    }
    if (size > max_torrent_bytes ||
        size > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
    {
        return Result<std::vector<std::uint8_t>>::failure(
            {ErrorCode::Conflict, "torrent source no longer matches its byte baseline", {}});
    }

    errno = 0;
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return Result<std::vector<std::uint8_t>>::failure(
            stream_error(errno, "failed to open torrent source"));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty())
    {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input || input.peek() != std::char_traits<char>::eof())
    {
        return Result<std::vector<std::uint8_t>>::failure(
            stream_error(errno, "failed to read torrent source"));
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

[[nodiscard]] Error cancelled_save()
{
    return {ErrorCode::Cancelled, "torrent save was cancelled", {}};
}
} // namespace

struct LoadedTorrent::Details
{
    TorrentDocument document;
    std::filesystem::path source;
    LoadedTorrentSourceState source_state{LoadedTorrentSourceState::RegularFile};
    std::vector<std::uint8_t> original_bytes;
    std::vector<LoadDiagnostic> diagnostics;
};

LoadedTorrent::LoadedTorrent(std::shared_ptr<const Details> details) : details_(std::move(details))
{
}

LoadedTorrent::LoadedTorrent(const LoadedTorrent&) noexcept = default;
LoadedTorrent::LoadedTorrent(LoadedTorrent&&) noexcept = default;
LoadedTorrent& LoadedTorrent::operator=(const LoadedTorrent&) noexcept = default;
LoadedTorrent& LoadedTorrent::operator=(LoadedTorrent&&) noexcept = default;
LoadedTorrent::~LoadedTorrent() = default;

const TorrentDocument& LoadedTorrent::document() const noexcept
{
    return details_->document;
}

const std::filesystem::path& LoadedTorrent::source_path() const noexcept
{
    return details_->source;
}

LoadedTorrentSourceState LoadedTorrent::source_state() const noexcept
{
    return details_->source_state;
}

const std::vector<LoadDiagnostic>& LoadedTorrent::diagnostics() const noexcept
{
    return details_->diagnostics;
}

std::int64_t SystemClock::now_unix_seconds() const noexcept
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(now).count();
}

LoadedTorrent TorrentRepository::make_loaded(TorrentDocument document, std::filesystem::path source,
                                             const LoadedTorrentSourceState source_state,
                                             std::vector<std::uint8_t> original_bytes,
                                             std::vector<LoadDiagnostic> diagnostics)
{
    return LoadedTorrent(std::make_shared<const LoadedTorrent::Details>(
        LoadedTorrent::Details{std::move(document), std::move(source), source_state,
                               std::move(original_bytes), std::move(diagnostics)}));
}

Result<LoadedTorrent> TorrentRepository::commit(const LoadedTorrent& loaded,
                                                std::vector<std::uint8_t> bytes,
                                                const SaveRequest& request,
                                                const CancellationToken& cancellation)
{
    if (request.mode == SaveTargetMode::NewPath)
    {
        return Result<LoadedTorrent>::failure(
            {ErrorCode::UnsupportedFeature,
             "repository does not support saving to an explicit target path",
             {}});
    }
    return commit(loaded, std::move(bytes), cancellation);
}

Result<LoadedTorrent> FileTorrentRepository::load(const std::filesystem::path& source,
                                                  const LoadOptions options)
{
    std::error_code error;
    auto absolute_source = std::filesystem::absolute(source, error);
    if (error)
    {
        return Result<LoadedTorrent>::failure(
            filesystem_error(error, "failed to make torrent source path absolute"));
    }
    absolute_source = absolute_source.lexically_normal();

    const auto link_status = std::filesystem::symlink_status(absolute_source, error);
    if (error)
    {
        if (error == std::errc::no_such_file_or_directory)
        {
            return Result<LoadedTorrent>::failure(
                {ErrorCode::FileNotFound, "torrent source does not exist", {}});
        }
        return Result<LoadedTorrent>::failure(
            filesystem_error(error, "failed to inspect torrent source"));
    }
    if (link_status.type() == std::filesystem::file_type::not_found)
    {
        return Result<LoadedTorrent>::failure(
            {ErrorCode::FileNotFound, "torrent source does not exist", {}});
    }
    const auto source_state = std::filesystem::is_symlink(link_status)
                                  ? LoadedTorrentSourceState::SymlinkFollowed
                                  : LoadedTorrentSourceState::RegularFile;
    const auto followed_status = std::filesystem::status(absolute_source, error);
    if (error || !std::filesystem::is_regular_file(followed_status))
    {
        if (error && source_state == LoadedTorrentSourceState::RegularFile)
        {
            return Result<LoadedTorrent>::failure(
                filesystem_error(error, "failed to inspect torrent source target"));
        }
        return Result<LoadedTorrent>::failure(
            {ErrorCode::ValidationFailed, "torrent source must resolve to an ordinary file", {}});
    }

    const auto size = std::filesystem::file_size(absolute_source, error);
    if (error)
    {
        return Result<LoadedTorrent>::failure(
            filesystem_error(error, "failed to inspect torrent source size"));
    }
    if (size > max_torrent_bytes ||
        size > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
    {
        return Result<LoadedTorrent>::failure(
            {ErrorCode::InvalidBencode, "bencode input exceeds configured byte limit", {}});
    }

    errno = 0;
    std::ifstream input(absolute_source, std::ios::binary);
    if (!input)
    {
        return Result<LoadedTorrent>::failure(stream_error(errno, "failed to open torrent source"));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty())
    {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input || input.peek() != std::char_traits<char>::eof())
    {
        return Result<LoadedTorrent>::failure(stream_error(errno, "failed to read torrent source"));
    }

    const auto mode = options.mode == LoadMode::Strict ? detail::MetadataReadMode::Strict
                                                       : detail::MetadataReadMode::Lenient;
    auto decoded = detail::decode_torrent(bytes, mode);
    if (!decoded)
    {
        return Result<LoadedTorrent>::failure(std::move(decoded).error());
    }
    auto document = std::move(decoded).value();
    auto diagnostics = load_diagnostics(document);
    return Result<LoadedTorrent>::success(make_loaded(std::move(document),
                                                      std::move(absolute_source), source_state,
                                                      std::move(bytes), std::move(diagnostics)));
}

Result<LoadedTorrent> FileTorrentRepository::commit(const LoadedTorrent& loaded,
                                                    std::vector<std::uint8_t> bytes,
                                                    const CancellationToken& cancellation)
{
    return commit(loaded, std::move(bytes), SaveRequest{}, cancellation);
}

Result<LoadedTorrent> FileTorrentRepository::commit(const LoadedTorrent& loaded,
                                                    std::vector<std::uint8_t> bytes,
                                                    const SaveRequest& request,
                                                    const CancellationToken& cancellation)
{
    if (cancellation.is_cancelled())
    {
        return Result<LoadedTorrent>::failure(cancelled_save());
    }

    std::filesystem::path target;
    if (request.mode == SaveTargetMode::Original)
    {
        target = loaded.details_->source;
    }
    else
    {
        if (request.destination.empty())
        {
            return Result<LoadedTorrent>::failure(
                {ErrorCode::ValidationFailed, "explicit save target path is empty", {}});
        }
        std::error_code absolute_error;
        target = std::filesystem::absolute(request.destination, absolute_error);
        if (absolute_error)
        {
            return Result<LoadedTorrent>::failure(
                filesystem_error(absolute_error, "failed to resolve explicit save target path"));
        }
        target = target.lexically_normal();
    }

    bool target_exists = request.mode == SaveTargetMode::Original;
    if (request.mode == SaveTargetMode::NewPath)
    {
        std::error_code target_status_error;
        const auto target_status = std::filesystem::symlink_status(target, target_status_error);
        if (target_status_error && target_status_error != std::errc::no_such_file_or_directory)
        {
            return Result<LoadedTorrent>::failure(
                filesystem_error(target_status_error, "failed to inspect save target"));
        }
        target_exists = target_status.type() != std::filesystem::file_type::not_found;
        if (target_exists && std::filesystem::is_symlink(target_status))
        {
            return Result<LoadedTorrent>::failure(
                {ErrorCode::Conflict, "save target must not be a symbolic link", {}});
        }
        if (target_exists && !std::filesystem::is_regular_file(target_status))
        {
            return Result<LoadedTorrent>::failure(
                {ErrorCode::Conflict, "save target is not an ordinary file", {}});
        }
    }

    const bool source_bound = target == loaded.details_->source.lexically_normal();
    if (request.mode == SaveTargetMode::NewPath && source_bound && target_exists &&
        !request.allow_overwrite)
    {
        return Result<LoadedTorrent>::failure(
            {ErrorCode::Conflict, "save target already exists", {}});
    }
    if (request.mode == SaveTargetMode::Original || source_bound)
    {
        std::error_code parent_error;
        const auto parent_status = std::filesystem::status(target.parent_path(), parent_error);
        if (parent_error || !std::filesystem::is_directory(parent_status))
        {
            if (!parent_error)
            {
                parent_error = std::make_error_code(std::errc::not_a_directory);
            }
            return Result<LoadedTorrent>::failure(
                filesystem_error(parent_error, "failed to inspect torrent source parent"));
        }
        std::error_code source_error;
        const auto source_status =
            std::filesystem::symlink_status(loaded.details_->source, source_error);
        if (source_error)
        {
            if (source_status.type() == std::filesystem::file_type::not_found ||
                source_error == std::errc::no_such_file_or_directory)
            {
                return Result<LoadedTorrent>::failure(
                    {ErrorCode::Conflict, "torrent source disappeared before commit", {}});
            }
            return Result<LoadedTorrent>::failure(
                filesystem_error(source_error, "failed to inspect torrent source before commit"));
        }
        if (std::filesystem::is_symlink(source_status) ||
            !std::filesystem::is_regular_file(source_status))
        {
            return Result<LoadedTorrent>::failure(
                {ErrorCode::Conflict, "torrent source is no longer an ordinary file", {}});
        }

        auto current = read_bounded_file(loaded.details_->source);
        if (!current)
        {
            return Result<LoadedTorrent>::failure(std::move(current).error());
        }
        if (current.value() != loaded.details_->original_bytes)
        {
            return Result<LoadedTorrent>::failure(
                {ErrorCode::Conflict, "torrent source bytes changed before commit", {}});
        }
    }
    else if (target_exists && !request.allow_overwrite)
    {
        return Result<LoadedTorrent>::failure(
            {ErrorCode::Conflict, "save target already exists", {}});
    }

    if (request.backup && target_exists)
    {
        auto backup = target;
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        backup += ".bak-" + std::to_string(stamp);
        std::error_code backup_error;
        if (!std::filesystem::copy_file(target, backup, std::filesystem::copy_options::none,
                                        backup_error))
        {
            return Result<LoadedTorrent>::failure(
                filesystem_error(backup_error, "failed to create torrent backup"));
        }
    }

    PendingFile pending(temporary_sibling(target));
    errno = 0;
    {
        std::ofstream output(pending.path(), std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return Result<LoadedTorrent>::failure(
                stream_error(errno, "failed to create temporary torrent sibling"));
        }
        if (!bytes.empty())
        {
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        output.close();
        if (!output)
        {
            return Result<LoadedTorrent>::failure(
                stream_error(errno, "failed to write temporary torrent sibling"));
        }
    }

    if (cancellation.is_cancelled())
    {
        return Result<LoadedTorrent>::failure(cancelled_save());
    }

    const auto commit_error = atomic_replace(pending.path(), target);
    if (commit_error)
    {
        return Result<LoadedTorrent>::failure(
            filesystem_error(commit_error, "failed to atomically replace torrent source"));
    }
    pending.release();

    auto details = std::make_shared<const LoadedTorrent::Details>(LoadedTorrent::Details{
        loaded.details_->document, target, LoadedTorrentSourceState::RegularFile, std::move(bytes),
        loaded.details_->diagnostics});
    return Result<LoadedTorrent>::success(LoadedTorrent(std::move(details)));
}

TorrentService::TorrentService(TorrentRepository& repository, Clock& clock) noexcept
    : repository_(repository), clock_(clock)
{
}

Result<LoadedTorrent> TorrentService::load(const std::filesystem::path& source,
                                           const LoadOptions options) const
{
    static_cast<void>(clock_);
    return repository_.load(source, options);
}
namespace {
template <class... Visitors> struct Overloaded : Visitors...
{
    using Visitors::operator()...;
};
template <class... Visitors> Overloaded(Visitors...) -> Overloaded<Visitors...>;

[[nodiscard]] TorrentMetadataInput metadata_input(const TorrentMetadata& metadata)
{
    return {metadata.comment(),   metadata.creator(),
            metadata.source(),    metadata.creation_time_unix_seconds(),
            metadata.web_seeds(), metadata.collections(),
            metadata.dht_nodes()};
}
} // namespace

Result<EditResult> TorrentService::edit(const LoadedTorrent& loaded,
                                        const std::vector<EditAction>& actions) const
{
    if (actions.empty())
    {
        return Result<EditResult>::success({EditDisposition::NoChange, loaded});
    }

    auto metadata = metadata_input(loaded.document().metadata());
    auto trackers = loaded.document().trackers();
    std::vector<detail::InfoIdentityPatch> identity_patches;
    bool info_source_patched = false;
    std::optional<Error> action_error;

    const auto update_trackers = [&](Result<TrackerList> result) {
        if (!result)
        {
            if (!action_error)
            {
                action_error = std::move(result).error();
            }
            return;
        }
        trackers = std::move(result).value();
    };

    for (const auto& action : actions)
    {
        std::visit(
            Overloaded{
                [&](const SetComment& value) { metadata.comment = value.value; },
                [&](const ClearComment&) { metadata.comment.reset(); },
                [&](const SetCreator& value) { metadata.creator = value.value; },
                [&](const ClearCreator&) { metadata.creator.reset(); },
                [&](const SetInfoSource& value) {
                    info_source_patched = true;
                    identity_patches.push_back(
                        {detail::InfoIdentityField::Source, false, value.value, false});
                },
                [&](const ClearInfoSource&) {
                    info_source_patched = true;
                    identity_patches.push_back(
                        {detail::InfoIdentityField::Source, false, {}, true});
                },
                [&](const SetName& value) {
                    identity_patches.push_back(
                        {detail::InfoIdentityField::Name, false, value.value, false});
                },
                [&](const SetCreationTime& value) {
                    metadata.creation_time_unix_seconds = value.unix_seconds;
                },
                [&](const SetCreationTimeNow&) {
                    metadata.creation_time_unix_seconds = clock_.now_unix_seconds();
                },
                [&](const ClearCreationTime&) { metadata.creation_time_unix_seconds.reset(); },
                [&](const ReplaceWebSeeds& value) { metadata.web_seeds = value.value; },
                [&](const ReplaceDhtNodes& value) { metadata.dht_nodes = value.value; },
                [&](const ReplaceTrackers& value) {
                    update_trackers(TrackerEngine::replace(value.value));
                },
                [&](const AddTrackerToTier& value) {
                    update_trackers(
                        TrackerEngine::add_to_tier(trackers, value.tier_index, value.tracker));
                },
                [&](const AddTrackerTier& value) {
                    update_trackers(TrackerEngine::add_tier(trackers, value.tier));
                },
                [&](const RemoveTracker& value) {
                    update_trackers(TrackerEngine::remove_tracker(trackers, value.tier_index,
                                                                  value.tracker_index));
                },
                [&](const MoveTracker& value) {
                    update_trackers(TrackerEngine::move_tracker(trackers, value.source_tier_index,
                                                                value.source_tracker_index,
                                                                value.destination_tier_index));
                },
                [&](const MoveTrackerWithinTier& value) {
                    update_trackers(TrackerEngine::move_tracker_within_tier(
                        trackers, value.tier_index, value.source_tracker_index,
                        value.destination_index));
                },
                [&](const MoveTrackerTier& value) {
                    update_trackers(TrackerEngine::move_tier(trackers, value.source_tier_index,
                                                             value.destination_index));
                },
                [&](const SetPrivate& value) {
                    identity_patches.push_back(
                        {detail::InfoIdentityField::Private, value.value, {}, false});
                },
            },
            action);
    }

    if (action_error)
    {
        return Result<EditResult>::failure(std::move(*action_error));
    }

    auto identity_document = loaded.document();
    for (const auto& patch : identity_patches)
    {
        auto patched = detail::patch_info_identity(identity_document, patch);
        if (!patched)
        {
            return Result<EditResult>::failure(std::move(patched).error());
        }
        identity_document = std::move(patched).value();
    }

    if (info_source_patched)
    {
        metadata.source = identity_document.metadata().source();
    }
    auto validated_metadata = TorrentMetadata::create(std::move(metadata));
    if (!validated_metadata)
    {
        return Result<EditResult>::failure(std::move(validated_metadata).error());
    }

    auto with_metadata = identity_document.with_metadata(std::move(validated_metadata).value());
    if (!with_metadata)
    {
        return Result<EditResult>::failure(std::move(with_metadata).error());
    }
    auto candidate = std::move(with_metadata).value().with_trackers(std::move(trackers));
    if (!candidate)
    {
        return Result<EditResult>::failure(std::move(candidate).error());
    }

    auto encoded = detail::encode_top_level_patch(candidate.value());
    if (!encoded)
    {
        return Result<EditResult>::failure(std::move(encoded).error());
    }
    if (encoded.value().disposition == detail::MetadataEncodeDisposition::NeedRebuild)
    {
        return Result<EditResult>::success({EditDisposition::NeedRebuild, loaded});
    }
    if (encoded.value().bytes == loaded.details_->original_bytes)
    {
        return Result<EditResult>::success({EditDisposition::NoChange, loaded});
    }

    auto details = std::make_shared<const LoadedTorrent::Details>(LoadedTorrent::Details{
        std::move(candidate).value(), loaded.details_->source, loaded.details_->source_state,
        loaded.details_->original_bytes, loaded.details_->diagnostics});
    return Result<EditResult>::success(

        {EditDisposition::Applied, LoadedTorrent(std::move(details))});
}
Result<SaveResult> TorrentService::save(const LoadedTorrent& loaded,
                                        const TaskContext& context) const
{
    return save(loaded, SaveRequest{}, context);
}

Result<SaveResult> TorrentService::save(const LoadedTorrent& loaded, const SaveRequest& request,
                                        const TaskContext& context) const
{
    if (context.cancellation.is_cancelled())
    {
        return Result<SaveResult>::failure(cancelled_save());
    }
    if (request.mode == SaveTargetMode::Original &&
        loaded.details_->source_state == LoadedTorrentSourceState::SymlinkFollowed)
    {
        return Result<SaveResult>::failure(
            {ErrorCode::ValidationFailed,
             "a symlink-followed loaded torrent is not eligible for source-bound save",
             {}});
    }

    auto encoded = detail::encode_top_level_patch(loaded.details_->document);
    if (!encoded)
    {
        return Result<SaveResult>::failure(std::move(encoded).error());
    }
    if (encoded.value().disposition == detail::MetadataEncodeDisposition::NeedRebuild)
    {
        return Result<SaveResult>::failure(
            {ErrorCode::UnsupportedFeature,
             "torrent document requires rebuild and cannot be source-bound saved",
             {}});
    }
    const bool same_target =
        request.mode == SaveTargetMode::Original ||
        request.destination.lexically_normal() == loaded.details_->source.lexically_normal();
    if (encoded.value().bytes == loaded.details_->original_bytes && same_target)
    {
        return Result<SaveResult>::success({SaveDisposition::NoChange, loaded});
    }

    auto committed =
        repository_.commit(loaded, std::move(encoded).value().bytes, request, context.cancellation);
    if (!committed)
    {
        return Result<SaveResult>::failure(std::move(committed).error());
    }

    return Result<SaveResult>::success({SaveDisposition::Saved, std::move(committed).value()});
}

Result<CreatePlan> TorrentService::plan_create(const CreatePlanRequest& request,
                                               const TaskContext& context) const
{
    return TorrentEngine{}.plan_create(request, context);
}
} // namespace torrentutils::core
