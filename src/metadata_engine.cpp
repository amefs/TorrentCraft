#include "metadata_engine.hpp"

#include "metadata_field_registry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace torrentutils::core::detail {

struct MetadataEngineAccess
{
    [[nodiscard]] static LogicalPath unavailable_path()
    {
        return LogicalPath({});
    }

    [[nodiscard]] static TorrentInfo retained_info(std::string name, const TorrentFormat format,
                                                   InfoHashes hashes, PieceInfo pieces,
                                                   std::vector<FileEntry> files,
                                                   const bool is_private)
    {
        return TorrentInfo(std::move(name), format, hashes, std::move(pieces), std::move(files),
                           is_private);
    }
};

struct RetainedTopLevelEntry
{
    std::string key;
    BencodeSpan value_span;
};

struct RetainedDocumentState
{
    RetainedDocumentState(std::shared_ptr<const std::vector<std::uint8_t>> source,
                          TorrentInfo info_value, TorrentMetadata metadata_value,
                          TrackerList tracker_value, std::vector<RetainedTopLevelEntry> entries,
                          const bool info_source, const MetadataReadMode read_mode)
        : bytes(std::move(source)), original_info(std::move(info_value)),
          original_metadata(std::move(metadata_value)), original_trackers(std::move(tracker_value)),
          top_level_entries(std::move(entries)), source_in_info(info_source), mode(read_mode)
    {
    }

    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    TorrentInfo original_info;
    TorrentMetadata original_metadata;
    TrackerList original_trackers;
    std::vector<RetainedTopLevelEntry> top_level_entries;
    bool source_in_info{};
    MetadataReadMode mode{MetadataReadMode::Lenient};
};

std::shared_ptr<const std::vector<std::uint8_t>>
retained_torrent_bytes(const TorrentDocument& document) noexcept
{
    const auto* state = retained_state(document);
    return state == nullptr ? nullptr : state->bytes;
}

namespace {
using Bytes = std::vector<std::uint8_t>;

[[nodiscard]] Error metadata_error(const ErrorCode code, std::string message)
{
    return {code, std::move(message), {}};
}

[[nodiscard]] Error invalid_torrent(std::string message)
{
    return metadata_error(ErrorCode::InvalidTorrent, std::move(message));
}

[[nodiscard]] Error unsupported(std::string message)
{
    return metadata_error(ErrorCode::UnsupportedFeature, std::move(message));
}

[[nodiscard]] std::string raw(const std::vector<std::uint8_t>& bytes, const BencodeSpan span)
{
    return std::string(reinterpret_cast<const char*>(bytes.data() + span.offset), span.size);
}

[[nodiscard]] const BencodeNode* lookup(const BencodeNode& dictionary,
                                        const std::vector<std::uint8_t>& bytes,
                                        const std::string_view key)
{
    if (dictionary.kind != BencodeNode::Kind::Dictionary)
    {
        return nullptr;
    }
    for (const auto& entry : std::get<BencodeDictionary>(dictionary.children))
    {
        if (raw(bytes, entry.first) == key)
        {
            return &entry.second;
        }
    }
    return nullptr;
}

[[nodiscard]] bool valid_utf8(const std::string& value) noexcept
{
    std::size_t index = 0;
    while (index < value.size())
    {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fU)
        {
            ++index;
            continue;
        }

        std::size_t continuation_count = 0;
        unsigned char second_minimum = 0x80U;
        unsigned char second_maximum = 0xbfU;
        if (first >= 0xc2U && first <= 0xdfU)
        {
            continuation_count = 1;
        }
        else if (first >= 0xe0U && first <= 0xefU)
        {
            continuation_count = 2;
            if (first == 0xe0U)
            {
                second_minimum = 0xa0U;
            }
            else if (first == 0xedU)
            {
                second_maximum = 0x9fU;
            }
        }
        else if (first >= 0xf0U && first <= 0xf4U)
        {
            continuation_count = 3;
            if (first == 0xf0U)
            {
                second_minimum = 0x90U;
            }
            else if (first == 0xf4U)
            {
                second_maximum = 0x8fU;
            }
        }
        else
        {
            return false;
        }

        if (index + continuation_count >= value.size())
        {
            return false;
        }
        const auto second = static_cast<unsigned char>(value[index + 1U]);
        if (second < second_minimum || second > second_maximum)
        {
            return false;
        }
        for (std::size_t offset = 2; offset <= continuation_count; ++offset)
        {
            const auto byte = static_cast<unsigned char>(value[index + offset]);
            if (byte < 0x80U || byte > 0xbfU)
            {
                return false;
            }
        }
        index += continuation_count + 1U;
    }
    return true;
}

[[nodiscard]] std::optional<std::string> string_value(const BencodeNode* node,
                                                      const std::vector<std::uint8_t>& bytes)
{
    if (node == nullptr || node->kind != BencodeNode::Kind::String)
    {
        return std::nullopt;
    }
    return raw(bytes, node->string_span);
}

[[nodiscard]] std::optional<std::uint64_t> unsigned_value(const BencodeNode* node)
{
    if (node == nullptr || node->kind != BencodeNode::Kind::Integer || node->integer < 0)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(node->integer);
}

[[nodiscard]] bool has_key(const BencodeNode& dictionary, const std::vector<std::uint8_t>& bytes,
                           const std::string_view key)
{
    return lookup(dictionary, bytes, key) != nullptr;
}

[[nodiscard]] Result<bool>
scan_dictionary_fields(const BencodeNode& dictionary, const std::vector<std::uint8_t>& bytes,
                       const FieldDictionary dictionary_kind, const MetadataReadMode mode,
                       const std::string& field, std::vector<DocumentWarning>& warnings,
                       std::vector<MetadataFieldInfo>& fields)
{
    bool retained = false;
    for (const auto& entry : std::get<BencodeDictionary>(dictionary.children))
    {
        const auto key = raw(bytes, entry.first);
        const auto classification = classify_field(key, dictionary_kind);
        fields.push_back({key, classification.category, field_scope(dictionary_kind),
                          std::string(classification.source), std::string(classification.type),
                          info_hash_bearing(dictionary_kind), classification.modeled});
        if (classification.category == MetadataFieldCategory::Standard)
        {
            continue;
        }
        if (classification.category == MetadataFieldCategory::Unknown &&
            mode == MetadataReadMode::Strict)
        {
            std::string message = "unsupported ";
            message.append(field).append(" field: ").append(key);
            return Result<bool>::failure(unsupported(std::move(message)));
        }
        retained = true;
        const std::string_view kind =
            classification.category == MetadataFieldCategory::Extension ? "extension" : "unknown";
        warnings.push_back({field, "retained " + std::string(kind) + " field: " + key});
    }
    return Result<bool>::success(retained);
}

