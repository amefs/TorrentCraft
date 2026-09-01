#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <torrentutils/core/metadata.hpp>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/task.hpp>
#include <torrentutils/core/torrent_document.hpp>
#include <torrentutils/core/torrent_engine.hpp>
#include <torrentutils/core/tracker.hpp>
#include <variant>
#include <vector>

namespace torrentutils::core {
namespace detail {
class VerificationAdmissionControllerTestProbe;
}

enum class LoadMode
{
    Lenient,
    Strict,
};

struct LoadOptions
{
    LoadMode mode{LoadMode::Lenient};
};

enum class LoadDiagnosticCode
{
    RetainedUnsupportedField,
    RetainedExtensionField,
};

enum class LoadDiagnosticScope
{
    TopLevel,
    Info,
};

using DiagnosticKeyBytes = std::vector<std::uint8_t>;

struct LoadDiagnostic
{
    LoadDiagnosticCode code{LoadDiagnosticCode::RetainedUnsupportedField};
    LoadDiagnosticScope scope{LoadDiagnosticScope::TopLevel};
    std::optional<DiagnosticKeyBytes> key;
};

enum class LoadedTorrentSourceState
{
    RegularFile,
    SymlinkFollowed,
};

/** Source-bound torrent snapshot whose persistence baseline cannot be forged by callers. */
class LoadedTorrent
{
  public:
    LoadedTorrent(const LoadedTorrent&) noexcept;
    LoadedTorrent(LoadedTorrent&&) noexcept;
    LoadedTorrent& operator=(const LoadedTorrent&) noexcept;
    LoadedTorrent& operator=(LoadedTorrent&&) noexcept;
    ~LoadedTorrent();

    [[nodiscard]] const TorrentDocument& document() const noexcept;
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept;
    [[nodiscard]] LoadedTorrentSourceState source_state() const noexcept;
    [[nodiscard]] const std::vector<LoadDiagnostic>& diagnostics() const noexcept;

  private:
    friend class TorrentRepository;
    friend class FileTorrentRepository;
    friend class TorrentService;

    struct Details;
    explicit LoadedTorrent(std::shared_ptr<const Details> details);

    std::shared_ptr<const Details> details_;
};

/** Injected source of signed UTC Unix epoch seconds for explicit SetCreationTimeNow actions. */
class Clock
{
  public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual std::int64_t now_unix_seconds() const noexcept = 0;
};

class SystemClock final : public Clock
{
  public:
    [[nodiscard]] std::int64_t now_unix_seconds() const noexcept override;
};

/** Explicit destination policy for persisting an edited torrent snapshot. */
enum class SaveTargetMode
{
    Original,
    NewPath,
};

/** Destination and overwrite policy for one persistence operation. */
struct SaveRequest
{
    SaveTargetMode mode{SaveTargetMode::Original};
    std::filesystem::path destination;
    bool allow_overwrite{false};
    bool backup{false};
};

/** Application persistence port; implementations own byte I/O and commit conflict checks. */
class TorrentRepository
{
  public:
    virtual ~TorrentRepository() = default;

    [[nodiscard]] virtual Result<LoadedTorrent> load(const std::filesystem::path& source,
                                                     LoadOptions options = {}) = 0;

    [[nodiscard]] virtual Result<LoadedTorrent> commit(const LoadedTorrent& loaded,
                                                       std::vector<std::uint8_t> bytes,
                                                       const CancellationToken& cancellation) = 0;

    /**
     * Persists to an explicit target when the repository supports it. The default keeps custom
     * source-bound repositories source-compatible while rejecting NewPath explicitly.
     */
    [[nodiscard]] virtual Result<LoadedTorrent> commit(const LoadedTorrent& loaded,
                                                       std::vector<std::uint8_t> bytes,
                                                       const SaveRequest& request,
                                                       const CancellationToken& cancellation);

  protected:
    [[nodiscard]] static LoadedTorrent make_loaded(TorrentDocument document,
                                                   std::filesystem::path source,
                                                   LoadedTorrentSourceState source_state,
                                                   std::vector<std::uint8_t> original_bytes,
                                                   std::vector<LoadDiagnostic> diagnostics);
};

/** Real filesystem repository using same-directory temporary files and atomic replacement. */
class FileTorrentRepository final : public TorrentRepository
{
  public:
    [[nodiscard]] Result<LoadedTorrent> load(const std::filesystem::path& source,
                                             LoadOptions options = {}) override;

    [[nodiscard]] Result<LoadedTorrent> commit(const LoadedTorrent& loaded,
                                               std::vector<std::uint8_t> bytes,
                                               const CancellationToken& cancellation) override;

