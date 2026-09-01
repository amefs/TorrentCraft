#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <torrentutils/core/result.hpp>
#include <vector>

namespace torrentutils::core {

/** Read-only classification of one metadata field against the field registry. */
enum class MetadataFieldCategory
{
    Standard,
    Extension,
    Unknown
};

/** Coarse bencode dictionary location of a classified metadata field. */
enum class MetadataFieldScope
{
    TopLevel,
    Info,
    InfoV1File,
    InfoV2FileTreeLeaf
};

/**
 * One metadata field present in a loaded torrent document.
 *
 * The key is the raw bencode key bytes. The classification is derived from the
 * registry plus the `x-*` extension convention; values are available through
 * TorrentDocument::metadata_field_values().
 */
struct MetadataFieldInfo
{
    std::string key;
    MetadataFieldCategory category{MetadataFieldCategory::Unknown};
    MetadataFieldScope scope{MetadataFieldScope::TopLevel};
    std::string source; // e.g. "BEP 3", "BEP 38", "convention"
    std::string type;   // human-readable bencode value type
    bool info_hash{};
    bool modeled{};
};

/** Validated HTTP(S) URL used as a torrent web seed. */
class WebSeedUrl
{
  public:
    [[nodiscard]] static Result<WebSeedUrl> parse(std::string value);

    [[nodiscard]] const std::string& value() const noexcept;

  private:
    explicit WebSeedUrl(std::string value);

    std::string value_;
};

/** Validated DHT bootstrap node. */
class DhtNode
{
  public:
    [[nodiscard]] static Result<DhtNode> create(std::string host, std::uint32_t port);

    [[nodiscard]] const std::string& host() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;

  private:
    DhtNode(std::string host, std::uint16_t port);

    std::string host_;
    std::uint16_t port_{};
};

/** Unvalidated input accepted by TorrentMetadata::create(). */
struct TorrentMetadataInput
{
    std::optional<std::string> comment;
    std::optional<std::string> creator;
    std::optional<std::string> source;
    std::optional<std::int64_t> creation_time_unix_seconds;
    std::vector<WebSeedUrl> web_seeds;
    std::vector<std::string> collections;
    std::vector<DhtNode> dht_nodes;
};

/** Validated top-level metadata independent of any bencode representation. */
class TorrentMetadata
{
  public:
    [[nodiscard]] static Result<TorrentMetadata> create(TorrentMetadataInput input = {});

    [[nodiscard]] const std::optional<std::string>& comment() const noexcept;
    [[nodiscard]] const std::optional<std::string>& creator() const noexcept;
    [[nodiscard]] const std::optional<std::string>& source() const noexcept;
    [[nodiscard]] const std::optional<std::int64_t>& creation_time_unix_seconds() const noexcept;
    [[nodiscard]] const std::vector<WebSeedUrl>& web_seeds() const noexcept;
    [[nodiscard]] const std::vector<std::string>& collections() const noexcept;
    [[nodiscard]] const std::vector<DhtNode>& dht_nodes() const noexcept;

  private:
    explicit TorrentMetadata(TorrentMetadataInput input);

    TorrentMetadataInput data_;
};

/** Read-only value of a direct top-level or info-dictionary metadata field. */
struct MetadataFieldValue
{
    std::string key;
    MetadataFieldScope scope{MetadataFieldScope::TopLevel};
    std::string type;
    std::string value;
};

} // namespace torrentutils::core