// Small SHA-1 implementation used only at the raw info-byte boundary.
[[nodiscard]] Sha1Digest sha1(const std::vector<std::uint8_t>& input, const BencodeSpan span)
{
    std::vector<std::uint8_t> message(input.begin() + static_cast<std::ptrdiff_t>(span.offset),
                                      input.begin() +
                                          static_cast<std::ptrdiff_t>(span.offset + span.size));
    const auto bit_count = std::uint64_t(message.size()) * 8U;
    message.push_back(0x80U);
    while ((message.size() % 64U) != 56U)
    {
        message.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        message.push_back(static_cast<std::uint8_t>(bit_count >> shift));
    }

    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xefcdab89U;
    std::uint32_t h2 = 0x98badcfeU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xc3d2e1f0U;
    for (std::size_t offset = 0; offset < message.size(); offset += 64U)
    {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0; index < 16U; ++index)
        {
            const auto byte_offset = offset + index * 4U;
            words[index] = (std::uint32_t(message[byte_offset]) << 24U) |
                           (std::uint32_t(message[byte_offset + 1U]) << 16U) |
                           (std::uint32_t(message[byte_offset + 2U]) << 8U) |
                           std::uint32_t(message[byte_offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index)
        {
            const auto value =
                words[index - 3U] ^ words[index - 8U] ^ words[index - 14U] ^ words[index - 16U];
            words[index] = (value << 1U) | (value >> 31U);
        }

        auto a = h0;
        auto b = h1;
        auto c = h2;
        auto d = h3;
        auto e = h4;
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            std::uint32_t function = 0;
            std::uint32_t constant = 0;
            if (index < 20U)
            {
                function = (b & c) | ((~b) & d);
                constant = 0x5a827999U;
            }
            else if (index < 40U)
            {
                function = b ^ c ^ d;
                constant = 0x6ed9eba1U;
            }
            else if (index < 60U)
            {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8f1bbcdcU;
            }
            else
            {
                function = b ^ c ^ d;
                constant = 0xca62c1d6U;
            }
            const auto temporary =
                ((a << 5U) | (a >> 27U)) + function + e + constant + words[index];
            e = d;
            d = c;
            c = (b << 30U) | (b >> 2U);
            b = a;
            a = temporary;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    Sha1Digest::Bytes output{};
    const std::array<std::uint32_t, 5> hashes{h0, h1, h2, h3, h4};
    for (std::size_t index = 0; index < hashes.size(); ++index)
    {
        for (std::size_t byte = 0; byte < 4U; ++byte)
        {
            output[index * 4U + byte] =
                static_cast<std::uint8_t>(hashes[index] >> (24U - byte * 8U));
        }
    }
    return Sha1Digest::from_bytes(output);
}

[[nodiscard]] Result<std::optional<std::string>>
read_optional_text(const BencodeNode& dictionary, const std::vector<std::uint8_t>& bytes,
                   const std::string_view key, const MetadataReadMode mode,
                   std::vector<DocumentWarning>& warnings, bool& retained)
{
    const auto* node = lookup(dictionary, bytes, key);
    if (node == nullptr)
    {
        return Result<std::optional<std::string>>::success(std::nullopt);
    }
    auto value = string_value(node, bytes);
    if (!value)
    {
        return Result<std::optional<std::string>>::failure(
            invalid_torrent(std::string(key) + " must be a byte string"));
    }
    if (valid_utf8(*value))
    {
        return Result<std::optional<std::string>>::success(std::move(value));
    }
    if (mode == MetadataReadMode::Strict)
    {
        return Result<std::optional<std::string>>::failure(
            invalid_torrent(std::string(key) + " is not valid UTF-8"));
    }
    retained = true;
    warnings.push_back({std::string(key), "invalid UTF-8 retained"});
    return Result<std::optional<std::string>>::success(std::nullopt);
}

[[nodiscard]] Result<std::string>
read_info_name(const BencodeNode& info, const std::vector<std::uint8_t>& bytes,
               const MetadataReadMode mode, std::vector<DocumentWarning>& warnings, bool& retained)
{
    auto name = string_value(lookup(info, bytes, "name"), bytes);
    if (!name || name->empty())
    {
        return Result<std::string>::failure(
            invalid_torrent("info name must be a non-empty byte string"));
    }
    if (valid_utf8(*name))
    {
        return Result<std::string>::success(std::move(*name));
    }
    if (mode == MetadataReadMode::Strict)
    {
        return Result<std::string>::failure(invalid_torrent("info name is not valid UTF-8"));
    }
    retained = true;
    warnings.push_back({"info.name", "invalid UTF-8 retained; public name unavailable"});
    return Result<std::string>::success({});
}

[[nodiscard]] Result<TorrentInfo> make_mapped_info(std::string name, const TorrentFormat format,
                                                   InfoHashes hashes, PieceInfo pieces,
                                                   std::vector<FileEntry> files,
                                                   const bool is_private)
{
    if (!name.empty())
    {
        return TorrentInfo::create(std::move(name), format, hashes, std::move(pieces),
                                   std::move(files), is_private);
    }
    return Result<TorrentInfo>::success(MetadataEngineAccess::retained_info(
        {}, format, hashes, std::move(pieces), std::move(files), is_private));
}

[[nodiscard]] Result<TrackerList>
read_trackers(const BencodeNode& root, const std::vector<std::uint8_t>& bytes,
              const MetadataReadMode mode, std::vector<DocumentWarning>& warnings, bool& retained)
{
    std::vector<TrackerTier> tiers;
    const auto parse_url = [&](const BencodeNode& node) -> Result<std::optional<TrackerUrl>> {
        auto text = string_value(&node, bytes);
        if (!text)
        {
            return Result<std::optional<TrackerUrl>>::failure(
                invalid_torrent("tracker URL must be a byte string"));
        }
        auto tracker = TrackerUrl::parse(*text);
        if (tracker)
        {
            return Result<std::optional<TrackerUrl>>::success(std::move(tracker).value());
        }
        if (mode == MetadataReadMode::Strict)
        {
            return Result<std::optional<TrackerUrl>>::failure(
                invalid_torrent("tracker URL is invalid"));
        }
        retained = true;
        warnings.push_back({"tracker", "invalid tracker URL retained"});
        return Result<std::optional<TrackerUrl>>::success(std::nullopt);
    };

    const auto* announce_list = lookup(root, bytes, "announce-list");
    if (announce_list != nullptr)
    {
        if (announce_list->kind != BencodeNode::Kind::List)
        {
            return Result<TrackerList>::failure(invalid_torrent("announce-list must be a list"));
        }
        for (const auto& tier_node : std::get<BencodeList>(announce_list->children))
        {
            if (tier_node.kind != BencodeNode::Kind::List)
            {
                return Result<TrackerList>::failure(
                    invalid_torrent("announce-list tiers must be lists"));
            }
            std::vector<TrackerUrl> urls;
            for (const auto& url_node : std::get<BencodeList>(tier_node.children))
            {
                auto url = parse_url(url_node);
                if (!url)
                {
                    return Result<TrackerList>::failure(url.error());
                }
                auto parsed_url = std::move(url).value();
                if (parsed_url)
                {
                    urls.push_back(std::move(parsed_url).value());
                }
            }
            if (!urls.empty())
            {
                auto tier = TrackerTier::create(std::move(urls));
                if (!tier)
                {
                    return Result<TrackerList>::failure(invalid_torrent("invalid tracker tier"));
                }
                tiers.push_back(std::move(tier).value());
            }
        }
    }

    const auto* announce = lookup(root, bytes, "announce");
    if (announce != nullptr && tiers.empty())
    {
        auto url = parse_url(*announce);
        if (!url)
        {
            return Result<TrackerList>::failure(url.error());
        }
        auto parsed_url = std::move(url).value();
        if (parsed_url)
        {
            std::vector<TrackerUrl> urls;
            urls.push_back(std::move(parsed_url).value());
            auto tier = TrackerTier::create(std::move(urls));
            if (!tier)
            {
                return Result<TrackerList>::failure(invalid_torrent("invalid tracker tier"));
            }
            tiers.push_back(std::move(tier).value());
        }
    }

    auto result = TrackerList::create(std::move(tiers));
    if (!result)
    {
        return Result<TrackerList>::failure(invalid_torrent("invalid tracker list"));
    }
    return result;
}

[[nodiscard]] Result<void> append_web_seed(const BencodeNode& node,
                                           const std::vector<std::uint8_t>& bytes,
                                           const MetadataReadMode mode,
                                           std::vector<WebSeedUrl>& output,
                                           std::vector<DocumentWarning>& warnings, bool& retained)
{
    auto value = string_value(&node, bytes);
    if (!value)
    {
        return Result<void>::failure(invalid_torrent("web seed URL must be a byte string"));
    }
    auto parsed = WebSeedUrl::parse(*value);
    if (parsed)
    {
        output.push_back(std::move(parsed).value());
        return Result<void>::success();
    }
    if (mode == MetadataReadMode::Strict)
    {
        return Result<void>::failure(invalid_torrent("web seed URL is invalid"));
    }
    retained = true;
    warnings.push_back({"metadata.web_seed", "invalid web seed URL retained"});
    return Result<void>::success();
}

[[nodiscard]] Result<void>
read_web_seed_field(const BencodeNode& root, const std::vector<std::uint8_t>& bytes,
                    const std::string_view key, const MetadataReadMode mode,
                    std::vector<WebSeedUrl>& output, std::vector<DocumentWarning>& warnings,
                    bool& retained)
{
    const auto* node = lookup(root, bytes, key);
    if (node == nullptr)
    {
        return Result<void>::success();
    }
    if (node->kind == BencodeNode::Kind::String)
    {
        return append_web_seed(*node, bytes, mode, output, warnings, retained);
    }
    if (node->kind != BencodeNode::Kind::List)
    {
        return Result<void>::failure(
            invalid_torrent(std::string(key) + " must be a string or list"));
    }
    for (const auto& item : std::get<BencodeList>(node->children))
    {
        auto appended = append_web_seed(item, bytes, mode, output, warnings, retained);
        if (!appended)
        {
            return appended;
        }
    }
    return Result<void>::success();
}

[[nodiscard]] Result<void> read_collections(const BencodeNode& root,
                                            const std::vector<std::uint8_t>& bytes,
                                            const MetadataReadMode mode,
                                            std::vector<std::string>& output,
                                            std::vector<DocumentWarning>& warnings, bool& retained)
{
    const auto* node = lookup(root, bytes, "collections");
    if (node == nullptr)
    {
        return Result<void>::success();
    }
    if (node->kind != BencodeNode::Kind::List)
    {
        return Result<void>::failure(invalid_torrent("collections must be a list"));
    }
    for (const auto& item : std::get<BencodeList>(node->children))
    {
        auto value = string_value(&item, bytes);
        if (!value)
        {
            return Result<void>::failure(invalid_torrent("collection value must be a byte string"));
        }
        if (value->empty() || !valid_utf8(*value))
        {
            if (mode == MetadataReadMode::Strict)
            {
                return Result<void>::failure(
                    invalid_torrent("collection value must be non-empty UTF-8"));
            }
            retained = true;
            warnings.push_back({"metadata.collection", "invalid collection value retained"});
            continue;
        }
        output.push_back(std::move(*value));
    }
    return Result<void>::success();
}

[[nodiscard]] Result<void> read_dht_nodes(const BencodeNode& root,
                                          const std::vector<std::uint8_t>& bytes,
                                          const MetadataReadMode mode, std::vector<DhtNode>& output,
                                          std::vector<DocumentWarning>& warnings, bool& retained)
{
    const auto* node = lookup(root, bytes, "nodes");
    if (node == nullptr)
    {
        return Result<void>::success();
    }
    if (node->kind != BencodeNode::Kind::List)
    {
        return Result<void>::failure(invalid_torrent("nodes must be a list"));
    }
    for (const auto& item : std::get<BencodeList>(node->children))
    {
        if (item.kind != BencodeNode::Kind::List)
        {
            return Result<void>::failure(invalid_torrent("DHT node must be a list"));
        }
        const auto& pair = std::get<BencodeList>(item.children);
        if (pair.size() != 2U)
        {
            return Result<void>::failure(invalid_torrent("DHT node must contain host and port"));
        }
        auto host = string_value(&pair[0], bytes);
        auto port = unsigned_value(&pair[1]);
        if (!host || !port || *port > std::numeric_limits<std::uint32_t>::max())
        {
            return Result<void>::failure(invalid_torrent("invalid DHT node shape"));
        }
        auto parsed = DhtNode::create(*host, static_cast<std::uint32_t>(*port));
        if (parsed)
        {
            output.push_back(std::move(parsed).value());
            continue;
        }
        if (mode == MetadataReadMode::Strict)
        {
            return Result<void>::failure(invalid_torrent("invalid DHT node"));
        }
        retained = true;
        warnings.push_back({"metadata.dht_node", "invalid DHT node retained"});
    }
    return Result<void>::success();
}

[[nodiscard]] Result<TorrentMetadata>
read_metadata(const BencodeNode& root, const BencodeNode& info,
              const std::vector<std::uint8_t>& bytes, const MetadataReadMode mode,
              std::vector<DocumentWarning>& warnings, bool& retained, bool& source_in_info)
{
    TorrentMetadataInput input;
    auto comment = read_optional_text(root, bytes, "comment", mode, warnings, retained);
    if (!comment)
    {
        return Result<TorrentMetadata>::failure(comment.error());
    }
    input.comment = std::move(comment).value();

    auto creator = read_optional_text(root, bytes, "created by", mode, warnings, retained);
    if (!creator)
    {
        return Result<TorrentMetadata>::failure(creator.error());
    }
    input.creator = std::move(creator).value();

    if (has_key(info, bytes, "source"))
    {
        auto source = read_optional_text(info, bytes, "source", mode, warnings, retained);
        if (!source)
        {
            return Result<TorrentMetadata>::failure(source.error());
        }
        input.source = std::move(source).value();
        source_in_info = true;
    }
    else if (lookup(root, bytes, "source") != nullptr)
    {
        auto source = read_optional_text(root, bytes, "source", mode, warnings, retained);
        if (!source)
        {
            return Result<TorrentMetadata>::failure(source.error());
        }
        input.source = std::move(source).value();
    }

    const auto* creation_date = lookup(root, bytes, "creation date");
    if (creation_date != nullptr)
    {
        if (creation_date->kind != BencodeNode::Kind::Integer)
        {
            return Result<TorrentMetadata>::failure(
                invalid_torrent("creation date must be an integer"));
        }
        input.creation_time_unix_seconds = creation_date->integer;
    }

    auto web_seeds =
        read_web_seed_field(root, bytes, "url-list", mode, input.web_seeds, warnings, retained);
    if (!web_seeds)
    {
        return Result<TorrentMetadata>::failure(web_seeds.error());
    }
    web_seeds =
        read_web_seed_field(root, bytes, "httpseeds", mode, input.web_seeds, warnings, retained);
    if (!web_seeds)
    {
        return Result<TorrentMetadata>::failure(web_seeds.error());
    }
    auto collections = read_collections(root, bytes, mode, input.collections, warnings, retained);
    if (!collections)
    {
        return Result<TorrentMetadata>::failure(collections.error());
    }
    auto nodes = read_dht_nodes(root, bytes, mode, input.dht_nodes, warnings, retained);
    if (!nodes)
    {
        return Result<TorrentMetadata>::failure(nodes.error());
    }

    auto metadata = TorrentMetadata::create(std::move(input));
    if (!metadata)
    {
        return Result<TorrentMetadata>::failure(invalid_torrent("invalid torrent metadata"));
    }
    return metadata;
}

struct ParsedFileAttributes
{
    FileAttributes attributes;
    std::optional<Sha1Digest> sha1_hint;
    std::optional<LogicalPath> symlink_target;
};

[[nodiscard]] Result<ParsedFileAttributes>
read_attributes(const BencodeNode& dictionary, const std::vector<std::uint8_t>& bytes,
                const std::optional<std::uint64_t> length, const std::string& field,
                std::vector<DocumentWarning>& warnings, bool& retained)
{
    FileAttributes attributes;
    const auto* node = lookup(dictionary, bytes, "attr");
    const auto* sha1_hint = lookup(dictionary, bytes, "sha1");
    const auto* symlink_path = lookup(dictionary, bytes, "symlink path");
    if (node == nullptr && sha1_hint == nullptr && symlink_path == nullptr)
    {
        return Result<ParsedFileAttributes>::success({attributes, std::nullopt, std::nullopt});
    }
    if (node == nullptr && symlink_path != nullptr)
    {
        return Result<ParsedFileAttributes>::failure(
            invalid_torrent("symlink path requires the symlink file attribute"));
    }

    bool has_unsupported_attribute = false;
    if (node != nullptr)
    {
        auto value = string_value(node, bytes);
        if (!value)
        {
            return Result<ParsedFileAttributes>::failure(
                invalid_torrent("file attr must be a byte string"));
        }
        for (const char attribute : *value)
        {
            switch (attribute)
            {
            case 'p':
                attributes.padding = true;
                break;
            case 'x':
                attributes.executable = true;
                break;
            case 'h':
                attributes.hidden = true;
                break;
            case 'l':
                attributes.symlink = true;
                break;
            default:
                has_unsupported_attribute = true;
                break;
            }
        }
    }
    std::optional<Sha1Digest> parsed_sha1_hint;
    if (sha1_hint != nullptr)
    {
        auto value = string_value(sha1_hint, bytes);
        if (!value || value->size() != Sha1Digest::Bytes{}.size())
        {
            return Result<ParsedFileAttributes>::failure(
                invalid_torrent("file sha1 hint must be a 20-byte string"));
        }
        Sha1Digest::Bytes digest_bytes{};
        std::memcpy(digest_bytes.data(), value->data(), digest_bytes.size());
        parsed_sha1_hint = Sha1Digest::from_bytes(digest_bytes);
    }

    if (attributes.symlink != (symlink_path != nullptr))
    {
        return Result<ParsedFileAttributes>::failure(
            invalid_torrent("symlink attribute and symlink path must appear together"));
    }
    std::optional<LogicalPath> symlink_target;
    if (symlink_path != nullptr)
    {
        if (length.has_value() && length.value() != 0U)
        {
            return Result<ParsedFileAttributes>::failure(
                invalid_torrent("symlink file length must be zero"));
        }
        if (symlink_path->kind != BencodeNode::Kind::List)
        {
            return Result<ParsedFileAttributes>::failure(
                invalid_torrent("symlink path must be a list of byte strings"));
        }
        std::vector<std::string> segments;
        for (const auto& segment_node : std::get<BencodeList>(symlink_path->children))
        {
            auto segment = string_value(&segment_node, bytes);
            if (!segment || !valid_utf8(*segment))
            {
                return Result<ParsedFileAttributes>::failure(
                    invalid_torrent("symlink path segment must be valid UTF-8"));
            }
            segments.push_back(std::move(*segment));
        }
        auto parsed_symlink_target = LogicalPath::from_segments(std::move(segments));
        if (!parsed_symlink_target)
        {
            return Result<ParsedFileAttributes>::failure(
                invalid_torrent("unsafe or invalid symlink path"));
        }
        symlink_target = std::move(parsed_symlink_target).value();
    }

    if (has_unsupported_attribute)
    {
        retained = true;
        warnings.push_back({field + ".attr", "retained unknown file attribute"});
    }
    return Result<ParsedFileAttributes>::success(
        {attributes, parsed_sha1_hint, std::move(symlink_target)});
}

struct ParsedFileSpec
{
    std::uint64_t length{};
    ParsedFileAttributes attributes;
};

[[nodiscard]] Result<ParsedFileSpec>
read_file_spec(const BencodeNode& dictionary, const std::vector<std::uint8_t>& bytes,
               const std::string& field, std::vector<DocumentWarning>& warnings, bool& retained)
{
    const auto* length_node = lookup(dictionary, bytes, "length");
    const auto length = unsigned_value(length_node);
    if (length_node != nullptr && !length)
    {
        return Result<ParsedFileSpec>::failure(
            invalid_torrent("file length must be a non-negative integer"));
    }
    auto attributes = read_attributes(dictionary, bytes, length, field, warnings, retained);
    if (!attributes)
    {
        return Result<ParsedFileSpec>::failure(attributes.error());
    }
    if (!length && !attributes.value().attributes.symlink)
    {
        return Result<ParsedFileSpec>::failure(invalid_torrent("file length is required"));
    }
    return Result<ParsedFileSpec>::success({length.value_or(0U), std::move(attributes).value()});
}

[[nodiscard]] Sha256Digest::Bytes sha256_bytes(const std::uint8_t* data, const std::size_t size)
{
    std::vector<std::uint8_t> message(data, data + size);
    const auto bit_count = std::uint64_t(message.size()) * 8U;
    message.push_back(0x80U);
    while ((message.size() % 64U) != 56U)
    {
        message.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        message.push_back(static_cast<std::uint8_t>(bit_count >> shift));
    }

    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
        0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
        0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
        0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
        0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
        0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
        0xc67178f2U};
    std::array<std::uint32_t, 8> hashes{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    const auto rotate_right = [](const std::uint32_t value, const unsigned shift) {
        return (value >> shift) | (value << (32U - shift));
    };

    for (std::size_t offset = 0; offset < message.size(); offset += 64U)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index)
        {
            const auto byte_offset = offset + index * 4U;
            words[index] = (std::uint32_t(message[byte_offset]) << 24U) |
                           (std::uint32_t(message[byte_offset + 1U]) << 16U) |
                           (std::uint32_t(message[byte_offset + 2U]) << 8U) |
                           std::uint32_t(message[byte_offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index)
        {
            const auto s0 = rotate_right(words[index - 15U], 7U) ^
                            rotate_right(words[index - 15U], 18U) ^ (words[index - 15U] >> 3U);
            const auto s1 = rotate_right(words[index - 2U], 17U) ^
                            rotate_right(words[index - 2U], 19U) ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
        }

        auto a = hashes[0];
        auto b = hashes[1];
        auto c = hashes[2];
        auto d = hashes[3];
        auto e = hashes[4];
        auto f = hashes[5];
        auto g = hashes[6];
        auto h = hashes[7];
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const auto sigma1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const auto choice = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sigma1 + choice + constants[index] + words[index];
            const auto sigma0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        hashes[0] += a;
        hashes[1] += b;
        hashes[2] += c;
        hashes[3] += d;
        hashes[4] += e;
        hashes[5] += f;
        hashes[6] += g;
        hashes[7] += h;
    }

    Sha256Digest::Bytes output{};
    for (std::size_t index = 0; index < hashes.size(); ++index)
    {
        for (std::size_t byte = 0; byte < 4U; ++byte)
        {
            output[index * 4U + byte] =
                static_cast<std::uint8_t>(hashes[index] >> (24U - byte * 8U));
        }
    }
    return output;
}

[[nodiscard]] Sha256Digest sha256(const std::vector<std::uint8_t>& input, const BencodeSpan span)
{
    return Sha256Digest::from_bytes(sha256_bytes(input.data() + span.offset, span.size));
}

[[nodiscard]] Sha256Digest::Bytes parent_hash(const Sha256Digest::Bytes& left,
                                              const Sha256Digest::Bytes& right)
{
    std::array<std::uint8_t, 64> input{};
    std::copy(left.begin(), left.end(), input.begin());
    std::copy(right.begin(), right.end(), input.begin() + 32);
    return sha256_bytes(input.data(), input.size());
}

[[nodiscard]] Sha256Digest::Bytes zero_hash_for_piece_length(std::uint64_t piece_length)
{
    Sha256Digest::Bytes zero{};
    for (std::uint64_t block_size = std::uint64_t{16U} * 1024U; block_size < piece_length;
         block_size *= 2U)
    {
        zero = parent_hash(zero, zero);
    }
    return zero;
}

struct V2MappedFile
{
    FileEntry file;
    Sha256Digest::Bytes root{};
};

[[nodiscard]] Result<void> walk_v2_file_tree(
    const BencodeNode& tree, const std::vector<std::uint8_t>& bytes, std::vector<std::string>& path,
    std::vector<V2MappedFile>& output, std::uint64_t& total_size, const MetadataReadMode mode,
    std::vector<DocumentWarning>& warnings, std::vector<MetadataFieldInfo>& fields, bool& retained)
{
    if (tree.kind != BencodeNode::Kind::Dictionary)
    {
        return Result<void>::failure(invalid_torrent("v2 file tree node must be a dictionary"));
    }
    const auto& entries = std::get<BencodeDictionary>(tree.children);
    const auto leaf = std::find_if(entries.begin(), entries.end(), [&bytes](const auto& entry) {
        return raw(bytes, entry.first).empty();
    });
    if (leaf != entries.end())
    {
        if (entries.size() != 1U || path.empty() ||
            leaf->second.kind != BencodeNode::Kind::Dictionary)
        {
            return Result<void>::failure(invalid_torrent("invalid v2 file leaf"));
        }
        auto extensions =
            scan_dictionary_fields(leaf->second, bytes, FieldDictionary::InfoV2FileTreeLeaf, mode,
                                   "info.file tree", warnings, fields);
        if (!extensions)
        {
            return Result<void>::failure(extensions.error());
        }
        retained = retained || extensions.value();
        auto spec = read_file_spec(leaf->second, bytes, "info.file tree", warnings, retained);
        if (!spec)
        {
            return Result<void>::failure(spec.error());
        }
        const auto length = spec.value().length;
        const auto& attributes = spec.value().attributes;
        if (attributes.attributes.padding)
        {
            return Result<void>::failure(invalid_torrent("v2 file tree cannot contain padding"));
        }
        const auto root_value = string_value(lookup(leaf->second, bytes, "pieces root"), bytes);
        if ((length == 0U && root_value) ||
            (length > 0U && (!root_value || root_value->size() != 32U)))
        {
            return Result<void>::failure(invalid_torrent("invalid v2 pieces root"));
        }
        if (length > std::numeric_limits<std::uint64_t>::max() - total_size)
        {
            return Result<void>::failure(invalid_torrent("total file length overflows uint64"));
        }
        auto logical_path = LogicalPath::from_segments(path);
        if (!logical_path)
        {
            return Result<void>::failure(invalid_torrent("unsafe or invalid v2 file path"));
        }
        std::optional<Sha256Digest> digest;
        Sha256Digest::Bytes digest_bytes{};
        if (root_value)
        {
            std::memcpy(digest_bytes.data(), root_value->data(), digest_bytes.size());
            digest = Sha256Digest::from_bytes(digest_bytes);
        }
        auto file =
            FileEntry::create(std::move(logical_path).value(), length, attributes.attributes,
                              digest, attributes.sha1_hint, attributes.symlink_target);
        if (!file)
        {
            return Result<void>::failure(invalid_torrent("invalid v2 file"));
        }
        total_size += length;
        output.push_back({std::move(file).value(), digest_bytes});
        return Result<void>::success();
    }

    if (entries.empty())
    {
        return Result<void>::failure(invalid_torrent("empty v2 file tree directory"));
    }
    for (const auto& entry : entries)
    {
        auto segment = raw(bytes, entry.first);
        if (segment.empty() || !valid_utf8(segment))
        {
            return Result<void>::failure(
                invalid_torrent("v2 path segment must be non-empty UTF-8"));
        }
        path.push_back(std::move(segment));
        auto walked = walk_v2_file_tree(entry.second, bytes, path, output, total_size, mode,
                                        warnings, fields, retained);
        path.pop_back();
        if (!walked)
        {
            return walked;
        }
    }
    return Result<void>::success();
}

[[nodiscard]] Result<void> validate_piece_layers(const BencodeNode& root,
                                                 const std::vector<std::uint8_t>& bytes,
                                                 const std::uint64_t piece_length,
                                                 const std::vector<V2MappedFile>& files,
                                                 bool& retained)
{
    struct ExpectedLayer
    {
        Sha256Digest::Bytes root{};
        std::size_t piece_count{};
        bool found{};
    };
    std::vector<ExpectedLayer> expected;
    for (const auto& mapped : files)
    {
        if (mapped.file.length() <= piece_length)
        {
            continue;
        }
        const auto count = mapped.file.length() / piece_length +
                           (mapped.file.length() % piece_length == 0U ? 0U : 1U);
        if (count > std::numeric_limits<std::size_t>::max())
        {
            return Result<void>::failure(invalid_torrent("v2 piece layer is too large"));
        }
        const auto duplicate =
            std::find_if(expected.begin(), expected.end(),
                         [&](const auto& item) { return item.root == mapped.root; });
        if (duplicate != expected.end())
        {
            if (duplicate->piece_count != static_cast<std::size_t>(count))
            {
                return Result<void>::failure(
                    invalid_torrent("shared v2 pieces root has inconsistent size"));
            }
            continue;
        }
        expected.push_back({mapped.root, static_cast<std::size_t>(count), false});
    }

    const auto* layers = lookup(root, bytes, "piece layers");
    if (layers == nullptr)
    {
        if (!expected.empty())
        {
            return Result<void>::failure(invalid_torrent("required v2 piece layer is missing"));
        }
        return Result<void>::success();
    }
    if (layers->kind != BencodeNode::Kind::Dictionary)
    {
        return Result<void>::failure(invalid_torrent("piece layers must be a dictionary"));
    }
    retained = true;
    for (const auto& entry : std::get<BencodeDictionary>(layers->children))
    {
        const auto root_bytes = raw(bytes, entry.first);
        auto value = string_value(&entry.second, bytes);
        if (root_bytes.size() != 32U || !value || value->size() % 32U != 0U)
        {
            return Result<void>::failure(invalid_torrent("invalid v2 piece layer entry"));
        }
        Sha256Digest::Bytes root_digest{};
        std::memcpy(root_digest.data(), root_bytes.data(), root_digest.size());
        auto expected_entry = std::find_if(expected.begin(), expected.end(), [&](const auto& item) {
            return item.root == root_digest;
        });
        if (expected_entry == expected.end() || value->size() != expected_entry->piece_count * 32U)
        {
            return Result<void>::failure(invalid_torrent("unexpected v2 piece layer"));
        }

        std::vector<Sha256Digest::Bytes> level(expected_entry->piece_count);
        for (std::size_t index = 0; index < level.size(); ++index)
        {
            std::memcpy(level[index].data(), value->data() + index * 32U, 32U);
        }
        std::size_t padded_count = 1U;
        while (padded_count < level.size())
        {
            padded_count *= 2U;
        }
        level.resize(padded_count, zero_hash_for_piece_length(piece_length));
        while (level.size() > 1U)
        {
            std::vector<Sha256Digest::Bytes> parents;
            parents.reserve(level.size() / 2U);
            for (std::size_t index = 0; index < level.size(); index += 2U)
            {
                parents.push_back(parent_hash(level[index], level[index + 1U]));
            }
            level = std::move(parents);
        }
        if (level.front() != root_digest)
        {
            return Result<void>::failure(
                invalid_torrent("v2 piece layer does not match pieces root"));
        }
        expected_entry->found = true;
    }
    if (std::any_of(expected.begin(), expected.end(), [](const auto& item) { return !item.found; }))
    {
        return Result<void>::failure(invalid_torrent("required v2 piece layer is missing"));
    }
    return Result<void>::success();
}

[[nodiscard]] Result<TorrentDocument> map_v2(const BencodeDocument& document,
                                             const MetadataReadMode mode)
{
    const auto& root = document.root;
    const auto& bytes = *document.bytes;
    const auto* info = lookup(root, bytes, "info");
    if (info == nullptr || info->kind != BencodeNode::Kind::Dictionary)
    {
        return Result<TorrentDocument>::failure(
            invalid_torrent("top-level info dictionary is required"));
    }
    const auto* meta_version = lookup(*info, bytes, "meta version");
    if (meta_version == nullptr || meta_version->kind != BencodeNode::Kind::Integer)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid meta version"));
    }
    if (meta_version->integer != 2)
    {
        return Result<TorrentDocument>::failure(unsupported("unsupported meta version"));
    }
    const bool hybrid = has_key(*info, bytes, "pieces");
    const auto format = hybrid ? TorrentFormat::Hybrid : TorrentFormat::V2;

    std::vector<DocumentWarning> warnings;
    bool retained = false;
    std::vector<MetadataFieldInfo> fields;
    auto top_extensions = scan_dictionary_fields(root, bytes, FieldDictionary::TopLevelV2, mode,
                                                 "top-level", warnings, fields);
    if (!top_extensions)
    {
        return Result<TorrentDocument>::failure(top_extensions.error());
    }
    retained = top_extensions.value();
    auto info_extensions = hybrid
                               ? scan_dictionary_fields(*info, bytes, FieldDictionary::InfoHybrid,
                                                        mode, "info", warnings, fields)
                               : scan_dictionary_fields(*info, bytes, FieldDictionary::InfoV2, mode,
                                                        "info", warnings, fields);
    if (!info_extensions)
    {
        return Result<TorrentDocument>::failure(info_extensions.error());
    }
    retained = retained || info_extensions.value();

    auto name = read_info_name(*info, bytes, mode, warnings, retained);
    if (!name)
    {
        return Result<TorrentDocument>::failure(name.error());
    }
    const auto piece_length = unsigned_value(lookup(*info, bytes, "piece length"));
    const auto* file_tree = lookup(*info, bytes, "file tree");
    if (!piece_length || *piece_length == 0U || file_tree == nullptr)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid v2 info fields"));
    }

    std::vector<V2MappedFile> mapped_files;
    std::vector<std::string> path;
    std::uint64_t v2_total_size = 0;
    auto walked = walk_v2_file_tree(*file_tree, bytes, path, mapped_files, v2_total_size, mode,
                                    warnings, fields, retained);
    if (!walked)
    {
        return Result<TorrentDocument>::failure(walked.error());
    }
    if (mapped_files.empty())
    {
        return Result<TorrentDocument>::failure(invalid_torrent("v2 file tree is empty"));
    }
    auto layers = validate_piece_layers(root, bytes, *piece_length, mapped_files, retained);
    if (!layers)
    {
        return Result<TorrentDocument>::failure(layers.error());
    }

    std::vector<FileEntry> files;
    std::uint64_t total_size = v2_total_size;
    std::vector<Sha1Digest> piece_hashes;
    if (!hybrid)
    {
        files.reserve(mapped_files.size());
        for (auto& mapped : mapped_files)
        {
            files.push_back(std::move(mapped.file));
        }
    }
    else
    {
        struct HybridFileSpec
        {
            LogicalPath path;
            std::uint64_t length{};
            FileAttributes attributes;
            std::optional<Sha1Digest> sha1_hint;
            std::optional<LogicalPath> symlink_target;
        };
        std::vector<HybridFileSpec> v1_files;
        const auto append_spec = [&](std::vector<std::string> segments, const std::uint64_t length,
                                     const ParsedFileAttributes& attributes) -> Result<void> {
            auto logical_path = LogicalPath::from_segments(std::move(segments));
            if (!logical_path)
            {
                return Result<void>::failure(invalid_torrent("unsafe or invalid hybrid file path"));
            }
            v1_files.push_back({std::move(logical_path).value(), length, attributes.attributes,
                                attributes.sha1_hint, attributes.symlink_target});
            return Result<void>::success();
        };

        const auto* file_list = lookup(*info, bytes, "files");
        if (file_list != nullptr)
        {
            if (file_list->kind != BencodeNode::Kind::List || has_key(*info, bytes, "length"))
            {
                return Result<TorrentDocument>::failure(
                    invalid_torrent("hybrid files must be a list without single-file length"));
            }
            const auto& entries = std::get<BencodeList>(file_list->children);
            if (entries.empty())
            {
                return Result<TorrentDocument>::failure(
                    invalid_torrent("hybrid files list is empty"));
            }
            for (const auto& entry : entries)
            {
                if (entry.kind != BencodeNode::Kind::Dictionary)
                {
                    return Result<TorrentDocument>::failure(
                        invalid_torrent("hybrid file entry must be a dictionary"));
                }
                auto extensions = scan_dictionary_fields(entry, bytes, FieldDictionary::InfoV1File,
                                                         mode, "info.files", warnings, fields);
                if (!extensions)
                {
                    return Result<TorrentDocument>::failure(extensions.error());
                }
                retained = retained || extensions.value();
                auto spec = read_file_spec(entry, bytes, "info.files", warnings, retained);
                if (!spec)
                {
                    return Result<TorrentDocument>::failure(spec.error());
                }
                const auto* path_node = lookup(entry, bytes, "path");
                std::vector<std::string> segments;
                if (path_node == nullptr && spec.value().attributes.attributes.padding)
                {
                    segments = {".pad", std::to_string(spec.value().length)};
                }
                else if (path_node == nullptr || path_node->kind != BencodeNode::Kind::List)
                {
                    return Result<TorrentDocument>::failure(
                        invalid_torrent("invalid hybrid v1 file"));
                }
                else
                {
                    for (const auto& segment_node : std::get<BencodeList>(path_node->children))
                    {
                        auto segment = string_value(&segment_node, bytes);
                        if (!segment || !valid_utf8(*segment))
                        {
                            return Result<TorrentDocument>::failure(
                                invalid_torrent("hybrid path segment must be valid UTF-8"));
                        }
                        segments.push_back(std::move(*segment));
                    }
                }
                auto appended =
                    append_spec(std::move(segments), spec.value().length, spec.value().attributes);
                if (!appended)
                {
                    return Result<TorrentDocument>::failure(appended.error());
                }
            }
        }
        else
        {
            auto spec = read_file_spec(*info, bytes, "info", warnings, retained);
            if (!spec)
            {
                return Result<TorrentDocument>::failure(spec.error());
            }
            auto appended =
                name.value().empty()
                    ? Result<void>::failure(invalid_torrent(
                          "invalid UTF-8 hybrid single-file name is not representable"))
                    : append_spec({name.value()}, spec.value().length, spec.value().attributes);
            if (!appended)
            {
                return Result<TorrentDocument>::failure(appended.error());
            }
        }

        std::vector<bool> matched(mapped_files.size(), false);
        files.reserve(v1_files.size());
        total_size = 0;
        bool saw_content = false;
        for (auto& spec : v1_files)
        {
            if (spec.length > std::numeric_limits<std::uint64_t>::max() - total_size)
            {
                return Result<TorrentDocument>::failure(
                    invalid_torrent("hybrid total file length overflows uint64"));
            }
            if (spec.attributes.padding)
            {
                const auto remainder = total_size % *piece_length;
                const auto expected = remainder == 0U ? 0U : *piece_length - remainder;
                const auto segments = spec.path.segments();
                if (!saw_content || expected == 0U || spec.length != expected ||
                    spec.attributes.executable || spec.attributes.symlink ||
                    segments.size() != 2U || segments[0] != ".pad" ||
                    segments[1] != std::to_string(spec.length))
                {
                    return Result<TorrentDocument>::failure(
                        invalid_torrent("invalid hybrid padding file"));
                }
                auto file = FileEntry::create(std::move(spec.path), spec.length, spec.attributes,
                                              std::nullopt, spec.sha1_hint, spec.symlink_target);
                if (!file)
                {
                    return Result<TorrentDocument>::failure(
                        invalid_torrent("invalid hybrid padding file"));
                }
                files.push_back(std::move(file).value());
            }
            else
            {
                if (spec.length > 0U && saw_content && total_size % *piece_length != 0U)
                {
                    return Result<TorrentDocument>::failure(
                        invalid_torrent("hybrid content files must start at piece boundaries"));
                }
                const auto match =
                    std::find_if(mapped_files.begin(), mapped_files.end(), [&](const auto& mapped) {
                        const auto index = static_cast<std::size_t>(&mapped - mapped_files.data());
                        return !matched[index] && mapped.file.path() == spec.path &&
                               mapped.file.length() == spec.length;
                    });
                if (match == mapped_files.end() ||
                    match->file.attributes().executable != spec.attributes.executable ||
                    match->file.attributes().symlink != spec.attributes.symlink ||
                    match->file.attributes().hidden != spec.attributes.hidden ||
                    match->file.sha1_hint() != spec.sha1_hint ||
                    match->file.symlink_target() != spec.symlink_target)
                {
                    return Result<TorrentDocument>::failure(
                        invalid_torrent("hybrid v1 and v2 file layouts do not match"));
                }
                const auto index = static_cast<std::size_t>(match - mapped_files.begin());
                matched[index] = true;
                auto file = FileEntry::create(std::move(spec.path), spec.length, spec.attributes,
                                              match->file.pieces_root(), spec.sha1_hint,
                                              spec.symlink_target);
                if (!file)
                {
                    return Result<TorrentDocument>::failure(
                        invalid_torrent("invalid hybrid content file"));
                }
                files.push_back(std::move(file).value());
                saw_content = saw_content || spec.length > 0U;
            }
            total_size += spec.length;
        }
        if (!saw_content ||
            std::any_of(matched.begin(), matched.end(), [](const bool value) { return !value; }))
        {
            return Result<TorrentDocument>::failure(
                invalid_torrent("hybrid v1 and v2 file layouts do not match"));
        }

        const auto pieces = string_value(lookup(*info, bytes, "pieces"), bytes);
        if (!pieces || pieces->size() % 20U != 0U)
        {
            return Result<TorrentDocument>::failure(invalid_torrent("invalid hybrid pieces"));
        }
        const auto& piece_bytes = pieces.value();
        piece_hashes.reserve(piece_bytes.size() / 20U);
        for (std::size_t offset = 0; offset < piece_bytes.size(); offset += 20U)
        {
            Sha1Digest::Bytes digest{};
            std::memcpy(digest.data(), piece_bytes.data() + offset, digest.size());
            piece_hashes.push_back(Sha1Digest::from_bytes(digest));
        }
    }

    auto piece_info = PieceInfo::create(format, *piece_length, total_size, std::move(piece_hashes));
    if (!piece_info)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid v2 piece layout"));
    }
    auto hashes = InfoHashes::create(
        format, hybrid ? std::optional<Sha1Digest>(sha1(bytes, info->encoded_span)) : std::nullopt,
        sha256(bytes, info->encoded_span));
    if (!hashes)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid v2 info hash"));
    }

    bool private_flag = false;
    const auto* private_node = lookup(*info, bytes, "private");
    if (private_node != nullptr)
    {
        const auto value = unsigned_value(private_node);
        if (!value || *value > 1U)
        {
            return Result<TorrentDocument>::failure(
                invalid_torrent("private must be integer zero or one"));
        }
        private_flag = *value == 1U;
    }
    auto torrent_info =
        make_mapped_info(std::move(name).value(), format, std::move(hashes).value(),
                         std::move(piece_info).value(), std::move(files), private_flag);
    if (!torrent_info)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid v2 torrent info"));
    }

    bool source_in_info = false;
    auto metadata = read_metadata(root, *info, bytes, mode, warnings, retained, source_in_info);
    if (!metadata)
    {
        return Result<TorrentDocument>::failure(metadata.error());
    }
    auto trackers = read_trackers(root, bytes, mode, warnings, retained);
    if (!trackers)
    {
        return Result<TorrentDocument>::failure(trackers.error());
    }
    std::vector<RetainedTopLevelEntry> entries;
    for (const auto& entry : std::get<BencodeDictionary>(root.children))
    {
        entries.push_back({raw(bytes, entry.first), entry.second.encoded_span});
    }

    auto info_value = std::move(torrent_info).value();
    auto metadata_value = std::move(metadata).value();
    auto tracker_value = std::move(trackers).value();
    auto state = std::make_shared<RetainedDocumentState>(document.bytes, info_value, metadata_value,
                                                         tracker_value, std::move(entries),
                                                         source_in_info, mode);
    return make_retained_document(std::move(info_value), std::move(metadata_value),
                                  std::move(tracker_value), std::move(warnings), std::move(fields),
                                  std::move(state), retained);
}

