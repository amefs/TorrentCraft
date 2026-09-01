#pragma once

#include <memory>
#include <string>
#include <torrentutils/core/metadata.hpp>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/torrent_info.hpp>
#include <torrentutils/core/tracker.hpp>
#include <vector>

namespace torrentutils::core {

/** Non-fatal diagnostic associated with a public document field. */
struct DocumentWarning
{
    std::string field;
    std::string message;
};

class TorrentDocument;
namespace detail {
struct RetainedDocumentState;
[[nodiscard]] Result<TorrentDocument>
make_retained_document(TorrentInfo info, TorrentMetadata metadata, TrackerList trackers,
                       std::vector<DocumentWarning> warnings, std::vector<MetadataFieldInfo> fields,
                       std::shared_ptr<const RetainedDocumentState> retained, bool has_extensions);
[[nodiscard]] const RetainedDocumentState* retained_state(const TorrentDocument& document) noexcept;
[[nodiscard]] std::vector<MetadataFieldValue>
metadata_field_values(const TorrentDocument& document);
} // namespace detail

/** Immutable torrent document snapshot with opaque private preservation state. */
class TorrentDocument
{
  public:
    TorrentDocument(const TorrentDocument&) noexcept;
    TorrentDocument(TorrentDocument&&) noexcept;
    TorrentDocument& operator=(const TorrentDocument&) noexcept;
    TorrentDocument& operator=(TorrentDocument&&) noexcept;
    ~TorrentDocument();

    [[nodiscard]] static Result<TorrentDocument> create(TorrentInfo info, TorrentMetadata metadata,
                                                        TrackerList trackers,
                                                        std::vector<DocumentWarning> warnings = {});

    [[nodiscard]] const TorrentInfo& info() const noexcept;
    [[nodiscard]] const TorrentMetadata& metadata() const noexcept;
    [[nodiscard]] const TrackerList& trackers() const noexcept;
    [[nodiscard]] const std::vector<DocumentWarning>& warnings() const noexcept;
    [[nodiscard]] bool has_retained_extensions() const noexcept;
    [[nodiscard]] const std::vector<MetadataFieldInfo>& metadata_fields() const noexcept;
    [[nodiscard]] std::vector<MetadataFieldValue> metadata_field_values() const;

    [[nodiscard]] Result<TorrentDocument> with_metadata(TorrentMetadata metadata) const;
    [[nodiscard]] Result<TorrentDocument> with_trackers(TrackerList trackers) const;

  private:
    friend Result<TorrentDocument>
    detail::make_retained_document(TorrentInfo, TorrentMetadata, TrackerList,
                                   std::vector<DocumentWarning>, std::vector<MetadataFieldInfo>,
                                   std::shared_ptr<const detail::RetainedDocumentState>, bool);
    friend const detail::RetainedDocumentState*
    detail::retained_state(const TorrentDocument&) noexcept;
    friend std::vector<MetadataFieldValue> detail::metadata_field_values(const TorrentDocument&);

    struct Details;

    explicit TorrentDocument(std::shared_ptr<const Details> details);

    std::shared_ptr<const Details> details_;
};

} // namespace torrentutils::core
