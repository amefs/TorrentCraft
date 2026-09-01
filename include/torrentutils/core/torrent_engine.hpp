#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <torrentutils/core/create_options.hpp>
#include <torrentutils/core/hash.hpp>
#include <torrentutils/core/path.hpp>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/task.hpp>
#include <torrentutils/core/torrent_document.hpp>
#include <utility>
#include <vector>

namespace torrentutils::core {

/** Whether the current Torrent Engine can verify a document. */
enum class VerificationCapability
{
    Supported,
    Unsupported
};

/** Stable machine-readable reasons why document verification is unsupported. */
enum class VerificationCapabilityDiagnosticCode
{
    UnsupportedTorrentFormat,
    UnsupportedPieceHashScheme,
    UnsupportedFileLayout,
    UnsupportedFileAttribute,
    UnsupportedSymlinkSemantics,
    BackendFeatureUnavailable
};

/** One deterministic verification-capability diagnostic. */
struct CapabilityDiagnostic
{
    VerificationCapabilityDiagnosticCode code;
    std::string message;
};

/** Read-only assessment of whether a loaded document can be verified. */
struct InspectionReport
{
    VerificationCapability verification_capability;
    std::vector<CapabilityDiagnostic> diagnostics;
};

/** Cryptographic comparison result for one completed torrent Piece. */
enum class PieceVerificationState
{
    Verified,
    Mismatched
};

/** A zero-based half-open range [begin, end) of completed torrent Pieces. */
struct PieceRange
{
    std::uint64_t begin;
    std::uint64_t end;
    PieceVerificationState state;
};

[[nodiscard]] constexpr bool is_valid(const PieceRange& range) noexcept
{
    return range.begin < range.end;
}

/** Stable, composable findings for one logical file in a verification report. */
enum class FileVerificationFinding : std::uint32_t
{
    None = 0,
    Missing = 1U << 0U,
    NotRegularFile = 1U << 1U,
    LengthMismatch = 1U << 2U,
    HashMismatch = 1U << 3U,
    SharedPieceMismatch = 1U << 4U,
    SymlinkMissing = 1U << 5U,
    SymlinkTargetMismatch = 1U << 6U
};

[[nodiscard]] constexpr FileVerificationFinding operator|(FileVerificationFinding lhs,
                                                          FileVerificationFinding rhs) noexcept
{
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): bitmask combinations are valid.
    return static_cast<FileVerificationFinding>(static_cast<std::uint32_t>(lhs) |
                                                static_cast<std::uint32_t>(rhs));
}