[[nodiscard]] Result<TorrentDocument> map_v1(const BencodeDocument& document,
                                             const MetadataReadMode mode)
{
    const auto& root = document.root;
    const auto& bytes = *document.bytes;
    const auto* info = lookup(root, bytes, "info");
    if (info == nullptr || info->kind != BencodeNode::Kind::Dictionary)
    {
        return Result<TorrentDocument>::failure(
            invalid_torrent("top-level info dictionary is required"));
    }
    if (has_key(*info, bytes, "meta version"))
    {
        return Result<TorrentDocument>::failure(
            unsupported("v2 and hybrid torrent mapping is not implemented"));
    }

    std::vector<DocumentWarning> warnings;
    bool retained = false;
    std::vector<MetadataFieldInfo> fields;
    auto top_extensions = scan_dictionary_fields(root, bytes, FieldDictionary::TopLevelV1, mode,
                                                 "top-level", warnings, fields);
    if (!top_extensions)
    {
        return Result<TorrentDocument>::failure(top_extensions.error());
    }
    retained = top_extensions.value();
    auto info_extensions = scan_dictionary_fields(*info, bytes, FieldDictionary::InfoV1, mode,
                                                  "info", warnings, fields);
    if (!info_extensions)
    {
        return Result<TorrentDocument>::failure(info_extensions.error());
    }
    retained = retained || info_extensions.value();

    auto name = read_info_name(*info, bytes, mode, warnings, retained);
    if (!name)
    {
        return Result<TorrentDocument>::failure(name.error());
    }
    const auto piece_length = unsigned_value(lookup(*info, bytes, "piece length"));
    const auto pieces = string_value(lookup(*info, bytes, "pieces"), bytes);
    if (!piece_length || *piece_length == 0U || !pieces || pieces->size() % 20U != 0U)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid v1 piece fields"));
    }

    std::vector<FileEntry> files;
    std::uint64_t total_size = 0;
    const auto append_file = [&](std::vector<std::string> segments, const std::uint64_t length,
                                 const ParsedFileAttributes& attributes) -> Result<void> {
        if (length > std::numeric_limits<std::uint64_t>::max() - total_size)
        {
            return Result<void>::failure(invalid_torrent("total file length overflows uint64"));
        }
        auto path = LogicalPath::from_segments(std::move(segments));
        if (!path)
        {
            return Result<void>::failure(invalid_torrent("unsafe or invalid torrent file path"));
        }
        auto file =
            FileEntry::create(std::move(path).value(), length, attributes.attributes, std::nullopt,
                              attributes.sha1_hint, attributes.symlink_target);
        if (!file)
        {
            return Result<void>::failure(invalid_torrent("invalid torrent file"));
        }
        total_size += length;
        files.push_back(std::move(file).value());
        return Result<void>::success();
    };

    const auto* file_list = lookup(*info, bytes, "files");
    if (file_list != nullptr)
    {
        if (file_list->kind != BencodeNode::Kind::List || has_key(*info, bytes, "length"))
        {
            return Result<TorrentDocument>::failure(
                invalid_torrent("v1 files must be a list without single-file length"));
        }
        const auto& entries = std::get<BencodeList>(file_list->children);
        if (entries.empty())
        {
            return Result<TorrentDocument>::failure(invalid_torrent("v1 files list is empty"));
        }
        for (const auto& entry : entries)
        {
            if (entry.kind != BencodeNode::Kind::Dictionary)
            {
                return Result<TorrentDocument>::failure(
                    invalid_torrent("v1 file entry must be a dictionary"));
            }
            auto file_extensions = scan_dictionary_fields(entry, bytes, FieldDictionary::InfoV1File,
                                                          mode, "info.files", warnings, fields);
            if (!file_extensions)
            {
                return Result<TorrentDocument>::failure(file_extensions.error());
            }
            retained = retained || file_extensions.value();
            auto spec = read_file_spec(entry, bytes, "info.files", warnings, retained);
            if (!spec)
            {
                return Result<TorrentDocument>::failure(spec.error());
            }
            const auto* path_node = lookup(entry, bytes, "path");
            std::vector<std::string> segments;
            if (path_node == nullptr && spec.value().attributes.attributes.padding)
            {
                segments = {".pad", std::to_string(spec.value().length)};
            }
            else if (path_node == nullptr || path_node->kind != BencodeNode::Kind::List)
            {
                return Result<TorrentDocument>::failure(invalid_torrent("invalid v1 file"));
            }
            else
            {
                for (const auto& segment_node : std::get<BencodeList>(path_node->children))
                {
                    auto segment = string_value(&segment_node, bytes);
                    if (!segment || !valid_utf8(*segment))
                    {
                        return Result<TorrentDocument>::failure(
                            invalid_torrent("v1 path segment must be valid UTF-8"));
                    }
                    segments.push_back(std::move(*segment));
                }
            }
            auto appended =
                append_file(std::move(segments), spec.value().length, spec.value().attributes);
            if (!appended)
            {
                return Result<TorrentDocument>::failure(appended.error());
            }
        }
    }
    else
    {
        auto spec = read_file_spec(*info, bytes, "info", warnings, retained);
        if (!spec)
        {
            return Result<TorrentDocument>::failure(spec.error());
        }
        Result<void> appended = Result<void>::failure(invalid_torrent("invalid single file"));
        if (name.value().empty())
        {
            auto file = FileEntry::create(MetadataEngineAccess::unavailable_path(),
                                          spec.value().length, spec.value().attributes.attributes,
                                          std::nullopt, spec.value().attributes.sha1_hint,
                                          spec.value().attributes.symlink_target);
            if (file)
            {
                total_size += spec.value().length;
                files.push_back(std::move(file).value());
                appended = Result<void>::success();
            }
        }
        else
        {
            appended = append_file({name.value()}, spec.value().length, spec.value().attributes);
        }
        if (!appended)
        {
            return Result<TorrentDocument>::failure(appended.error());
        }
    }

    std::vector<Sha1Digest> piece_hashes;
    piece_hashes.reserve(pieces->size() / 20U);
    for (std::size_t offset = 0; offset < pieces->size(); offset += 20U)
    {
        Sha1Digest::Bytes digest{};
        std::memcpy(digest.data(), pieces->data() + offset, digest.size());
        piece_hashes.push_back(Sha1Digest::from_bytes(digest));
    }
    auto piece_info =
        PieceInfo::create(TorrentFormat::V1, *piece_length, total_size, std::move(piece_hashes));
    if (!piece_info)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid v1 piece layout"));
    }
    auto hashes =
        InfoHashes::create(TorrentFormat::V1, sha1(bytes, info->encoded_span), std::nullopt);
    if (!hashes)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid v1 info hash"));
    }

    bool private_flag = false;
    const auto* private_node = lookup(*info, bytes, "private");
    if (private_node != nullptr)
    {
        const auto value = unsigned_value(private_node);
        if (!value || *value > 1U)
        {
            return Result<TorrentDocument>::failure(
                invalid_torrent("private must be integer zero or one"));
        }
        private_flag = *value == 1U;
    }

    auto torrent_info =
        make_mapped_info(std::move(name).value(), TorrentFormat::V1, std::move(hashes).value(),
                         std::move(piece_info).value(), std::move(files), private_flag);
    if (!torrent_info)
    {
        return Result<TorrentDocument>::failure(invalid_torrent("invalid torrent info"));
    }

    bool source_in_info = false;
    auto metadata = read_metadata(root, *info, bytes, mode, warnings, retained, source_in_info);
    if (!metadata)
    {
        return Result<TorrentDocument>::failure(metadata.error());
    }
    auto trackers = read_trackers(root, bytes, mode, warnings, retained);
    if (!trackers)
    {
        return Result<TorrentDocument>::failure(trackers.error());
    }

    std::vector<RetainedTopLevelEntry> entries;
    for (const auto& entry : std::get<BencodeDictionary>(root.children))
    {
        entries.push_back({raw(bytes, entry.first), entry.second.encoded_span});
    }

    auto info_value = std::move(torrent_info).value();
    auto metadata_value = std::move(metadata).value();
    auto tracker_value = std::move(trackers).value();
    auto state = std::make_shared<RetainedDocumentState>(document.bytes, info_value, metadata_value,
                                                         tracker_value, std::move(entries),
                                                         source_in_info, mode);
    return make_retained_document(std::move(info_value), std::move(metadata_value),
                                  std::move(tracker_value), std::move(warnings), std::move(fields),
                                  std::move(state), retained);
}

