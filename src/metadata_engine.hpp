#pragma once

#include "bencode_adapter.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <torrentutils/core/torrent_document.hpp>
#include <vector>

namespace torrentutils::core::detail {

enum class MetadataReadMode
{
    Lenient,
    Strict
};

enum class MetadataEncodeDisposition
{
    Encoded,
    NeedRebuild
};

struct MetadataEncodeOutcome
{
    MetadataEncodeDisposition disposition{MetadataEncodeDisposition::NeedRebuild};
    std::vector<std::uint8_t> bytes;
};

enum class InfoIdentityField
{
    Private,
    Name,
    Source,
};

struct InfoIdentityPatch
{
    InfoIdentityField field{InfoIdentityField::Private};
    bool private_value{};
    std::string value;
    bool clear{};
};

struct RetainedDocumentState;

[[nodiscard]] Result<TorrentDocument>
make_retained_document(TorrentInfo info, TorrentMetadata metadata, TrackerList trackers,
                       std::vector<DocumentWarning> warnings, std::vector<MetadataFieldInfo> fields,
                       std::shared_ptr<const RetainedDocumentState> retained, bool has_extensions);
[[nodiscard]] const RetainedDocumentState* retained_state(const TorrentDocument& document) noexcept;
[[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>>
retained_torrent_bytes(const TorrentDocument& document) noexcept;

[[nodiscard]] Result<TorrentDocument>
decode_torrent(std::vector<std::uint8_t> bytes, MetadataReadMode mode = MetadataReadMode::Lenient);
[[nodiscard]] Result<TorrentDocument>
decode_torrent(std::vector<std::uint8_t> bytes, MetadataReadMode mode, const BencodeLimits& limits);
[[nodiscard]] Result<MetadataEncodeOutcome>
encode_top_level_patch(const TorrentDocument& candidate);
[[nodiscard]] Result<TorrentDocument> patch_info_identity(const TorrentDocument& document,
                                                          const InfoIdentityPatch& patch);
[[nodiscard]] bool source_is_in_info(const TorrentDocument& document) noexcept;
[[nodiscard]] std::vector<MetadataFieldValue>
metadata_field_values(const TorrentDocument& document);

} // namespace torrentutils::core::detail