constexpr FileVerificationFinding& operator|=(FileVerificationFinding& lhs,
                                              FileVerificationFinding rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool has_finding(FileVerificationFinding findings,
                                         FileVerificationFinding finding) noexcept
{
    return (static_cast<std::uint32_t>(findings) & static_cast<std::uint32_t>(finding)) ==
           static_cast<std::uint32_t>(finding);
}

/** Cumulative verification counters for one changed logical file. */
struct FileVerificationProgress
{
    LogicalPath path;
    std::uint64_t expected_bytes;
    std::uint64_t hashed_bytes;
    std::uint64_t verified_bytes;
    std::uint64_t mismatched_bytes;
};

/** One bounded page of synchronous verification progress. */
struct VerificationProgress
{
    std::uint64_t sequence;
    std::vector<FileVerificationProgress> files;
    std::vector<PieceRange> piece_ranges;
};

using VerificationProgressCallback = std::function<void(const VerificationProgress&)>;

/** Unvalidated limits for the resources consumed by one verification operation. */
struct VerificationResourceBudgetInput
{
    std::uint32_t hashing_workers{};
    std::uint64_t checking_memory_bytes{};
    std::uint64_t max_logical_files{};
    std::uint64_t max_pieces{};
};

/** Validated, immutable resources reserved for one verification operation. */
class VerificationResourceBudget
{
  public:
    [[nodiscard]] static Result<VerificationResourceBudget>
    create(VerificationResourceBudgetInput input);

    [[nodiscard]] std::uint32_t hashing_workers() const noexcept;
    [[nodiscard]] std::uint64_t checking_memory_bytes() const noexcept;
    [[nodiscard]] std::uint64_t max_logical_files() const noexcept;
    [[nodiscard]] std::uint64_t max_pieces() const noexcept;

  private:
    explicit VerificationResourceBudget(VerificationResourceBudgetInput input) noexcept;

    VerificationResourceBudgetInput input_;
};

/** Backend file-I/O mode selected by the front end for a create/verify operation. */
enum class DiskIoMode
{
    Posix,
    Mmap
};

/** Input for one verification operation against an immutable document. */
struct VerifyRequest
{
    TorrentDocument document;
    std::filesystem::path content_root;
    VerificationProgressCallback on_progress{};
    std::optional<VerificationResourceBudget> resource_budget;
    std::optional<DiskIoMode> disk_io_mode;
    VerifyRequest(TorrentDocument document, std::filesystem::path content_root,
                  VerificationProgressCallback on_progress = {},
                  std::optional<VerificationResourceBudget> resource_budget = {},
                  std::optional<DiskIoMode> disk_io_mode = {})
        : document(std::move(document)), content_root(std::move(content_root)),
          on_progress(std::move(on_progress)), resource_budget(std::move(resource_budget)),
          disk_io_mode(std::move(disk_io_mode))
    {
    }
};
/** Aggregate business outcome of a completed verification operation. */

enum class VerificationOutcome
{
    Verified,
    Mismatched,
    Incomplete
};

/** Bounded final verification evidence for one logical file. */
struct FileVerificationResult
{
    LogicalPath path;
    std::uint64_t expected_bytes;
    std::uint64_t hashed_bytes;
    std::uint64_t verified_bytes;
    std::uint64_t mismatched_bytes;
    FileVerificationFinding findings{FileVerificationFinding::None};
};

/** Completed verification outcome and file-level evidence. */
struct VerificationReport
{
    VerificationOutcome outcome;
    std::uint64_t expected_bytes;
    std::uint64_t hashed_bytes;
    std::uint64_t verified_bytes;
    std::uint64_t mismatched_bytes;
    std::vector<FileVerificationResult> files;
};

/** Validated top-level metadata supplied during one creation operation. */
struct CreationMetadataInput
{
    std::optional<std::string> comment;
    std::optional<std::string> created_by;
    std::optional<std::int64_t> creation_time_unix_seconds;
};

/** Validated identity-affecting metadata supplied during one creation operation. */
struct CreateInfoInput
{
    std::optional<std::string> source;
};

/** Input for one explicit torrent creation operation. */
struct CreateRequest
{
    std::filesystem::path content_root;
    std::filesystem::path target_path;
    CreateOptions options;
    bool allow_overwrite{false};
    CreationMetadataInput creation_metadata;
    CreateInfoInput create_info;
    std::optional<DiskIoMode> disk_io_mode;

    CreateRequest(std::filesystem::path content_root, std::filesystem::path target_path,
                  CreateOptions options, bool allow_overwrite = false,
                  CreationMetadataInput creation_metadata = {}, CreateInfoInput create_info = {},
                  std::optional<DiskIoMode> disk_io_mode = {})
        : content_root(std::move(content_root)), target_path(std::move(target_path)),
          options(std::move(options)), allow_overwrite(allow_overwrite),
          creation_metadata(std::move(creation_metadata)), create_info(std::move(create_info)),
          disk_io_mode(std::move(disk_io_mode))
    {
    }
};

/** Facts about a torrent file committed by a successful creation operation. */
struct CreateResult
{
    std::filesystem::path target_path;
    TorrentFormat format;
    InfoHashes info_hashes;
    std::uint64_t payload_bytes;
    std::uint32_t piece_length;
};

/** Input for a side-effect-free creation estimate. */
struct CreatePlanRequest
{
    std::filesystem::path content_root;
    CreateOptions options;

    CreatePlanRequest(std::filesystem::path content_root, CreateOptions options)
        : content_root(std::move(content_root)), options(std::move(options))
    {
    }
};

/** Creation facts shown by the GUI before hashing or writing any output. */
struct CreatePlan
{
    std::uint64_t payload_bytes{};
    std::uint32_t piece_length{};
    std::uint64_t piece_count{};
};

/** Stateless synchronous torrent creation, inspection, and verification service. */
class TorrentEngine
{
  public:
    [[nodiscard]] Result<InspectionReport> inspect(const TorrentDocument& document,
                                                   const TaskContext& context = {}) const;
    [[nodiscard]] Result<CreateResult> create(const CreateRequest& request,
                                              const TaskContext& context = {}) const;
    [[nodiscard]] Result<CreatePlan> plan_create(const CreatePlanRequest& request,
                                                 const TaskContext& context = {}) const;
    [[nodiscard]] Result<VerificationReport> verify(const VerifyRequest& request,
                                                    const TaskContext& context = {}) const;
};

} // namespace torrentutils::core