[[nodiscard]] bool same_info(const TorrentInfo& left, const TorrentInfo& right)
{
    if (left.name() != right.name() || left.format() != right.format() ||
        left.is_private() != right.is_private() ||
        left.info_hashes().v1() != right.info_hashes().v1() ||
        left.info_hashes().v2() != right.info_hashes().v2() ||
        left.pieces().piece_length() != right.pieces().piece_length() ||
        left.pieces().total_size() != right.pieces().total_size() ||
        left.pieces().v1_piece_hashes() != right.pieces().v1_piece_hashes() ||
        left.files().size() != right.files().size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.files().size(); ++index)
    {
        const auto& left_file = left.files()[index];
        const auto& right_file = right.files()[index];
        if (left_file.path() != right_file.path() || left_file.length() != right_file.length() ||
            left_file.attributes().padding != right_file.attributes().padding ||
            left_file.attributes().executable != right_file.attributes().executable ||
            left_file.attributes().hidden != right_file.attributes().hidden ||
            left_file.attributes().symlink != right_file.attributes().symlink ||
            left_file.pieces_root() != right_file.pieces_root() ||
            left_file.sha1_hint() != right_file.sha1_hint() ||
            left_file.symlink_target() != right_file.symlink_target())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_web_seeds(const std::vector<WebSeedUrl>& left,
                                  const std::vector<WebSeedUrl>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index].value() != right[index].value())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_dht_nodes(const std::vector<DhtNode>& left,
                                  const std::vector<DhtNode>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index].host() != right[index].host() || left[index].port() != right[index].port())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_trackers(const TrackerList& left, const TrackerList& right)
{
    if (left.tiers().size() != right.tiers().size())
    {
        return false;
    }
    for (std::size_t tier = 0; tier < left.tiers().size(); ++tier)
    {
        const auto& left_urls = left.tiers()[tier].trackers();
        const auto& right_urls = right.tiers()[tier].trackers();
        if (left_urls.size() != right_urls.size())
        {
            return false;
        }
        for (std::size_t index = 0; index < left_urls.size(); ++index)
        {
            if (left_urls[index].value() != right_urls[index].value())
            {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool same_metadata(const TorrentMetadata& left, const TorrentMetadata& right)
{
    return left.comment() == right.comment() && left.creator() == right.creator() &&
           left.source() == right.source() &&
           left.creation_time_unix_seconds() == right.creation_time_unix_seconds() &&
           same_web_seeds(left.web_seeds(), right.web_seeds()) &&
           left.collections() == right.collections() &&
           same_dht_nodes(left.dht_nodes(), right.dht_nodes());
}

void append(Bytes& target, const std::string_view value)
{
    target.insert(target.end(), value.begin(), value.end());
}

[[nodiscard]] Bytes encode_string(const std::string_view value)
{
    Bytes output;
    append(output, std::to_string(value.size()));
    output.push_back(static_cast<std::uint8_t>(':'));
    output.insert(output.end(), value.begin(), value.end());
    return output;
}

[[nodiscard]] Bytes encode_integer(const std::int64_t value)
{
    Bytes output{static_cast<std::uint8_t>('i')};
    append(output, std::to_string(value));
    output.push_back(static_cast<std::uint8_t>('e'));
    return output;
}

[[nodiscard]] Bytes encode_string_list(const std::vector<std::string>& values)
{
    Bytes output{static_cast<std::uint8_t>('l')};
    for (const auto& value : values)
    {
        auto encoded = encode_string(value);
        output.insert(output.end(), encoded.begin(), encoded.end());
    }
    output.push_back(static_cast<std::uint8_t>('e'));
    return output;
}

struct EncodedTopLevelEntry
{
    std::string key;
    Bytes value;
};

void erase_entry(std::vector<EncodedTopLevelEntry>& entries, const std::string_view key)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [key](const auto& entry) { return entry.key == key; }),
                  entries.end());
}

void set_entry(std::vector<EncodedTopLevelEntry>& entries, std::string key, Bytes value)
{
    erase_entry(entries, key);
    entries.push_back({std::move(key), std::move(value)});
}

void set_optional_string(std::vector<EncodedTopLevelEntry>& entries, const std::string& key,
                         const std::optional<std::string>& value)
{
    erase_entry(entries, key);
    if (value)
    {
        set_entry(entries, key, encode_string(*value));
    }
}

[[nodiscard]] Bytes encode_trackers(const TrackerList& trackers)
{
    Bytes output{static_cast<std::uint8_t>('l')};
    for (const auto& tier : trackers.tiers())
    {
        output.push_back(static_cast<std::uint8_t>('l'));
        for (const auto& tracker : tier.trackers())
        {
            auto encoded = encode_string(tracker.value());
            output.insert(output.end(), encoded.begin(), encoded.end());
        }
        output.push_back(static_cast<std::uint8_t>('e'));
    }
    output.push_back(static_cast<std::uint8_t>('e'));
    return output;
}

[[nodiscard]] Bytes encode_web_seeds(const std::vector<WebSeedUrl>& seeds)
{
    std::vector<std::string> values;
    values.reserve(seeds.size());
    for (const auto& seed : seeds)
    {
        values.push_back(seed.value());
    }
    return encode_string_list(values);
}

[[nodiscard]] Bytes encode_dht_nodes(const std::vector<DhtNode>& nodes)
{
    Bytes output{static_cast<std::uint8_t>('l')};
    for (const auto& node : nodes)
    {
        output.push_back(static_cast<std::uint8_t>('l'));
        auto host = encode_string(node.host());
        output.insert(output.end(), host.begin(), host.end());
        auto port = encode_integer(node.port());
        output.insert(output.end(), port.begin(), port.end());
        output.push_back(static_cast<std::uint8_t>('e'));
    }
    output.push_back(static_cast<std::uint8_t>('e'));
    return output;
}

[[nodiscard]] bool raw_byte_less(const std::string& left, const std::string& right)
{
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](const char lhs, const char rhs) {
            return static_cast<unsigned char>(lhs) < static_cast<unsigned char>(rhs);
        });
}

