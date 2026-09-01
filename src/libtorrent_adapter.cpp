#include "libtorrent_adapter.hpp"

#include "inspection_capabilities.hpp"
#include "metadata_engine.hpp"
#include "torrent_engine_fault_injection.hpp"
#include "verification_progress_publisher.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/mmap_disk_io.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace torrentutils::core::detail {
namespace {

namespace lt = libtorrent;

static_assert(lt::version_major == 2);
static_assert(lt::version_minor == 0);
static_assert(lt::version_tiny == 14);

Error filesystem_error(std::string message, const std::error_code& error)
{
    if (error == std::errc::no_such_file_or_directory)
    {
        return {ErrorCode::FileNotFound, std::move(message), {}};
    }
    if (error == std::errc::permission_denied)
    {
        return {ErrorCode::AccessDenied, std::move(message), {}};
    }
    return {ErrorCode::IoFailure, std::move(message), {}};
}

Error libtorrent_error(std::string message, const lt::error_code& error)
{
    if (error == std::errc::no_such_file_or_directory)
    {
        return {ErrorCode::FileNotFound, std::move(message), {}};
    }
    if (error == std::errc::permission_denied)
    {
        return {ErrorCode::AccessDenied, std::move(message), {}};
    }
    return {ErrorCode::IoFailure, std::move(message), {}};
}

std::filesystem::path read_symlink_with_faults(const std::filesystem::path& path,
                                               std::error_code& error)
{
    if (torrent_engine_fault_is_active(TorrentEngineFault::ReadSymlinkAccessDenied))
    {
        error = std::make_error_code(std::errc::permission_denied);
        return {};
    }
    return std::filesystem::read_symlink(path, error);
}

lt::create_flags_t creation_flags(TorrentFormat format)
{
    switch (format)
    {
    case TorrentFormat::V1:
        return lt::create_torrent::v1_only;
    case TorrentFormat::V2:
        return lt::create_torrent::v2_only;
    case TorrentFormat::Hybrid:
        return {};
    }
    return {};
}

bool is_ascii_digit(const unsigned char value) noexcept
{
    return value >= static_cast<unsigned char>('0') && value <= static_cast<unsigned char>('9');
}

int compare_path_bytes(const std::string& left, const std::string& right) noexcept
{
    const auto count = (std::min)(left.size(), right.size());
    for (std::size_t index{}; index < count; ++index)
    {
        const auto left_byte = static_cast<unsigned char>(left[index]);
        const auto right_byte = static_cast<unsigned char>(right[index]);
        if (left_byte != right_byte)
            return left_byte < right_byte ? -1 : 1;
    }
    if (left.size() == right.size())
        return 0;
    return left.size() < right.size() ? -1 : 1;
}

std::string normalized_torrent_path(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

int compare_natural_run(const std::string& left, std::size_t& left_offset, const std::string& right,
                        std::size_t& right_offset)
{
    const auto left_begin = left_offset;
    const auto right_begin = right_offset;
    while (left_offset < left.size() &&
           is_ascii_digit(static_cast<unsigned char>(left[left_offset])))
        ++left_offset;
    while (right_offset < right.size() &&
           is_ascii_digit(static_cast<unsigned char>(right[right_offset])))
        ++right_offset;

    auto left_digits = left_begin;
    auto right_digits = right_begin;
    while (left_digits < left_offset && left[left_digits] == '0')
        ++left_digits;
    while (right_digits < right_offset && right[right_digits] == '0')
        ++right_digits;
    const auto left_significant = left_offset - left_digits;
    const auto right_significant = right_offset - right_digits;
    if (left_significant != right_significant)
        return left_significant < right_significant ? -1 : 1;
    return compare_path_bytes(left.substr(left_digits, left_significant),
                              right.substr(right_digits, right_significant));
}

bool natural_path_less(const std::string& left, const std::string& right)
{
    std::size_t left_offset{};
    std::size_t right_offset{};
    while (left_offset < left.size() && right_offset < right.size())
    {
        const auto left_digit = is_ascii_digit(static_cast<unsigned char>(left[left_offset]));
        const auto right_digit = is_ascii_digit(static_cast<unsigned char>(right[right_offset]));
        if (left_digit && right_digit)
        {
            const auto comparison = compare_natural_run(left, left_offset, right, right_offset);
            if (comparison != 0)
                return comparison < 0;
            continue;
        }
        if (left[left_offset] != right[right_offset])
            return static_cast<unsigned char>(left[left_offset]) <
                   static_cast<unsigned char>(right[right_offset]);
        ++left_offset;
        ++right_offset;
    }
    return compare_path_bytes(left, right) < 0;
}

std::size_t path_depth(const std::string& path) noexcept
{
    return static_cast<std::size_t>(std::count(path.begin(), path.end(), '/'));
}

bool file_path_less(const lt::file_storage& files, const lt::file_index_t left,
                    const lt::file_index_t right, const FileOrderPolicy policy)
{
    const auto left_path = normalized_torrent_path(files.file_path(left));
    const auto right_path = normalized_torrent_path(files.file_path(right));
    switch (policy)
    {
    case FileOrderPolicy::Natural:
        return natural_path_less(left_path, right_path);
    case FileOrderPolicy::BreadthFirst:
        if (const auto left_depth = path_depth(left_path); left_depth != path_depth(right_path))
            return left_depth < path_depth(right_path);
        return compare_path_bytes(left_path, right_path) < 0;
    case FileOrderPolicy::Lexicographical:
    case FileOrderPolicy::CanonicalAlignment:
        return compare_path_bytes(left_path, right_path) < 0;
    }
    return compare_path_bytes(left_path, right_path) < 0;
}

std::filesystem::path temporary_sibling(const std::filesystem::path& target)
{
    static std::atomic<std::uint64_t> sequence{};
    auto name = target.filename();
    name += (".torrentutils.tmp." + std::to_string(sequence.fetch_add(1)));
    return target.parent_path() / name;
}

Result<std::filesystem::path>
unavailable_verification_path(const std::filesystem::path& content_root)
{
    static std::atomic<std::uint64_t> sequence{};
    for (;;)
    {
        auto filename = content_root.filename();
        filename += (".torrentutils.verify.unavailable." + std::to_string(sequence.fetch_add(1)));
        const auto candidate = content_root.parent_path() / filename;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (error == std::errc::no_such_file_or_directory ||
            status.type() == std::filesystem::file_type::not_found)
        {
            return Result<std::filesystem::path>::success(filename);
        }
        if (error)
        {
            return Result<std::filesystem::path>::failure(
                filesystem_error("could not prepare verification file mapping", error));
        }
    }
}

std::error_code commit_file(const std::filesystem::path& source,
                            const std::filesystem::path& target, bool allow_overwrite)
{
#ifdef _WIN32
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (allow_overwrite)
    {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    if (MoveFileExW(source.c_str(), target.c_str(), flags) == 0)
    {
        return {static_cast<int>(GetLastError()), std::system_category()};
    }
    return {};
#else
    static_cast<void>(allow_overwrite);
    std::error_code error;
    std::filesystem::rename(source, target, error);
    return error;
#endif
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

    PendingFile(const PendingFile&) = delete;
    PendingFile& operator=(const PendingFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

    void committed() noexcept
    {
        committed_ = true;
    }

  private:
    std::filesystem::path path_;
    bool committed_{};
};

Result<InfoHashes> info_hashes(const std::vector<char>& bytes, TorrentFormat format)
{
    if (bytes.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return Result<InfoHashes>::failure(
            {ErrorCode::Internal, "generated torrent metadata is too large", {}});
    }

    lt::error_code error;
    const lt::torrent_info torrent(bytes.data(), static_cast<int>(bytes.size()), error);
    if (error)
    {
        return Result<InfoHashes>::failure(
            {ErrorCode::Internal, "could not inspect generated torrent metadata", {}});
    }

    const auto hashes = torrent.info_hashes();
    std::optional<Sha1Digest> v1;
    if (hashes.has_v1())
    {
        Sha1Digest::Bytes value{};
        std::copy_n(reinterpret_cast<const std::uint8_t*>(hashes.v1.data()), value.size(),
                    value.begin());
        v1 = Sha1Digest::from_bytes(value);
    }

    std::optional<Sha256Digest> v2;
    if (hashes.has_v2())
    {
        Sha256Digest::Bytes value{};
        std::copy_n(reinterpret_cast<const std::uint8_t*>(hashes.v2.data()), value.size(),
                    value.begin());
        v2 = Sha256Digest::from_bytes(value);
    }

    return InfoHashes::create(format, v1, v2);
}

Result<std::shared_ptr<lt::torrent_info>> v1_verification_metadata(const TorrentDocument& document)
{
    const auto& info = document.info();
    if (info.pieces().piece_length() >
            static_cast<std::uint64_t>((std::numeric_limits<int>::max)()) ||
        info.pieces().v1_piece_hashes().size() >
            static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        info.files().size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return Result<std::shared_ptr<lt::torrent_info>>::failure(
            {ErrorCode::UnsupportedFeature,
             "torrent verification layout exceeds backend limits",
             {}});
    }

    lt::file_storage files;
    const bool single_file = info.files().size() == 1U;
    for (const auto& file : info.files())
    {
        auto torrent_path = file.path().to_string();
        if (!single_file)
        {
            torrent_path.insert(0, info.name());
            torrent_path.insert(info.name().size(), 1, '/');
        }

        lt::file_flags_t flags;
        if (file.attributes().padding)
        {
            flags |= lt::file_storage::flag_pad_file;
        }
        if (file.attributes().hidden)
        {
            flags |= lt::file_storage::flag_hidden;
        }
        if (file.attributes().executable)
        {
            flags |= lt::file_storage::flag_executable;
        }
        std::string symlink_target;
        if (file.attributes().symlink)
        {
            const auto& target = file.symlink_target();
            if (!target)
            {
                return Result<std::shared_ptr<lt::torrent_info>>::failure(
                    {ErrorCode::Internal,
                     "BEP 47 symlink verification metadata has no target",
                     {}});
            }
            flags |= lt::file_storage::flag_symlink;
            symlink_target = target->to_string();
        }
        if (file.length() > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
        {
            return Result<std::shared_ptr<lt::torrent_info>>::failure(
                {ErrorCode::UnsupportedFeature,
                 "torrent verification file length exceeds backend limits",
                 {}});
        }
        files.add_file(torrent_path, static_cast<std::int64_t>(file.length()), flags, 0,
                       symlink_target);
    }

    try
    {
        lt::create_torrent creator(files, static_cast<int>(info.pieces().piece_length()),
                                   lt::create_torrent::v1_only);
        const auto& hashes = info.pieces().v1_piece_hashes();
        for (std::size_t index = 0; index < hashes.size(); ++index)
        {
            const auto& bytes = hashes[index].bytes();
            creator.set_hash(lt::piece_index_t{static_cast<int>(index)},
                             lt::sha1_hash(reinterpret_cast<const char*>(bytes.data())));
        }
        const auto encoded = creator.generate_buf();
        if (encoded.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return Result<std::shared_ptr<lt::torrent_info>>::failure(
                {ErrorCode::Internal, "verification metadata is too large", {}});
        }
        lt::error_code error;
        auto metadata = std::make_shared<lt::torrent_info>(encoded.data(),
                                                           static_cast<int>(encoded.size()), error);
        if (error)
        {
            return Result<std::shared_ptr<lt::torrent_info>>::failure(
                libtorrent_error("could not prepare torrent verification metadata", error));
        }
        return Result<std::shared_ptr<lt::torrent_info>>::success(std::move(metadata));
    }
    catch (const std::system_error& exception)
    {
        return Result<std::shared_ptr<lt::torrent_info>>::failure(
            {ErrorCode::Internal,
             "could not prepare torrent verification metadata: " + exception.code().message(),
             {}});
    }
}

bool verification_metadata_is_available(const TorrentDocument& document)
{
    return document.info().format() == TorrentFormat::V1 ||
           static_cast<bool>(retained_torrent_bytes(document));
}

Result<std::shared_ptr<lt::torrent_info>> verification_metadata(const TorrentDocument& document)
{
    if (document.info().format() == TorrentFormat::V1)
    {
        return v1_verification_metadata(document);
    }

    if (!verification_metadata_is_available(document))
    {
        return Result<std::shared_ptr<lt::torrent_info>>::failure(
            {ErrorCode::UnsupportedFeature,
             "v2 and hybrid verification requires retained torrent metadata",
             {}});
    }

    const auto bytes = retained_torrent_bytes(document);
    if (bytes->size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return Result<std::shared_ptr<lt::torrent_info>>::failure(
            {ErrorCode::UnsupportedFeature,
             "torrent verification metadata exceeds backend limits",
             {}});
    }

    lt::error_code error;
    auto metadata = std::make_shared<lt::torrent_info>(reinterpret_cast<const char*>(bytes->data()),
                                                       static_cast<int>(bytes->size()), error);
    if (error)
    {
        return Result<std::shared_ptr<lt::torrent_info>>::failure(
            libtorrent_error("could not prepare torrent verification metadata", error));
    }
    return Result<std::shared_ptr<lt::torrent_info>>::success(std::move(metadata));
}

struct VerificationFilePreflight
{
    std::uint64_t hashable_bytes;
    FileVerificationFinding finding;
};

Result<std::vector<VerificationFilePreflight>>
inspect_verification_files(const VerifyRequest& request)
{
    const auto& files = request.document.info().files();
    const bool single_file = files.size() == 1U;
    const auto torrent_root =
        single_file ? request.content_root.parent_path() : request.content_root;
    std::vector<VerificationFilePreflight> result;
    result.reserve(files.size());
    for (const auto& file : files)
    {
        if (file.attributes().padding)
        {
            result.push_back({file.length(), FileVerificationFinding::None});
            continue;
        }

        const auto physical_path =
            single_file ? request.content_root
                        : request.content_root / std::filesystem::u8path(file.path().to_string());
        std::error_code error;
        const auto status = std::filesystem::symlink_status(physical_path, error);
        if (error == std::errc::no_such_file_or_directory ||
            status.type() == std::filesystem::file_type::not_found)
        {
            const auto finding = file.attributes().symlink ? FileVerificationFinding::SymlinkMissing
                                                           : FileVerificationFinding::Missing;
            result.push_back({0, finding});
            continue;
        }
        if (error)
        {
            return Result<std::vector<VerificationFilePreflight>>::failure(
                filesystem_error("could not inspect verification file", error));
        }

        if (file.attributes().symlink)
        {
            if (!std::filesystem::is_symlink(status))
            {
                result.push_back({0, FileVerificationFinding::SymlinkMissing});
                continue;
            }
            const auto& declared_target = file.symlink_target();
            if (!declared_target)
            {
                return Result<std::vector<VerificationFilePreflight>>::failure(
                    {ErrorCode::Internal, "BEP 47 symlink has no declared target", {}});
            }
            const auto raw_target = read_symlink_with_faults(physical_path, error);
            if (error == std::errc::no_such_file_or_directory)
            {
                result.push_back({0, FileVerificationFinding::SymlinkMissing});
                continue;
            }
            if (error)
            {
                return Result<std::vector<VerificationFilePreflight>>::failure(
                    filesystem_error("could not read BEP 47 symlink target", error));
            }
            const auto resolved_target =
                raw_target.is_absolute()
                    ? raw_target.lexically_normal()
                    : (physical_path.parent_path() / raw_target).lexically_normal();
            const auto expected_target =
                (torrent_root / std::filesystem::u8path(declared_target->to_string()))
                    .lexically_normal();
            const auto finding = resolved_target == expected_target
                                     ? FileVerificationFinding::None
                                     : FileVerificationFinding::SymlinkTargetMismatch;
            result.push_back({0, finding});
            continue;
        }
        if (!std::filesystem::is_regular_file(status))
        {
            result.push_back({0, FileVerificationFinding::NotRegularFile});
            continue;
        }

        const auto actual_size = std::filesystem::file_size(physical_path, error);
        if (error)
        {
            return Result<std::vector<VerificationFilePreflight>>::failure(
                filesystem_error("could not read verification file length", error));
        }
        if (actual_size != file.length())
        {
            const auto hashable_bytes =
                (std::min)(actual_size, static_cast<std::uintmax_t>(file.length()));
            result.push_back({static_cast<std::uint64_t>(hashable_bytes),
                              FileVerificationFinding::LengthMismatch});
            continue;
        }
        result.push_back({file.length(), FileVerificationFinding::None});
    }
    return Result<std::vector<VerificationFilePreflight>>::success(std::move(result));
}

Error verification_layout_error()
{
    return {ErrorCode::Internal, "torrent verification file layout does not match metadata", {}};
}

Result<VerificationPieceLayouts>
verification_piece_layouts(const TorrentInfo& info, const lt::torrent_info& metadata,
                           const std::vector<VerificationFilePreflight>& preflight)
{
    VerificationPieceLayouts layouts(static_cast<std::size_t>(metadata.num_pieces()));
    const auto piece_length = static_cast<std::uint64_t>(metadata.piece_length());
    if (info.format() == TorrentFormat::V2)
    {
        std::size_t logical_file_index{};
        const auto& backend_files = metadata.files();
        for (const auto backend_file_index : backend_files.file_range())
        {
            if (backend_files.pad_file_at(backend_file_index))
            {
                continue;
            }
            if (logical_file_index >= info.files().size())
            {
                return Result<VerificationPieceLayouts>::failure(verification_layout_error());
            }

            const auto& file = info.files()[logical_file_index];
            if (static_cast<std::uint64_t>(backend_files.file_size(backend_file_index)) !=
                file.length())
            {
                return Result<VerificationPieceLayouts>::failure(verification_layout_error());
            }

            const auto first_piece =
                static_cast<int>(backend_files.piece_index_at_file(backend_file_index));
            const auto piece_count = backend_files.file_num_pieces(backend_file_index);
            std::uint64_t file_offset{};
            for (int file_piece = 0; file_piece < piece_count; ++file_piece)
            {
                const auto piece = first_piece + file_piece;
                if (piece < 0 || static_cast<std::size_t>(piece) >= layouts.size() ||
                    file_offset >= file.length())
                {
                    return Result<VerificationPieceLayouts>::failure(verification_layout_error());
                }
                const auto bytes = (std::min)(piece_length, file.length() - file_offset);
                auto& layout = layouts[static_cast<std::size_t>(piece)];
                layout.overlaps.push_back({logical_file_index, bytes});
                file_offset += bytes;
                if (file_offset > preflight[logical_file_index].hashable_bytes)
                {
                    layout.hashable = false;
                }
            }
            if (file_offset != file.length())
            {
                return Result<VerificationPieceLayouts>::failure(verification_layout_error());
            }
            ++logical_file_index;
        }
        if (logical_file_index != info.files().size())
        {
            return Result<VerificationPieceLayouts>::failure(verification_layout_error());
        }
        return Result<VerificationPieceLayouts>::success(std::move(layouts));
    }

    for (std::size_t piece = 0; piece < layouts.size(); ++piece)
    {
        const auto piece_begin = static_cast<std::uint64_t>(piece) * piece_length;
        const auto piece_end = (std::min)(piece_begin + piece_length, info.pieces().total_size());
        std::uint64_t file_begin{};
        std::size_t report_file_index{};
        for (std::size_t file_index = 0; file_index < info.files().size(); ++file_index)
        {
            const auto& file = info.files()[file_index];
            const auto file_end = file_begin + file.length();
            const auto overlap_begin = (std::max)(piece_begin, file_begin);
            const auto overlap_end = (std::min)(piece_end, file_end);
            if (!file.attributes().padding && overlap_begin < overlap_end)
            {
                auto& layout = layouts[piece];
                layout.overlaps.push_back({report_file_index, overlap_end - overlap_begin});
                const auto required_file_end = overlap_end - file_begin;
                if (required_file_end > preflight[file_index].hashable_bytes)
                {
                    layout.hashable = false;
                }
            }
            if (!file.attributes().padding)
            {
                ++report_file_index;
            }
            file_begin = file_end;
        }
    }
    return Result<VerificationPieceLayouts>::success(std::move(layouts));
}

VerificationReport make_verification_report(const TorrentDocument& document,
                                            const lt::torrent_status& status,
                                            const std::vector<VerificationFilePreflight>& preflight,
                                            const VerificationPieceLayouts& layouts)
{
    const auto& info = document.info();
    VerificationReport report{VerificationOutcome::Verified, 0, 0, 0, 0, {}};
    report.files.reserve(info.files().size());
    for (std::size_t index = 0; index < info.files().size(); ++index)
    {
        const auto& file = info.files()[index];
        if (file.attributes().padding)
        {
            continue;
        }
        report.expected_bytes += file.length();
        report.files.push_back({file.path(), file.length(), 0, 0, 0, preflight[index].finding});
        if (preflight[index].finding == FileVerificationFinding::SymlinkTargetMismatch)
        {
            if (report.outcome == VerificationOutcome::Verified)
            {
                report.outcome = VerificationOutcome::Mismatched;
            }
        }
        else if (preflight[index].finding != FileVerificationFinding::None)
        {
            report.outcome = VerificationOutcome::Incomplete;
        }
    }

    for (std::size_t piece = 0; piece < layouts.size(); ++piece)
    {
        const auto piece_index = lt::piece_index_t{static_cast<int>(piece)};
        const bool verified = piece_index < status.pieces.end_index() && status.pieces[piece_index];

        const auto& layout = layouts[piece];
        if (!layout.hashable)
        {
            report.outcome = VerificationOutcome::Incomplete;
            continue;
        }

        for (const auto& overlap : layout.overlaps)
        {
            auto& file = report.files[overlap.file_index];
            file.hashed_bytes += overlap.bytes;
            report.hashed_bytes += overlap.bytes;
            if (verified)
            {
                file.verified_bytes += overlap.bytes;
                report.verified_bytes += overlap.bytes;
            }
            else
            {
                file.mismatched_bytes += overlap.bytes;
                report.mismatched_bytes += overlap.bytes;
            }
        }

        if (!verified)
        {
            if (report.outcome == VerificationOutcome::Verified)
            {
                report.outcome = VerificationOutcome::Mismatched;
            }
            const auto finding = layout.overlaps.size() > 1U
                                     ? FileVerificationFinding::SharedPieceMismatch
                                     : FileVerificationFinding::HashMismatch;
            for (const auto& overlap : layout.overlaps)
            {
                report.files[overlap.file_index].findings |= finding;
            }
        }
    }
    return report;
}

std::vector<FileVerificationProgress> verification_progress_files(const TorrentInfo& info)
{
    std::vector<FileVerificationProgress> files;
    files.reserve(info.files().size());
    for (const auto& file : info.files())
    {
        if (!file.attributes().padding)
        {
            files.push_back({file.path(), file.length(), 0, 0, 0});
        }
    }
    return files;
}

std::vector<PieceVerificationState> verification_piece_states(const lt::torrent_status& status,
                                                              std::size_t piece_count)
{
    std::vector<PieceVerificationState> states(piece_count, PieceVerificationState::Mismatched);
    for (std::size_t piece = 0; piece < piece_count; ++piece)
    {
        const auto piece_index = lt::piece_index_t{static_cast<int>(piece)};
        if (piece_index < status.pieces.end_index() && status.pieces[piece_index])
        {
            states[piece] = PieceVerificationState::Verified;
        }
    }
    return states;
}

Result<CreateResult> create_from_storage(const CreateRequest& request, const TaskContext& context,
                                         lt::file_storage files, std::uint64_t payload_bytes)
{
    if (payload_bytes > static_cast<std::uint64_t>(lt::file_storage::max_file_size))
    {
        return Result<CreateResult>::failure(
            {ErrorCode::ValidationFailed,
             "create request validation failed",
             {{"create.content_root", "file is too large for the torrent backend"}}});
    }

    const auto piece_length = request.options.piece_length_for(payload_bytes);
    if (piece_length > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
    {
        return Result<CreateResult>::failure(
            {ErrorCode::Internal, "piece length exceeds backend limits", {}});
    }

    const auto flags = creation_flags(request.options.format()) | lt::create_torrent::symlinks;
    lt::create_torrent creator(files, static_cast<int>(piece_length), flags);
    creator.set_priv(request.options.is_private());
    creator.set_creation_date(request.creation_metadata.creation_time_unix_seconds.value_or(0));
    if (request.creation_metadata.comment && !request.creation_metadata.comment->empty())
    {
        creator.set_comment(request.creation_metadata.comment->c_str());
    }
    if (request.creation_metadata.created_by)
    {
        creator.set_creator(request.creation_metadata.created_by->c_str());
    }
    int tier{};
    for (const auto& tracker_tier : request.options.trackers().tiers())
    {
        for (const auto& tracker : tracker_tier.trackers())
        {
            creator.add_tracker(tracker.value(), tier);
        }
        ++tier;
    }
    for (const auto& web_seed : request.options.web_seeds())
    {
        creator.add_url_seed(web_seed.value());
    }

    lt::error_code hash_error;
    lt::settings_pack hashing_settings;
    auto* disk_io_constructor = request.disk_io_mode == core::DiskIoMode::Posix
                                    ? &lt::posix_disk_io_constructor
                                    : &lt::mmap_disk_io_constructor;
    lt::set_piece_hashes(
        creator, request.content_root.parent_path().u8string(), hashing_settings,
        disk_io_constructor,
        [&context, piece_count = creator.num_pieces(), piece_length,
         payload_bytes](lt::piece_index_t piece) {
            if (context.on_progress)
            {
                const auto completed = static_cast<std::uint64_t>(static_cast<int>(piece)) + 1U;
                const auto completed_bytes =
                    std::min(payload_bytes, completed * static_cast<std::uint64_t>(piece_length));
                context.on_progress({"hashing", completed, static_cast<std::uint64_t>(piece_count),
                                     completed_bytes, payload_bytes});
            }
        },
        hash_error);
    if (hash_error)
    {
        return Result<CreateResult>::failure(
            libtorrent_error("could not hash create content", hash_error));
    }
    if (context.cancellation.is_cancelled())
    {
        return Result<CreateResult>::failure(
            {ErrorCode::Cancelled, "torrent creation was cancelled", {}});
    }

    if (torrent_engine_fault_is_active(TorrentEngineFault::CreateMetadataEncoding))
    {
        return Result<CreateResult>::failure(
            {ErrorCode::Internal,
             "could not generate torrent metadata: injected metadata encoding failure",
             {}});
    }

    std::vector<char> encoded;
    try
    {
        auto torrent = creator.generate();
        if (request.create_info.source && !request.create_info.source->empty())
        {
            torrent["info"]["source"] = *request.create_info.source;
        }
        lt::bencode(std::back_inserter(encoded), torrent);
    }
    catch (const std::system_error& error)
    {
        return Result<CreateResult>::failure(
            {ErrorCode::Internal,
             "could not generate torrent metadata: " + error.code().message(),
             {}});
    }

    auto hashes = info_hashes(encoded, request.options.format());
    if (!hashes)
    {
        return Result<CreateResult>::failure(hashes.error());
    }

    PendingFile pending(temporary_sibling(request.target_path));
    {
        std::ofstream output(pending.path(), std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return Result<CreateResult>::failure(
                {ErrorCode::IoFailure, "could not create temporary torrent file", {}});
        }
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.close();
        if (!output)
        {
            return Result<CreateResult>::failure(
                {ErrorCode::IoFailure, "could not write temporary torrent file", {}});
        }
    }

    const auto commit_error =
        commit_file(pending.path(), request.target_path, request.allow_overwrite);
    if (commit_error)
    {
        return Result<CreateResult>::failure(
            filesystem_error("could not commit torrent file", commit_error));
    }
    pending.committed();

    return Result<CreateResult>::success({request.target_path, request.options.format(),
                                          std::move(hashes).value(), payload_bytes, piece_length});
}

} // namespace

Result<CreateResult> create_regular_file(const CreateRequest& request, const TaskContext& context,
                                         std::uint64_t payload_bytes)
{
    lt::file_storage files;
    files.add_file(request.content_root.filename().generic_u8string(),
                   static_cast<std::int64_t>(payload_bytes));
    return create_from_storage(request, context, std::move(files), payload_bytes);
}

bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end() && candidate_part != candidate.end();
         ++root_part, ++candidate_part)
    {
        if (*root_part != *candidate_part)
        {
            return false;
        }
    }
    return root_part == root.end();
}

Result<std::string> validate_bep47_symlink(const std::filesystem::path& root,
                                           const std::filesystem::path& link)
{
    std::error_code error;
    const auto raw_target = read_symlink_with_faults(link, error);
    if (error)
    {
        return Result<std::string>::failure(
            filesystem_error("could not read BEP 47 symlink target", error));
    }
    if (raw_target.empty() || raw_target.is_absolute())
    {
        return Result<std::string>::failure(
            {ErrorCode::ValidationFailed,
             "create request validation failed",
             {{"create.content_root", "contains a symlink with an external target"}}});
    }

    const auto target_path = (link.parent_path() / raw_target).lexically_normal();
    const auto target_status = std::filesystem::symlink_status(target_path, error);
    if (error == std::errc::no_such_file_or_directory ||
        target_status.type() == std::filesystem::file_type::not_found)
    {
        return Result<std::string>::failure(
            {ErrorCode::ValidationFailed,
             "create request validation failed",
             {{"create.content_root", "contains a dangling symlink"}}});
    }
    if (error)
    {
        return Result<std::string>::failure(
            filesystem_error("could not inspect BEP 47 symlink target", error));
    }
    if (std::filesystem::is_symlink(target_status))
    {
        return Result<std::string>::failure(
            {ErrorCode::ValidationFailed,
             "create request validation failed",
             {{"create.content_root", "contains a symlink whose target is another symlink"}}});
    }
    if (!std::filesystem::is_regular_file(target_status))
    {
        return Result<std::string>::failure(
            {ErrorCode::ValidationFailed,
             "create request validation failed",
             {{"create.content_root", "contains a symlink whose target is not a regular file"}}});
    }

    const auto canonical_root = std::filesystem::canonical(root, error);
    if (error)
    {
        return Result<std::string>::failure(
            filesystem_error("could not resolve create content root", error));
    }
    const auto canonical_target = std::filesystem::canonical(target_path, error);
    if (error)
    {
        return Result<std::string>::failure(
            filesystem_error("could not resolve BEP 47 symlink target", error));
    }
    if (!path_is_within(canonical_root, canonical_target))
    {
        return Result<std::string>::failure(
            {ErrorCode::ValidationFailed,
             "create request validation failed",
             {{"create.content_root", "contains a symlink with an external target"}}});
    }

    return Result<std::string>::success(
        canonical_target.lexically_relative(canonical_root).generic_u8string());
}

struct PreparedDirectory
{
    lt::file_storage files;
    std::uint64_t payload_bytes{};
};

Result<PreparedDirectory> prepare_regular_directory(const CreateRequest& request)
{
    std::error_code error;
    std::uint64_t payload_bytes{};
    std::unordered_map<std::string, std::string> symlink_targets;
    std::filesystem::recursive_directory_iterator entry(request.content_root, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error)
    {
        return Result<PreparedDirectory>::failure(
            filesystem_error("could not enumerate create content root", error));
    }

    for (; entry != end; entry.increment(error))
    {
        if (error)
        {
            return Result<PreparedDirectory>::failure(
                filesystem_error("could not enumerate create content root", error));
        }

        const auto status = entry->symlink_status(error);
        if (error)
        {
            return Result<PreparedDirectory>::failure(
                filesystem_error("could not inspect create content entry", error));
        }
        if (std::filesystem::is_symlink(status))
        {
            auto link_target = validate_bep47_symlink(request.content_root, entry->path());
            if (!link_target)
            {
                return Result<PreparedDirectory>::failure(link_target.error());
            }
            const auto relative_path = entry->path().lexically_relative(request.content_root);
            const auto torrent_path =
                (request.content_root.filename() / relative_path).generic_u8string();
            symlink_targets.emplace(torrent_path, std::move(link_target).value());
            continue;
        }
        if (std::filesystem::is_directory(status))
        {
            continue;
        }
        if (!std::filesystem::is_regular_file(status))
        {
            return Result<PreparedDirectory>::failure(
                {ErrorCode::ValidationFailed,
                 "create request validation failed",
                 {{"create.content_root", "contains an unsupported filesystem entry"}}});
        }

        const auto size = entry->file_size(error);
        if (error)
        {
            return Result<PreparedDirectory>::failure(
                filesystem_error("could not read create content length", error));
        }
        if (size > (std::numeric_limits<std::uint64_t>::max)() - payload_bytes)
        {
            return Result<PreparedDirectory>::failure(
                {ErrorCode::ValidationFailed,
                 "create request validation failed",
                 {{"create.content_root", "payload size exceeds supported limits"}}});
        }
        payload_bytes += size;
    }

    if (payload_bytes == 0)
    {
        return Result<PreparedDirectory>::failure(
            {ErrorCode::ValidationFailed,
             "create request validation failed",
             {{"create.content_root", "must contain at least one non-empty regular file"}}});
    }

    lt::file_storage files;
    try
    {
        const auto flags = creation_flags(request.options.format()) | lt::create_torrent::symlinks;
        lt::add_files(files, request.content_root.u8string(), flags);

        std::vector<lt::file_index_t> sorted_indices;
        sorted_indices.reserve(static_cast<std::size_t>(files.num_files()));
        for (const auto index : files.file_range())
        {
            sorted_indices.push_back(index);
        }
        std::sort(sorted_indices.begin(), sorted_indices.end(),
                  [&files, policy = request.options.file_order_policy()](const auto left,
                                                                         const auto right) {
                      return file_path_less(files, left, right, policy);
                  });

        lt::file_storage normalized_files;
        for (const auto index : sorted_indices)
        {
            const auto file_path = files.file_path(index);
            auto normalized_file_path = file_path;
            std::replace(normalized_file_path.begin(), normalized_file_path.end(), '\\', '/');
            const auto symlink = symlink_targets.find(normalized_file_path);
            auto file_flags = files.file_flags(index);
            auto file_size = files.file_size(index);
            std::string symlink_target;
            if (symlink != symlink_targets.end())
            {
                file_flags |= lt::file_storage::flag_symlink;
                file_size = 0;
                symlink_target = std::move(symlink->second);
                symlink_targets.erase(symlink);
            }
            else if (file_flags & lt::file_storage::flag_symlink)
            {
                const auto link_path =
                    request.content_root.parent_path() / std::filesystem::u8path(file_path);
                auto normalized_target = validate_bep47_symlink(request.content_root, link_path);
                if (!normalized_target)
                {
                    return Result<PreparedDirectory>::failure(normalized_target.error());
                }
                symlink_target = std::move(normalized_target).value();
            }
            normalized_files.add_file(file_path, file_size, file_flags, files.mtime(index),
                                      symlink_target);
        }
        if (!symlink_targets.empty())
        {
            return Result<PreparedDirectory>::failure(
                {ErrorCode::ValidationFailed,
                 "create request validation failed",
                 {{"create.content_root",
                   "contains a symlink target that is not represented in the torrent layout"}}});
        }
        files = std::move(normalized_files);
        if (request.options.file_order_policy() == FileOrderPolicy::CanonicalAlignment &&
            request.options.format() != TorrentFormat::V1)
        {
            files.set_piece_length(
                static_cast<int>(request.options.piece_length_for(payload_bytes)));
            files.canonicalize();
        }
    }
    catch (const std::system_error& exception)
    {
        return Result<PreparedDirectory>::failure(
            {ErrorCode::IoFailure,
             "could not enumerate create content: " + exception.code().message(),
             {}});
    }
    return Result<PreparedDirectory>::success({std::move(files), payload_bytes});
}

Result<CreateResult> create_regular_directory(const CreateRequest& request,
                                              const TaskContext& context)
{
    auto prepared = prepare_regular_directory(request);
    if (!prepared)
    {
        return Result<CreateResult>::failure(std::move(prepared).error());
    }
    auto directory = std::move(prepared).value();
    return create_from_storage(request, context, std::move(directory.files),
                               directory.payload_bytes);
}

Result<CreatePlan> LibtorrentAdapter::plan_create(const CreatePlanRequest& request,
                                                  const TaskContext& context)
{
    std::error_code error;
    const auto status = std::filesystem::status(request.content_root, error);
    if (error)
    {
        if (error == std::errc::no_such_file_or_directory)
        {
            return Result<CreatePlan>::failure(
                {ErrorCode::FileNotFound, "create content root does not exist", {}});
        }
        return Result<CreatePlan>::failure(
            filesystem_error("could not inspect create content root", error));
    }
    if (status.type() == std::filesystem::file_type::not_found)
    {
        return Result<CreatePlan>::failure(
            {ErrorCode::FileNotFound, "create content root does not exist", {}});
    }
    if (context.cancellation.is_cancelled())
    {
        return Result<CreatePlan>::failure(
            {ErrorCode::Cancelled, "create plan calculation was cancelled", {}});
    }

    std::uint64_t payload_bytes{};
    lt::file_storage files;
    if (std::filesystem::is_regular_file(status))
    {
        payload_bytes = std::filesystem::file_size(request.content_root, error);
        if (error)
        {
            return Result<CreatePlan>::failure(
                filesystem_error("could not read create content length", error));
        }
        files.add_file(request.content_root.filename().generic_u8string(),
                       static_cast<std::int64_t>(payload_bytes));
    }
    else if (std::filesystem::is_directory(status))
    {
        auto prepared =
            prepare_regular_directory(CreateRequest{request.content_root, {}, request.options});
        if (!prepared)
        {
            return Result<CreatePlan>::failure(std::move(prepared).error());
        }
        auto directory = std::move(prepared).value();
        payload_bytes = directory.payload_bytes;
        files = std::move(directory.files);
    }
    else
    {
        return Result<CreatePlan>::failure(
            {ErrorCode::ValidationFailed,
             "create request validation failed",
             {{"create.content_root", "must be a regular file or directory"}}});
    }

    const auto piece_length = request.options.piece_length_for(payload_bytes);
    if (piece_length == 0)
    {
        return Result<CreatePlan>::failure(
            {ErrorCode::ValidationFailed, "piece length must be positive", {}});
    }
    if (piece_length > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()))
    {
        return Result<CreatePlan>::failure(
            {ErrorCode::Internal, "piece length exceeds backend limits", {}});
    }

    try
    {
        const auto flags = creation_flags(request.options.format()) | lt::create_torrent::symlinks;
        const lt::create_torrent creator(files, static_cast<int>(piece_length), flags);
        return Result<CreatePlan>::success(
            {payload_bytes, piece_length, static_cast<std::uint64_t>(creator.num_pieces())});
    }
    catch (const std::system_error& exception)
    {
        return Result<CreatePlan>::failure(
            {ErrorCode::IoFailure,
             "could not prepare create plan: " + exception.code().message(),
             {}});
    }
}

Result<VerificationBackendCapabilities> libtorrent_verification_backend_capabilities()
{
    if (torrent_engine_fault_is_active(TorrentEngineFault::InspectBackendInitialization))
    {
        return Result<VerificationBackendCapabilities>::failure(
            {ErrorCode::Internal, "verification capability assessment failed", {}});
    }

    VerificationBackendCapabilities capabilities;
    capabilities.max_file_size = static_cast<std::uint64_t>(lt::file_storage::max_file_size);
    capabilities.max_file_offset = static_cast<std::uint64_t>(lt::file_storage::max_file_offset);
    capabilities.max_file_count = static_cast<std::uint64_t>((std::numeric_limits<int>::max)());
    capabilities.max_piece_size = static_cast<std::uint64_t>(lt::file_storage::max_piece_size);
    capabilities.max_piece_count = static_cast<std::uint64_t>(lt::file_storage::max_num_pieces);
    if (torrent_engine_fault_is_active(TorrentEngineFault::InspectRestrictedCapabilities))
    {
        capabilities.verification = false;
        capabilities.v2_format = false;
        capabilities.file_attributes = false;
        capabilities.bep47_symlinks = false;
        capabilities.max_file_size = 512;
        capabilities.max_file_offset = 512;
        capabilities.max_piece_count = 0;
    }
    return Result<VerificationBackendCapabilities>::success(capabilities);
}

Result<InspectionReport>
inspect_verification_capability(const TorrentDocument& document,
                                const VerificationBackendCapabilitiesProvider& provider)
{
    auto provided = provider();
    if (!provided)
    {
        return Result<InspectionReport>::failure(provided.error());
    }
    const auto& capabilities = provided.value();
    const auto& info = document.info();
    std::vector<CapabilityDiagnostic> diagnostics;

    const bool format_supported =
        (info.format() == TorrentFormat::V1 && capabilities.v1_format) ||
        (info.format() == TorrentFormat::V2 && capabilities.v2_format) ||
        (info.format() == TorrentFormat::Hybrid && capabilities.hybrid_format);
    if (!format_supported)
    {
        diagnostics.push_back({VerificationCapabilityDiagnosticCode::UnsupportedTorrentFormat,
                               "torrent format is unavailable for verification"});
    }

    if (!verification_metadata_is_available(document))
    {
        diagnostics.push_back({VerificationCapabilityDiagnosticCode::UnsupportedPieceHashScheme,
                               "v2 piece layer metadata is unavailable for verification"});
    }

    const auto piece_length = info.pieces().piece_length();
    const auto piece_count = info.pieces().total_size() / piece_length +
                             (info.pieces().total_size() % piece_length == 0U ? 0U : 1U);
    bool unsupported_layout = info.files().size() > capabilities.max_file_count ||
                              piece_length > capabilities.max_piece_size ||
                              piece_count > capabilities.max_piece_count;
    std::uint64_t file_offset{};
    for (const auto& file : info.files())
    {
        if (file.length() > capabilities.max_file_size ||
            file_offset > capabilities.max_file_offset ||
            file.length() > capabilities.max_file_offset -
                                (std::min)(file_offset, capabilities.max_file_offset))
        {
            unsupported_layout = true;
        }
        file_offset += file.length();
        if (file_offset > capabilities.max_file_offset)
        {
            unsupported_layout = true;
        }
    }
    if (unsupported_layout)
    {
        diagnostics.push_back({VerificationCapabilityDiagnosticCode::UnsupportedFileLayout,
                               "torrent file layout exceeds verification limits"});
    }

    const bool needs_file_attributes =
        std::any_of(info.files().begin(), info.files().end(), [](const FileEntry& file) {
            const auto attributes = file.attributes();
            return attributes.padding || attributes.executable || attributes.hidden;
        });
    if (needs_file_attributes && !capabilities.file_attributes)
    {
        diagnostics.push_back({VerificationCapabilityDiagnosticCode::UnsupportedFileAttribute,
                               "torrent file attributes are unavailable for verification"});
    }

    const bool needs_symlinks =
        std::any_of(info.files().begin(), info.files().end(),
                    [](const FileEntry& file) { return file.attributes().symlink; });
    if (needs_symlinks && !capabilities.bep47_symlinks)
    {
        diagnostics.push_back({VerificationCapabilityDiagnosticCode::UnsupportedSymlinkSemantics,
                               "BEP 47 symlink semantics are unavailable for verification"});
    }

    if (!capabilities.verification)
    {
        diagnostics.push_back({VerificationCapabilityDiagnosticCode::BackendFeatureUnavailable,
                               "required verification capability is unavailable"});
    }

    const auto capability = diagnostics.empty() ? VerificationCapability::Supported
                                                : VerificationCapability::Unsupported;
    return Result<InspectionReport>::success({capability, std::move(diagnostics)});
}

Result<InspectionReport> LibtorrentAdapter::inspect(const TorrentDocument& document)
{
    return inspect_verification_capability(document, libtorrent_verification_backend_capabilities);
}

Result<void> validate_verification_resource_budget(const VerifyRequest& request)
{
    if (!request.resource_budget)
    {
        return Result<void>::success();
    }

    const auto& budget = *request.resource_budget;
    const auto& info = request.document.info();
    const auto piece_length = info.pieces().piece_length();
    const auto total_size = info.pieces().total_size();
    const auto piece_count =
        total_size / piece_length + (total_size % piece_length == 0U ? 0U : 1U);
    if (info.files().size() > budget.max_logical_files())
    {
        return Result<void>::failure(
            {ErrorCode::ResourceLimitExceeded,
             "torrent logical file count exceeds verification resource budget",
             {}});
    }
    if (piece_count > budget.max_pieces())
    {
        return Result<void>::failure({ErrorCode::ResourceLimitExceeded,
                                      "torrent piece count exceeds verification resource budget",
                                      {}});
    }
    return Result<void>::success();
}
/**
 * Configure the current local-only operation without removing libtorrent's
 * network settings from the backend. This block is intentionally reversible
 * when a future operation needs tracker/DHT or peer networking.
 */
void configure_local_only_session(lt::settings_pack& settings)
{
    settings.set_str(lt::settings_pack::listen_interfaces, "");
    settings.set_bool(lt::settings_pack::enable_outgoing_tcp, false);
    settings.set_bool(lt::settings_pack::enable_incoming_tcp, false);
    settings.set_bool(lt::settings_pack::enable_outgoing_utp, false);
    settings.set_bool(lt::settings_pack::enable_incoming_utp, false);
    settings.set_bool(lt::settings_pack::enable_dht, false);
    settings.set_bool(lt::settings_pack::enable_lsd, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
}

Result<void> apply_verification_resource_budget(const VerifyRequest& request,
                                                lt::settings_pack& settings)
{
    if (!request.resource_budget)
    {
        return Result<void>::success();
    }

    const auto& budget = *request.resource_budget;
    constexpr std::uint64_t checking_memory_block_bytes = 16ULL * 1024ULL;
    const auto checking_blocks =
        (budget.checking_memory_bytes() + checking_memory_block_bytes - 1U) /
        checking_memory_block_bytes;
    if (budget.hashing_workers() > static_cast<std::uint32_t>((std::numeric_limits<int>::max)()) ||
        checking_blocks > static_cast<std::uint64_t>((std::numeric_limits<int>::max)()))
    {
        return Result<void>::failure(
            {ErrorCode::ResourceLimitExceeded,
             "verification resource budget cannot be represented by the backend",
             {}});
    }

    settings.set_int(lt::settings_pack::hashing_threads,
                     static_cast<int>(budget.hashing_workers()));
    settings.set_int(lt::settings_pack::checking_mem_usage, static_cast<int>(checking_blocks));

    return Result<void>::success();
}

Result<CreateResult> LibtorrentAdapter::create(const CreateRequest& request,
                                               const TaskContext& context)
{
    std::error_code error;
    const auto status = std::filesystem::status(request.content_root, error);
    if (error)
    {
        if (error == std::errc::no_such_file_or_directory)
        {
            return Result<CreateResult>::failure(
                {ErrorCode::FileNotFound, "create content root does not exist", {}});
        }
        return Result<CreateResult>::failure(
            filesystem_error("could not inspect create content root", error));
    }
    if (status.type() == std::filesystem::file_type::not_found)
    {
        return Result<CreateResult>::failure(
            {ErrorCode::FileNotFound, "create content root does not exist", {}});
    }

    std::error_code target_error;
    const bool target_exists = std::filesystem::exists(request.target_path, target_error);
    if (target_error)
    {
        return Result<CreateResult>::failure(
            filesystem_error("could not inspect create target", target_error));
    }
    if (target_exists && !request.allow_overwrite)
    {
        return Result<CreateResult>::failure(
            {ErrorCode::Conflict, "create target already exists", {}});
    }
    if (std::filesystem::is_regular_file(status))
    {
        const auto size = std::filesystem::file_size(request.content_root, error);
        if (error)
        {
            return Result<CreateResult>::failure(
                filesystem_error("could not read create content length", error));
        }
        return create_regular_file(request, context, size);
    }
    if (std::filesystem::is_directory(status))
    {
        return create_regular_directory(request, context);
    }

    return Result<CreateResult>::failure(
        {ErrorCode::ValidationFailed,
         "create request validation failed",
         {{"create.content_root", "must be a regular file or directory"}}});
}

Result<VerificationReport> LibtorrentAdapter::verify(const VerifyRequest& request,
                                                     const TaskContext& context)
{
    auto inspection = inspect(request.document);
    if (!inspection)
    {
        return Result<VerificationReport>::failure(inspection.error());
    }
    if (inspection.value().verification_capability == VerificationCapability::Unsupported)
    {
        return Result<VerificationReport>::failure(
            {ErrorCode::UnsupportedFeature,
             "torrent document requires unsupported verification capabilities",
             {}});
    }

    auto budget = validate_verification_resource_budget(request);
    if (!budget)
    {
        return Result<VerificationReport>::failure(budget.error());
    }

    auto metadata = verification_metadata(request.document);
    if (!metadata)
    {
        return Result<VerificationReport>::failure(metadata.error());
    }

    auto preflight = inspect_verification_files(request);
    if (!preflight)
    {
        return Result<VerificationReport>::failure(preflight.error());
    }
    const auto file_preflight = std::move(preflight).value();
    auto torrent_metadata = std::move(metadata).value();
    auto layouts =
        verification_piece_layouts(request.document.info(), *torrent_metadata, file_preflight);
    if (!layouts)
    {
        return Result<VerificationReport>::failure(layouts.error());
    }
    const auto piece_layouts = std::move(layouts).value();
    const bool single_file = request.document.info().files().size() == 1U;
    std::unique_ptr<VerificationProgressPublisher> progress;
    if (request.on_progress)
    {
        if (torrent_engine_fault_is_active(TorrentEngineFault::VerifyProgressPublisherConstruction))
        {
            return Result<VerificationReport>::failure(
                {ErrorCode::Internal,
                 "could not initialize verification progress delivery: injected publisher "
                 "construction "
                 "failure",
                 {}});
        }
        progress = std::make_unique<VerificationProgressPublisher>(
            verification_progress_files(request.document.info()), piece_layouts,
            request.on_progress, context.cancellation);
    }

    lt::settings_pack settings;
    configure_local_only_session(settings);
    auto alert_mask =
        lt::alert_category::status | lt::alert_category::error | lt::alert_category::storage;
    auto applied_budget = apply_verification_resource_budget(request, settings);
    if (!applied_budget)
    {
        return Result<VerificationReport>::failure(applied_budget.error());
    }
    if (progress)
    {
        alert_mask |= lt::alert_category::piece_progress;
    }
    settings.set_int(lt::settings_pack::alert_mask,
                     static_cast<int>(static_cast<std::uint32_t>(alert_mask)));

    lt::session_params session_parameters(settings);
    session_parameters.disk_io_constructor = request.disk_io_mode == core::DiskIoMode::Posix
                                                 ? &lt::posix_disk_io_constructor
                                                 : &lt::mmap_disk_io_constructor;
    lt::session session(session_parameters);
    lt::add_torrent_params parameters;
    parameters.ti = std::move(torrent_metadata);
    parameters.save_path = request.content_root.parent_path().u8string();
    parameters.flags = lt::torrent_flags::stop_when_ready;
    std::size_t logical_file_index{};
    for (const auto index : parameters.ti->files().file_range())
    {
        if (request.document.info().format() == TorrentFormat::V2 &&
            parameters.ti->files().pad_file_at(index))
        {
            continue;
        }
        if (logical_file_index >= request.document.info().files().size())
        {
            return Result<VerificationReport>::failure(
                {ErrorCode::Internal,
                 "torrent verification file layout does not match metadata",
                 {}});
        }
        const auto file_index = logical_file_index++;
        const auto logical_path = request.document.info().files()[file_index].path();
        auto physical_path = single_file ? request.content_root.filename()
                                         : request.content_root.filename() /
                                               std::filesystem::u8path(logical_path.to_string());
        if (file_preflight[file_index].finding == FileVerificationFinding::NotRegularFile)
        {
            auto unavailable_path = unavailable_verification_path(request.content_root);
            if (!unavailable_path)
            {
                return Result<VerificationReport>::failure(unavailable_path.error());
            }
            physical_path = std::move(unavailable_path).value();
        }
        parameters.renamed_files.emplace(index, physical_path.generic_u8string());
    }
    if (logical_file_index != request.document.info().files().size())
    {
        return Result<VerificationReport>::failure(
            {ErrorCode::Internal, "torrent verification file layout does not match metadata", {}});
    }

    lt::error_code add_error;
    const auto handle = session.add_torrent(std::move(parameters), add_error);
    if (add_error)
    {
        return Result<VerificationReport>::failure(
            libtorrent_error("could not start torrent verification", add_error));
    }

    if (torrent_engine_fault_is_active(TorrentEngineFault::VerifyBackendIo))
    {
        return Result<VerificationReport>::failure(
            {ErrorCode::IoFailure,
             "could not verify torrent content: injected backend I/O failure",
             {}});
    }

    bool checked{};
    while (!checked)
    {
        if (context.cancellation.is_cancelled())
        {
            return Result<VerificationReport>::failure(
                {ErrorCode::Cancelled, "torrent verification was cancelled", {}});
        }

        session.wait_for_alert(lt::milliseconds(50));
        std::vector<lt::alert*> alerts;
        session.pop_alerts(&alerts);
        for (const auto* alert : alerts)
        {
            if (const auto* file_error = lt::alert_cast<lt::file_error_alert>(alert))
            {
                return Result<VerificationReport>::failure(
                    libtorrent_error("could not verify torrent content", file_error->error));
            }
            if (progress)
            {
                if (const auto* piece = lt::alert_cast<lt::piece_finished_alert>(alert))
                {
                    progress->record(
                        static_cast<std::uint64_t>(static_cast<int>(piece->piece_index)),
                        PieceVerificationState::Verified);
                }
            }
            if (lt::alert_cast<lt::torrent_checked_alert>(alert) != nullptr)
            {
                checked = true;
            }
        }
        if (progress)
        {
            if (checked)
            {
                const auto completed_status = handle.status(lt::torrent_handle::query_pieces);
                progress->complete(
                    verification_piece_states(completed_status, piece_layouts.size()));
            }
            else
            {
                progress->flush();
            }
        }
        if (context.cancellation.is_cancelled())
        {
            return Result<VerificationReport>::failure(
                {ErrorCode::Cancelled, "torrent verification was cancelled", {}});
        }
    }

    const auto status = handle.status(lt::torrent_handle::query_pieces);
    return Result<VerificationReport>::success(
        make_verification_report(request.document, status, file_preflight, piece_layouts));
}

} // namespace torrentutils::core::detail