    [[nodiscard]] Result<LoadedTorrent> commit(const LoadedTorrent& loaded,
                                               std::vector<std::uint8_t> bytes,
                                               const SaveRequest& request,
                                               const CancellationToken& cancellation) override;
};

struct SetComment
{
    std::string value;
};
struct ClearComment
{
};
struct SetCreator
{
    std::string value;
};
struct ClearCreator
{
};
struct SetInfoSource
{
    std::string value;
};
struct ClearInfoSource
{
};
struct SetName
{
    std::string value;
};
struct SetCreationTime
{
    std::int64_t unix_seconds{};
};
struct SetCreationTimeNow
{
};
struct ClearCreationTime
{
};
struct ReplaceWebSeeds
{
    std::vector<WebSeedUrl> value;
};
struct ReplaceDhtNodes
{
    std::vector<DhtNode> value;
};
struct ReplaceTrackers
{
    TrackerList value;
};
struct AddTrackerToTier
{
    std::size_t tier_index{};
    TrackerUrl tracker;
};
struct AddTrackerTier
{
    TrackerTier tier;
};
struct RemoveTracker
{
    std::size_t tier_index{};
    std::size_t tracker_index{};
};
struct MoveTracker
{
    std::size_t source_tier_index{};
    std::size_t source_tracker_index{};
    std::size_t destination_tier_index{};
};
struct MoveTrackerWithinTier
{
    std::size_t tier_index{};
    std::size_t source_tracker_index{};
    std::size_t destination_index{};
};
struct MoveTrackerTier
{
    std::size_t source_tier_index{};
    std::size_t destination_index{};
};
struct SetPrivate
{
    bool value{};
};

using EditAction =
    std::variant<SetComment, ClearComment, SetCreator, ClearCreator, SetInfoSource, ClearInfoSource,
                 SetName, SetCreationTime, SetCreationTimeNow, ClearCreationTime, ReplaceWebSeeds,
                 ReplaceDhtNodes, ReplaceTrackers, AddTrackerToTier, AddTrackerTier, RemoveTracker,
                 MoveTracker, MoveTrackerWithinTier, MoveTrackerTier, SetPrivate>;

enum class EditDisposition
{
    Applied,
    NoChange,
    NeedRebuild,
};

struct EditResult
{
    EditDisposition disposition{EditDisposition::NoChange};
    LoadedTorrent loaded;
};

enum class SaveDisposition
{
    Saved,
    NoChange,
};

struct SaveResult
{
    SaveDisposition disposition{SaveDisposition::NoChange};
    LoadedTorrent loaded;
};

/** Synchronous non-owning facade; callers serialize access to each service instance. */
class TorrentService
{
  public:
    TorrentService(TorrentRepository& repository, Clock& clock) noexcept;

    [[nodiscard]] Result<LoadedTorrent> load(const std::filesystem::path& source,
                                             LoadOptions options = {}) const;

    [[nodiscard]] Result<EditResult> edit(const LoadedTorrent& loaded,
                                          const std::vector<EditAction>& actions) const;

    [[nodiscard]] Result<SaveResult> save(const LoadedTorrent& loaded,
                                          const TaskContext& context = {}) const;
    [[nodiscard]] Result<SaveResult> save(const LoadedTorrent& loaded, const SaveRequest& request,
                                          const TaskContext& context = {}) const;

    [[nodiscard]] Result<InspectionReport> inspect(const TorrentDocument& document,
                                                   const TaskContext& context = {}) const;
    [[nodiscard]] Result<CreateResult> create(const CreateRequest& request,
                                              const TaskContext& context = {}) const;
    [[nodiscard]] Result<CreatePlan> plan_create(const CreatePlanRequest& request,
                                                 const TaskContext& context = {}) const;
    [[nodiscard]] Result<VerificationReport> verify(const VerifyRequest& request,
                                                    const TaskContext& context = {}) const;

  private:
    TorrentRepository& repository_;
    Clock& clock_;
};
/**
 * Shared, thread-safe FIFO limit on synchronous verification calls entering backend work.
 *
 * A controller can be shared by multiple AdmissionControlledVerifier instances. Its capacity
 * applies across all of them; it does not make an associated TorrentService thread-safe.
 */
class VerificationAdmissionController
{
  public:
    /** Creates a controller; a zero capacity returns ValidationFailed. */
    [[nodiscard]] static Result<VerificationAdmissionController> create(std::size_t capacity);
    [[nodiscard]] std::size_t capacity() const noexcept;

  private:
    struct State;
    class Permit;

    explicit VerificationAdmissionController(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class detail::VerificationAdmissionControllerTestProbe;
    friend class AdmissionControlledVerifier;
};

/**
 * Non-owning verification entry point governed by a shared admission controller.
 *
 * Calls wait and acquire permits in FIFO order. Cancellation while waiting returns Cancelled
 * without invoking TorrentService::verify; after admission, cancellation follows verify semantics.
 * Callers must still serialize access to each referenced TorrentService instance.
 */
class AdmissionControlledVerifier
{
  public:
    AdmissionControlledVerifier(const TorrentService& service,
                                VerificationAdmissionController& controller) noexcept;
    [[nodiscard]] Result<VerificationReport> verify(const VerifyRequest& request,
                                                    const TaskContext& context = {}) const;

  private:
    const TorrentService& service_;
    VerificationAdmissionController& controller_;
};
} // namespace torrentutils::core