[[nodiscard]] std::size_t encoded_key_start(const std::vector<std::uint8_t>& bytes,
                                            const BencodeSpan key_span)
{
    if (key_span.offset == 0U || bytes[key_span.offset - 1U] != ':')
    {
        return key_span.offset;
    }
    auto position = key_span.offset - 1U;
    while (position > 0U && bytes[position - 1U] >= static_cast<std::uint8_t>('0') &&
           bytes[position - 1U] <= static_cast<std::uint8_t>('9'))
    {
        --position;
    }
    return position;
}

[[nodiscard]] bool sorted_info_keys(const BencodeNode& info, const std::vector<std::uint8_t>& bytes)
{
    const auto& entries = std::get<BencodeDictionary>(info.children);
    std::optional<std::string> previous;
    for (const auto& entry : entries)
    {
        const auto key = raw(bytes, entry.first);
        if (previous && raw_byte_less(key, *previous))
        {
            return false;
        }
        previous = key;
    }
    return true;
}

[[nodiscard]] Bytes encode_identity_key_value(const InfoIdentityPatch& patch)
{
    switch (patch.field)
    {
    case InfoIdentityField::Private:
        return encode_integer(patch.private_value ? 1 : 0);
    case InfoIdentityField::Name:
    case InfoIdentityField::Source:
        return encode_string(patch.value);
    }
    return {};
}

