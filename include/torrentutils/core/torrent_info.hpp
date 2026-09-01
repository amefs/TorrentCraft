#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <torrentutils/core/hash.hpp>
#include <torrentutils/core/path.hpp>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/torrent_format.hpp>
#include <vector>

namespace torrentutils::core {

namespace detail {
struct MetadataEngineAccess;
} // namespace detail

/** BEP 47 file attributes represented by the Domain model. */
struct FileAttributes
{
    bool padding{};
    bool executable{};
    bool hidden{};
    bool symlink{};
};

/**
 * Validated file entry in a torrent's logical file layout.
 *
 * A BEP 47 symlink has zero length, no V2 pieces root, and a validated target relative to the
 * torrent root. The optional SHA-1 value is a non-authoritative file hint.
 */
class FileEntry
{
  public:
    [[nodiscard]] static Result<FileEntry>
    create(LogicalPath path, std::uint64_t length, FileAttributes attributes = {},
           std::optional<Sha256Digest> pieces_root = std::nullopt,
           std::optional<Sha1Digest> sha1_hint = std::nullopt,
           std::optional<LogicalPath> symlink_target = std::nullopt);

    [[nodiscard]] const LogicalPath& path() const noexcept;
    [[nodiscard]] std::uint64_t length() const noexcept;
    [[nodiscard]] const FileAttributes& attributes() const noexcept;
    [[nodiscard]] const std::optional<Sha256Digest>& pieces_root() const noexcept;
    [[nodiscard]] const std::optional<Sha1Digest>& sha1_hint() const noexcept;
    [[nodiscard]] const std::optional<LogicalPath>& symlink_target() const noexcept;

  private:
    FileEntry(LogicalPath path, std::uint64_t length, FileAttributes attributes,
              std::optional<Sha256Digest> pieces_root, std::optional<Sha1Digest> sha1_hint,
              std::optional<LogicalPath> symlink_target);

    LogicalPath path_;
    std::uint64_t length_{};
    FileAttributes attributes_;
    std::optional<Sha256Digest> pieces_root_;
    std::optional<Sha1Digest> sha1_hint_;
    std::optional<LogicalPath> symlink_target_;
};

/** Validated format-specific piece layout summary. */
class PieceInfo
{
  public:
    [[nodiscard]] static Result<PieceInfo> create(TorrentFormat format, std::uint64_t piece_length,
                                                  std::uint64_t total_size,
                                                  std::vector<Sha1Digest> v1_piece_hashes = {});

    [[nodiscard]] TorrentFormat format() const noexcept;
    [[nodiscard]] std::uint64_t piece_length() const noexcept;
    [[nodiscard]] std::uint64_t total_size() const noexcept;
    [[nodiscard]] const std::vector<Sha1Digest>& v1_piece_hashes() const noexcept;

  private:
    PieceInfo(TorrentFormat format, std::uint64_t piece_length, std::uint64_t total_size,
              std::vector<Sha1Digest> v1_piece_hashes);

    TorrentFormat format_;
    std::uint64_t piece_length_{};
    std::uint64_t total_size_{};
    std::vector<Sha1Digest> v1_piece_hashes_;
};

/** Read-only validated summary of the info dictionary. */
class TorrentInfo
{
  public:
    [[nodiscard]] static Result<TorrentInfo> create(std::string name, TorrentFormat format,
                                                    InfoHashes info_hashes, PieceInfo pieces,
                                                    std::vector<FileEntry> files,
                                                    bool is_private = false);

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] TorrentFormat format() const noexcept;
    [[nodiscard]] const InfoHashes& info_hashes() const noexcept;
    [[nodiscard]] const PieceInfo& pieces() const noexcept;
    [[nodiscard]] const std::vector<FileEntry>& files() const noexcept;
    [[nodiscard]] bool is_private() const noexcept;

  private:
    friend struct detail::MetadataEngineAccess;

    TorrentInfo(std::string name, TorrentFormat format, InfoHashes info_hashes, PieceInfo pieces,
                std::vector<FileEntry> files, bool is_private);

    std::string name_;
    TorrentFormat format_;
    InfoHashes info_hashes_;
    PieceInfo pieces_;
    std::vector<FileEntry> files_;
    bool is_private_{};
};

} // namespace torrentutils::core