[[nodiscard]] std::string identity_key(const InfoIdentityField field)
{
    switch (field)
    {
    case InfoIdentityField::Private:
        return "private";
    case InfoIdentityField::Name:
        return "name";
    case InfoIdentityField::Source:
        return "source";
    }
    return {};
}

[[nodiscard]] FieldDictionary info_dictionary_for_format(const TorrentFormat format)
{
    switch (format)
    {
    case TorrentFormat::V1:
        return FieldDictionary::InfoV1;
    case TorrentFormat::V2:
        return FieldDictionary::InfoV2;
    case TorrentFormat::Hybrid:
        return FieldDictionary::InfoHybrid;
    }
    return FieldDictionary::InfoV1;
}

[[nodiscard]] Result<std::optional<Bytes>>
splice_info_identity(const std::vector<std::uint8_t>& bytes, const BencodeNode& info,
                     const InfoIdentityPatch& patch)
{
    if (info.kind != BencodeNode::Kind::Dictionary ||
        info.encoded_span.offset + info.encoded_span.size > bytes.size() ||
        info.encoded_span.size < 2U ||
        bytes[info.encoded_span.offset] != static_cast<std::uint8_t>('d') ||
        bytes[info.encoded_span.offset + info.encoded_span.size - 1U] !=
            static_cast<std::uint8_t>('e'))
    {
        return Result<std::optional<Bytes>>::failure(
            metadata_error(ErrorCode::ValidationFailed, "info dictionary span is invalid"));
    }
    if (!sorted_info_keys(info, bytes))
    {
        return Result<std::optional<Bytes>>::failure(
            metadata_error(ErrorCode::ValidationFailed, "info dictionary keys are not sorted"));
    }

    const auto key = identity_key(patch.field);
    const auto& entries = std::get<BencodeDictionary>(info.children);
    const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& item) {
        return raw(bytes, item.first) == key;
    });

    if (patch.field == InfoIdentityField::Private && entry != entries.end())
    {
        if (entry->second.kind != BencodeNode::Kind::Integer || entry->second.integer < 0 ||
            entry->second.integer > 1)
        {
            return Result<std::optional<Bytes>>::failure(
                metadata_error(ErrorCode::ValidationFailed, "private must be integer zero or one"));
        }
        if ((entry->second.integer == 1) == patch.private_value)
        {
            return Result<std::optional<Bytes>>::success(std::nullopt);
        }
    }
    if (patch.field != InfoIdentityField::Private && !patch.clear && !valid_utf8(patch.value))
    {
        return Result<std::optional<Bytes>>::failure(
            metadata_error(ErrorCode::ValidationFailed, key + " must contain valid UTF-8"));
    }
    if (patch.field == InfoIdentityField::Name && (patch.clear || patch.value.empty()))
    {
        return Result<std::optional<Bytes>>::failure(
            metadata_error(ErrorCode::ValidationFailed, "info name must be non-empty"));
    }
    if (patch.field != InfoIdentityField::Private && entry != entries.end())
    {
        if (entry->second.kind != BencodeNode::Kind::String)
        {
            return Result<std::optional<Bytes>>::failure(
                metadata_error(ErrorCode::ValidationFailed, key + " must be a byte string"));
        }
        if (!patch.clear && raw(bytes, entry->second.string_span) == patch.value)
        {
            return Result<std::optional<Bytes>>::success(std::nullopt);
        }
    }
    if (entry == entries.end() && patch.clear)
    {
        return Result<std::optional<Bytes>>::success(std::nullopt);
    }
    if (entry == entries.end() && patch.field == InfoIdentityField::Name)
    {
        return Result<std::optional<Bytes>>::failure(
            metadata_error(ErrorCode::ValidationFailed, "info name is required"));
    }

    std::size_t start = info.encoded_span.offset + info.encoded_span.size - 1U;
    std::size_t end = start;
    Bytes replacement;
    if (entry != entries.end())
    {
        start = patch.clear ? encoded_key_start(bytes, entry->first)
                            : entry->second.encoded_span.offset;
        end = entry->second.encoded_span.offset + entry->second.encoded_span.size;
        if (!patch.clear)
        {
            replacement = encode_identity_key_value(patch);
        }
    }
    else
    {
        replacement = encode_string(key);
        auto value = encode_identity_key_value(patch);
        replacement.insert(replacement.end(), value.begin(), value.end());
        for (const auto& item : entries)
        {
            if (raw_byte_less(key, raw(bytes, item.first)))
            {
                start = encoded_key_start(bytes, item.first);
                break;
            }
        }
    }

    Bytes output;
    output.reserve(bytes.size() - (end - start) + replacement.size());
    output.insert(output.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(start));
    output.insert(output.end(), replacement.begin(), replacement.end());
    output.insert(output.end(), bytes.begin() + static_cast<std::ptrdiff_t>(end), bytes.end());
    return Result<std::optional<Bytes>>::success(std::move(output));
}

[[nodiscard]] Result<Bytes> encode_patch(const TorrentDocument& candidate,
                                         const RetainedDocumentState& state)
{
    if (!same_info(candidate.info(), state.original_info))
    {
        return Result<Bytes>::failure(
            metadata_error(ErrorCode::Conflict, "candidate info differs from retained info"));
    }

    std::vector<EncodedTopLevelEntry> entries;
    entries.reserve(state.top_level_entries.size() + 8U);
    for (const auto& retained : state.top_level_entries)
    {
        const auto begin =
            state.bytes->begin() + static_cast<std::ptrdiff_t>(retained.value_span.offset);
        const auto end = begin + static_cast<std::ptrdiff_t>(retained.value_span.size);
        entries.push_back({retained.key, Bytes(begin, end)});
    }

    const auto& original_metadata = state.original_metadata;
    const auto& metadata = candidate.metadata();
    if (metadata.comment() != original_metadata.comment())
    {
        set_optional_string(entries, "comment", metadata.comment());
    }
    if (metadata.creator() != original_metadata.creator())
    {
        set_optional_string(entries, "created by", metadata.creator());
    }
    if (metadata.source() != original_metadata.source())
    {
        return Result<Bytes>::failure(metadata_error(
            ErrorCode::Conflict, "top-level source is read-only compatibility metadata"));
    }
    const auto& creation_time = metadata.creation_time_unix_seconds();
    if (creation_time != original_metadata.creation_time_unix_seconds())
    {
        erase_entry(entries, "creation date");
        if (creation_time)
        {
            set_entry(entries, "creation date", encode_integer(creation_time.value()));
        }
    }
    if (!same_web_seeds(metadata.web_seeds(), original_metadata.web_seeds()))
    {
        erase_entry(entries, "url-list");
        erase_entry(entries, "httpseeds");
        if (!metadata.web_seeds().empty())
        {
            set_entry(entries, "url-list", encode_web_seeds(metadata.web_seeds()));
        }
    }
    if (metadata.collections() != original_metadata.collections())
    {
        erase_entry(entries, "collections");
        if (!metadata.collections().empty())
        {
            set_entry(entries, "collections", encode_string_list(metadata.collections()));
        }
    }
    if (!same_dht_nodes(metadata.dht_nodes(), original_metadata.dht_nodes()))
    {
        erase_entry(entries, "nodes");
        if (!metadata.dht_nodes().empty())
        {
            set_entry(entries, "nodes", encode_dht_nodes(metadata.dht_nodes()));
        }
    }
    if (!same_trackers(candidate.trackers(), state.original_trackers))
    {
        erase_entry(entries, "announce");
        erase_entry(entries, "announce-list");
        if (!candidate.trackers().tiers().empty())
        {
            const auto& first = candidate.trackers().tiers().front().trackers().front();
            set_entry(entries, "announce", encode_string(first.value()));
            set_entry(entries, "announce-list", encode_trackers(candidate.trackers()));
        }
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return raw_byte_less(left.key, right.key);
    });
    Bytes output{static_cast<std::uint8_t>('d')};
    for (const auto& entry : entries)
    {
        auto key = encode_string(entry.key);
        output.insert(output.end(), key.begin(), key.end());
        output.insert(output.end(), entry.value.begin(), entry.value.end());
    }
    output.push_back(static_cast<std::uint8_t>('e'));
    return Result<Bytes>::success(std::move(output));
}
} // namespace

Result<TorrentDocument> decode_torrent(std::vector<std::uint8_t> bytes, const MetadataReadMode mode)
{
    return decode_torrent(std::move(bytes), mode, {});
}

Result<TorrentDocument> decode_torrent(std::vector<std::uint8_t> bytes, const MetadataReadMode mode,
                                       const BencodeLimits& limits)
{
    auto decoded = BencodeAdapter::decode(std::move(bytes), limits);
    if (!decoded)
    {
        return Result<TorrentDocument>::failure(decoded.error());
    }
    if (decoded.value().root.kind != BencodeNode::Kind::Dictionary)
    {
        return Result<TorrentDocument>::failure(
            invalid_torrent("torrent root must be a dictionary"));
    }
    const auto* info = lookup(decoded.value().root, *decoded.value().bytes, "info");
    if (info != nullptr && info->kind == BencodeNode::Kind::Dictionary &&
        has_key(*info, *decoded.value().bytes, "meta version"))
    {
        return map_v2(decoded.value(), mode);
    }
    return map_v1(decoded.value(), mode);
}

Result<TorrentDocument> patch_info_identity(const TorrentDocument& document,
                                            const InfoIdentityPatch& patch)
{
    const auto* state = retained_state(document);
    if (state == nullptr || state->bytes == nullptr)
    {
        return Result<TorrentDocument>::failure(
            metadata_error(ErrorCode::Conflict, "document has no retained torrent bytes"));
    }

    const auto patchability = info_patchability(
        identity_key(patch.field), info_dictionary_for_format(document.info().format()));
    if (patchability != InfoPatchability::StandardIdentity &&
        patchability != InfoPatchability::ExplicitExtensionIdentity)
    {
        return Result<TorrentDocument>::failure(
            metadata_error(ErrorCode::UnsupportedFeature, "info field is not patchable"));
    }

    auto decoded = BencodeAdapter::decode(*state->bytes);
    if (!decoded)
    {
        return Result<TorrentDocument>::failure(decoded.error());
    }
    const auto* info = lookup(decoded.value().root, *decoded.value().bytes, "info");
    if (info == nullptr || info->kind != BencodeNode::Kind::Dictionary)
    {
        return Result<TorrentDocument>::failure(
            metadata_error(ErrorCode::InvalidTorrent, "top-level info dictionary is required"));
    }

    auto patched = splice_info_identity(*decoded.value().bytes, *info, patch);
    if (!patched)
    {
        return Result<TorrentDocument>::failure(patched.error());
    }
    auto patched_bytes = std::move(patched).value();
    if (!patched_bytes)
    {
        return Result<TorrentDocument>::success(document);
    }

    auto reparsed = decode_torrent(std::move(*patched_bytes), state->mode);
    if (!reparsed)
    {
        return Result<TorrentDocument>::failure(reparsed.error());
    }
    return reparsed;
}

Result<MetadataEncodeOutcome> encode_top_level_patch(const TorrentDocument& candidate)
{
    const auto* state = retained_state(candidate);
    if (state == nullptr || !same_info(candidate.info(), state->original_info))
    {
        return Result<MetadataEncodeOutcome>::success({MetadataEncodeDisposition::NeedRebuild, {}});
    }
    if (candidate.metadata().source() != state->original_metadata.source())
    {
        return Result<MetadataEncodeOutcome>::success({MetadataEncodeDisposition::NeedRebuild, {}});
    }
    if (same_metadata(candidate.metadata(), state->original_metadata) &&
        same_trackers(candidate.trackers(), state->original_trackers))
    {
        return Result<MetadataEncodeOutcome>::success(
            {MetadataEncodeDisposition::Encoded, *state->bytes});
    }

    auto encoded = encode_patch(candidate, *state);
    if (!encoded)
    {
        if (encoded.error().code == ErrorCode::Conflict)
        {
            return Result<MetadataEncodeOutcome>::success(
                {MetadataEncodeDisposition::NeedRebuild, {}});
        }
        return Result<MetadataEncodeOutcome>::failure(encoded.error());
    }
    return Result<MetadataEncodeOutcome>::success(
        {MetadataEncodeDisposition::Encoded, std::move(encoded).value()});
}

bool source_is_in_info(const TorrentDocument& document) noexcept
{
    const auto* state = retained_state(document);
    return state != nullptr && state->source_in_info;
}

[[nodiscard]] std::string escaped_metadata_string(const std::string& value)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(value.size() + 2U);
    output.push_back('"');
    for (const auto byte : value)
    {
        const auto character = static_cast<unsigned char>(byte);
        switch (character)
        {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character < 0x20U)
            {
                output += "\\x";
                output.push_back(hex[character >> 4U]);
                output.push_back(hex[character & 0x0fU]);
            }
            else
            {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
    return output;
}

[[nodiscard]] std::string metadata_value_text(const BencodeNode& node,
                                              const std::vector<std::uint8_t>& bytes,
                                              const std::size_t depth = 0U)
{
    if (depth > 3U)
    {
        return "…";
    }
    switch (node.kind)
    {
    case BencodeNode::Kind::Integer:
        return std::to_string(node.integer);
    case BencodeNode::Kind::String: {
        const auto value = raw(bytes, node.string_span);
        if (valid_utf8(value))
        {
            return escaped_metadata_string(value);
        }
        return "<binary " + std::to_string(value.size()) + " bytes>";
    }
    case BencodeNode::Kind::List: {
        std::string output{"["};
        const auto& children = std::get<BencodeList>(node.children);
        for (std::size_t index = 0; index < children.size(); ++index)
        {
            if (index != 0U)
            {
                output += ", ";
            }
            output += metadata_value_text(children[index], bytes, depth + 1U);
        }
        output.push_back(']');
        return output;
    }
    case BencodeNode::Kind::Dictionary: {
        std::string output{"{"};
        const auto& children = std::get<BencodeDictionary>(node.children);
        for (std::size_t index = 0; index < children.size(); ++index)
        {
            if (index != 0U)
            {
                output += ", ";
            }
            output += escaped_metadata_string(raw(bytes, children[index].first));
            output += ": ";
            output += metadata_value_text(children[index].second, bytes, depth + 1U);
        }
        output.push_back('}');
        return output;
    }
    }
    return {};
}

[[nodiscard]] const MetadataFieldInfo* metadata_field_info(const TorrentDocument& document,
                                                           const std::string_view key,
                                                           const MetadataFieldScope scope)
{
    for (const auto& field : document.metadata_fields())
    {
        if (field.key == key && field.scope == scope)
        {
            return &field;
        }
    }
    return nullptr;
}

std::vector<MetadataFieldValue> metadata_field_values(const TorrentDocument& document)
{
    const auto* state = retained_state(document);
    if (state == nullptr || state->bytes == nullptr)
    {
        return {};
    }
    auto decoded = BencodeAdapter::decode(*state->bytes);
    if (!decoded || decoded.value().root.kind != BencodeNode::Kind::Dictionary)
    {
        return {};
    }

    std::vector<MetadataFieldValue> values;
    const auto& bytes = *decoded.value().bytes;
    const auto append = [&](const BencodeNode& dictionary, const MetadataFieldScope scope) {
        if (dictionary.kind != BencodeNode::Kind::Dictionary)
        {
            return;
        }
        for (const auto& entry : std::get<BencodeDictionary>(dictionary.children))
        {
            const auto key = raw(bytes, entry.first);
            if ((scope == MetadataFieldScope::TopLevel && key == "info") ||
                (scope == MetadataFieldScope::Info && (key == "files" || key == "file tree")))
            {
                continue;
            }
            const auto* info = metadata_field_info(document, key, scope);
            values.push_back({key, scope, info == nullptr ? std::string{} : info->type,
                              metadata_value_text(entry.second, bytes)});
        }
    };

    append(decoded.value().root, MetadataFieldScope::TopLevel);
    const auto* info = lookup(decoded.value().root, bytes, "info");
    if (info != nullptr)
    {
        append(*info, MetadataFieldScope::Info);
    }
    return values;
}

} // namespace torrentutils::core::detail

namespace torrentutils::core {
std::vector<MetadataFieldValue> TorrentDocument::metadata_field_values() const
{
    return detail::metadata_field_values(*this);
}
} // namespace torrentutils::core
