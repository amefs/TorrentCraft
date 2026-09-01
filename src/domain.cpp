#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <sstream>
#include <torrentutils/core/domain.hpp>
#include <unordered_set>
#include <utility>

namespace torrentutils::core {
namespace {

[[nodiscard]] Error validation_error(std::string field, std::string message)
{
    return {ErrorCode::ValidationFailed,
            "domain validation failed",
            {{std::move(field), std::move(message)}}};
}

[[nodiscard]] bool is_valid_utf8(const std::string& value) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index = 0;
    while (index < value.size())
    {
        const auto first = bytes[index];
        if (first <= 0x7F)
        {
            ++index;
            continue;
        }

        std::size_t count = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if ((first & 0xE0U) == 0xC0U)
        {
            count = 2;
            code_point = first & 0x1FU;
            minimum = 0x80;
        }
        else if ((first & 0xF0U) == 0xE0U)
        {
            count = 3;
            code_point = first & 0x0FU;
            minimum = 0x800;
        }
        else if ((first & 0xF8U) == 0xF0U)
        {
            count = 4;
            code_point = first & 0x07U;
            minimum = 0x10000;
        }
        else
        {
            return false;
        }

        if (index + count > value.size())
        {
            return false;
        }
        for (std::size_t offset = 1; offset < count; ++offset)
        {
            const auto next = bytes[index + offset];
            if ((next & 0xC0U) != 0x80U)
            {
                return false;
            }
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU))
        {
            return false;
        }
        index += count;
    }
    return true;
}

[[nodiscard]] bool contains_uri_whitespace_or_control(const std::string& value) noexcept
{
    return std::any_of(value.begin(), value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte <= 0x20U || byte == 0x7FU;
    });
}

[[nodiscard]] std::string ascii_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

[[nodiscard]] bool is_decimal(const std::string& value) noexcept
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

struct ParsedUrl
{
    std::string scheme;
    std::string comparison_key;
};

[[nodiscard]] Result<ParsedUrl> parse_url(const std::string& value,
                                          const std::vector<std::string>& allowed_schemes,
                                          std::string field)
{
    if (value.empty())
    {
        return Result<ParsedUrl>::failure(validation_error(std::move(field), "must not be empty"));
    }
    if (!is_valid_utf8(value))
    {
        return Result<ParsedUrl>::failure(
            validation_error(std::move(field), "must contain valid UTF-8"));
    }
    if (contains_uri_whitespace_or_control(value))
    {
        return Result<ParsedUrl>::failure(validation_error(
            std::move(field), "must not contain whitespace or control characters"));
    }

    const auto separator = value.find("://");
    if (separator == std::string::npos || separator == 0)
    {
        return Result<ParsedUrl>::failure(
            validation_error(std::move(field), "must contain a URI scheme and authority"));
    }

    auto scheme = ascii_lower(value.substr(0, separator));
    if (std::find(allowed_schemes.begin(), allowed_schemes.end(), scheme) == allowed_schemes.end())
    {
        return Result<ParsedUrl>::failure(
            validation_error(std::move(field), "uses an unsupported URI scheme"));
    }

    const auto authority_start = separator + 3;
    const auto suffix_start = value.find_first_of("/?#", authority_start);
    const auto authority_end = suffix_start == std::string::npos ? value.size() : suffix_start;
    auto authority = value.substr(authority_start, authority_end - authority_start);
    if (authority.empty())
    {
        return Result<ParsedUrl>::failure(
            validation_error(std::move(field), "must contain a host"));
    }

    std::string user_info;
    const auto at = authority.rfind('@');
    if (at != std::string::npos)
    {
        user_info = authority.substr(0, at + 1);
        authority.erase(0, at + 1);
    }

    std::string host;
    std::string port;
    if (!authority.empty() && authority.front() == '[')
    {
        const auto close = authority.find(']');
        if (close == std::string::npos)
        {
            return Result<ParsedUrl>::failure(
                validation_error(std::move(field), "contains an invalid IPv6 host"));
        }
        host = authority.substr(0, close + 1);
        if (close + 1 < authority.size())
        {
            if (authority[close + 1] != ':')
            {
                return Result<ParsedUrl>::failure(
                    validation_error(std::move(field), "contains an invalid authority"));
            }
            port = authority.substr(close + 2);
        }
    }
    else
    {
        const auto first_colon = authority.find(':');
        const auto last_colon = authority.rfind(':');
        if (first_colon != std::string::npos && first_colon != last_colon)
        {
            return Result<ParsedUrl>::failure(
                validation_error(std::move(field), "IPv6 hosts must use brackets"));
        }
        if (last_colon != std::string::npos)
        {
            host = authority.substr(0, last_colon);
            port = authority.substr(last_colon + 1);
        }
        else
        {
            host = authority;
        }
    }

    if (host.empty())
    {
        return Result<ParsedUrl>::failure(
            validation_error(std::move(field), "must contain a host"));
    }
    if (!port.empty())
    {
        if (!is_decimal(port))
        {
            return Result<ParsedUrl>::failure(
                validation_error(std::move(field), "contains an invalid port"));
        }
        std::uint32_t parsed_port = 0;
        for (const auto digit : port)
        {
            const auto digit_value = static_cast<std::uint32_t>(digit - '0');
            if (parsed_port > (65535U - digit_value) / 10U)
            {
                return Result<ParsedUrl>::failure(
                    validation_error(std::move(field), "contains a port outside 1-65535"));
            }
            parsed_port = parsed_port * 10U + digit_value;
        }
        if (parsed_port == 0)
        {
            return Result<ParsedUrl>::failure(
                validation_error(std::move(field), "contains a port outside 1-65535"));
        }
        if ((scheme == "http" && parsed_port == 80) || (scheme == "https" && parsed_port == 443))
        {
            port.clear();
        }
    }
    else if (!authority.empty() && authority.back() == ':')
    {
        return Result<ParsedUrl>::failure(
            validation_error(std::move(field), "contains an empty port"));
    }

    std::string key = scheme + "://" + user_info + ascii_lower(std::move(host));
    if (!port.empty())
    {
        key += ':';
        key += port;
    }
    if (suffix_start != std::string::npos)
    {
        key += value.substr(suffix_start);
    }
    return Result<ParsedUrl>::success({std::move(scheme), std::move(key)});
}

[[nodiscard]] int hex_value(char character) noexcept
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f')
    {
        return 10 + character - 'a';
    }
    if (character >= 'A' && character <= 'F')
    {
        return 10 + character - 'A';
    }
    return -1;
}

template <std::size_t Size>
[[nodiscard]] Result<std::array<std::uint8_t, Size>> parse_digest_hex(std::string value,
                                                                      std::string field)
{
    if (value.size() != Size * 2)
    {
        return Result<std::array<std::uint8_t, Size>>::failure(
            validation_error(std::move(field), "has an invalid hexadecimal length"));
    }

    std::array<std::uint8_t, Size> bytes{};
    for (std::size_t index = 0; index < Size; ++index)
    {
        const auto high = hex_value(value[index * 2]);
        const auto low = hex_value(value[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            return Result<std::array<std::uint8_t, Size>>::failure(
                validation_error(std::move(field), "contains a non-hexadecimal character"));
        }
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return Result<std::array<std::uint8_t, Size>>::success(bytes);
}

template <std::size_t Size>
[[nodiscard]] std::string digest_to_hex(const std::array<std::uint8_t, Size>& bytes)
{
    constexpr char kHex[] = "0123456789abcdef";
    std::string value(Size * 2, '0');
    for (std::size_t index = 0; index < Size; ++index)
    {
        value[index * 2] = kHex[bytes[index] >> 4U];
        value[index * 2 + 1] = kHex[bytes[index] & 0x0FU];
    }
    return value;
}

[[nodiscard]] bool is_power_of_two(std::uint64_t value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] bool is_known_format(TorrentFormat format) noexcept
{
    return format == TorrentFormat::V1 || format == TorrentFormat::V2 ||
           format == TorrentFormat::Hybrid;
}

} // namespace

Sha1Digest::Sha1Digest(Bytes bytes) noexcept : bytes_(bytes) {}

Sha1Digest Sha1Digest::from_bytes(Bytes bytes) noexcept
{
    return Sha1Digest(bytes);
}

Result<Sha1Digest> Sha1Digest::from_hex(std::string value)
{
    auto bytes = parse_digest_hex<20>(std::move(value), "hash.sha1");
    if (!bytes)
    {
        const auto message = bytes.error().issues.empty()
                                 ? std::string("contains an invalid SHA-1 digest")
                                 : bytes.error().issues.front().message;
        return Result<Sha1Digest>::failure(validation_error("hash.sha1", message));
    }
    return Result<Sha1Digest>::success(Sha1Digest(std::move(bytes).value()));
}

const Sha1Digest::Bytes& Sha1Digest::bytes() const noexcept
{
    return bytes_;
}

std::string Sha1Digest::to_hex() const
{
    return digest_to_hex(bytes_);
}

bool operator==(const Sha1Digest& lhs, const Sha1Digest& rhs) noexcept
{
    return lhs.bytes_ == rhs.bytes_;
}

bool operator!=(const Sha1Digest& lhs, const Sha1Digest& rhs) noexcept
{
    return !(lhs == rhs);
}

Sha256Digest::Sha256Digest(Bytes bytes) noexcept : bytes_(bytes) {}

Sha256Digest Sha256Digest::from_bytes(Bytes bytes) noexcept
{
    return Sha256Digest(bytes);
}

Result<Sha256Digest> Sha256Digest::from_hex(std::string value)
{
    auto bytes = parse_digest_hex<32>(std::move(value), "hash.sha256");
    if (!bytes)
    {
        const auto message = bytes.error().issues.empty()
                                 ? std::string("contains an invalid SHA-256 digest")
                                 : bytes.error().issues.front().message;
        return Result<Sha256Digest>::failure(validation_error("hash.sha256", message));
    }
    return Result<Sha256Digest>::success(Sha256Digest(std::move(bytes).value()));
}

const Sha256Digest::Bytes& Sha256Digest::bytes() const noexcept
{
    return bytes_;
}

std::string Sha256Digest::to_hex() const
{
    return digest_to_hex(bytes_);
}

bool operator==(const Sha256Digest& lhs, const Sha256Digest& rhs) noexcept
{
    return lhs.bytes_ == rhs.bytes_;
}

bool operator!=(const Sha256Digest& lhs, const Sha256Digest& rhs) noexcept
{
    return !(lhs == rhs);
}

CreateOptions::CreateOptions(TorrentFormat format, PieceLengthStrategy piece_length_strategy,
                             FileOrderPolicy file_order_policy,
                             std::optional<std::uint32_t> fixed_piece_length, bool is_private,
                             TrackerList trackers, std::vector<WebSeedUrl> web_seeds)
    : format_(format), piece_length_strategy_(piece_length_strategy),
      file_order_policy_(file_order_policy), fixed_piece_length_(fixed_piece_length),
      is_private_(is_private), trackers_(std::move(trackers)), web_seeds_(std::move(web_seeds))
{
}

Result<CreateOptions> CreateOptions::create(CreateOptionsInput input)
{
    const bool known_format = input.format == TorrentFormat::V1 ||
                              input.format == TorrentFormat::V2 ||
                              input.format == TorrentFormat::Hybrid;
    if (!known_format)
    {
        return Result<CreateOptions>::failure(
            validation_error("create.format", "must be a supported torrent format"));
    }

    const bool known_strategy = input.piece_length_strategy == PieceLengthStrategy::Auto ||
                                input.piece_length_strategy == PieceLengthStrategy::Fixed;
    if (!known_strategy)
    {
        return Result<CreateOptions>::failure(validation_error(
            "create.piece_length_strategy", "must be an automatic or fixed piece-length strategy"));
    }

    const bool known_file_order_policy =
        input.file_order_policy == FileOrderPolicy::Lexicographical ||
        input.file_order_policy == FileOrderPolicy::CanonicalAlignment ||
        input.file_order_policy == FileOrderPolicy::Natural ||
        input.file_order_policy == FileOrderPolicy::BreadthFirst;
    if (!known_file_order_policy)
    {
        return Result<CreateOptions>::failure(
            validation_error("create.file_order_policy", "must be a supported file order policy"));
    }

    if (input.piece_length_strategy == PieceLengthStrategy::Auto && input.fixed_piece_length)
    {
        return Result<CreateOptions>::failure(validation_error(
            "create.fixed_piece_length", "must be absent for the automatic strategy"));
    }

    if (input.piece_length_strategy == PieceLengthStrategy::Fixed)
    {
        constexpr std::uint32_t kMinimumPieceLength = 16U * 1024U;
        constexpr std::uint32_t kMaximumPieceLength = 16U * 1024U * 1024U;
        if (!input.fixed_piece_length || *input.fixed_piece_length < kMinimumPieceLength ||
            *input.fixed_piece_length > kMaximumPieceLength ||
            (*input.fixed_piece_length & (*input.fixed_piece_length - 1U)) != 0U)
        {
            return Result<CreateOptions>::failure(validation_error(
                "create.fixed_piece_length", "must be a power of two between 16 KiB and 16 MiB"));
        }
    }

    auto trackers = TrackerList::create(std::move(input.tracker_tiers));
    if (!trackers)
    {
        return Result<CreateOptions>::failure(trackers.error());
    }

    return Result<CreateOptions>::success(
        CreateOptions(input.format, input.piece_length_strategy, input.file_order_policy,
                      input.fixed_piece_length, input.is_private, std::move(trackers).value(),
                      std::move(input.web_seeds)));
}

TorrentFormat CreateOptions::format() const noexcept
{
    return format_;
}

PieceLengthStrategy CreateOptions::piece_length_strategy() const noexcept
{
    return piece_length_strategy_;
}

FileOrderPolicy CreateOptions::file_order_policy() const noexcept
{
    return file_order_policy_;
}

const std::optional<std::uint32_t>& CreateOptions::fixed_piece_length() const noexcept
{
    return fixed_piece_length_;
}

bool CreateOptions::is_private() const noexcept
{
    return is_private_;
}

const TrackerList& CreateOptions::trackers() const noexcept
{
    return trackers_;
}

const std::vector<WebSeedUrl>& CreateOptions::web_seeds() const noexcept
{
    return web_seeds_;
}

std::uint32_t CreateOptions::piece_length_for(std::uint64_t regular_payload_size) const noexcept
{
    if (piece_length_strategy_ == PieceLengthStrategy::Fixed && fixed_piece_length_)
    {
        return *fixed_piece_length_;
    }

    // Keep automatic creation aligned with libtorrent's piece_size == 0 policy,
    // which qBittorrent uses.
    constexpr std::array<std::uint64_t, 10> kSizeTable{
        {2684355ULL, 10737418ULL, 42949673ULL, 171798692ULL, 687194767ULL, 2748779069ULL,
         10995116278ULL, 43980465111ULL, 175921860444ULL, 703687441777ULL}};

    std::uint32_t piece_length = 16U * 1024U;
    for (const auto threshold : kSizeTable)
    {
        if (threshold >= regular_payload_size)
        {
            break;
        }
        piece_length *= 2U;
    }
    return piece_length;
}

InfoHashes::InfoHashes(TorrentFormat format, std::optional<Sha1Digest> v1,
                       std::optional<Sha256Digest> v2)
    : format_(format), v1_(v1), v2_(v2)
{
}

Result<InfoHashes> InfoHashes::create(TorrentFormat format, std::optional<Sha1Digest> v1,
                                      std::optional<Sha256Digest> v2)
{
    const bool valid = (format == TorrentFormat::V1 && v1 && !v2) ||
                       (format == TorrentFormat::V2 && !v1 && v2) ||
                       (format == TorrentFormat::Hybrid && v1 && v2);
    if (!valid)
    {
        return Result<InfoHashes>::failure(validation_error(
            "info.hashes", "must contain exactly the hashes required by the torrent format"));
    }
    return Result<InfoHashes>::success(InfoHashes(format, v1, v2));
}

TorrentFormat InfoHashes::format() const noexcept
{
    return format_;
}

const std::optional<Sha1Digest>& InfoHashes::v1() const noexcept
{
    return v1_;
}

const std::optional<Sha256Digest>& InfoHashes::v2() const noexcept
{
    return v2_;
}

LogicalPath::LogicalPath(std::vector<std::string> segments) : segments_(std::move(segments)) {}

Result<LogicalPath> LogicalPath::from_segments(std::vector<std::string> segments)
{
    if (segments.empty())
    {
        return Result<LogicalPath>::failure(
            validation_error("file.path", "must contain at least one relative segment"));
    }
    for (const auto& segment : segments)
    {
        if (segment.empty())
        {
            return Result<LogicalPath>::failure(
                validation_error("file.path", "must not contain empty segments"));
        }
        if (segment == "." || segment == "..")
        {
            return Result<LogicalPath>::failure(
                validation_error("file.path", "must not contain dot segments"));
        }
        if (!is_valid_utf8(segment))
        {
            return Result<LogicalPath>::failure(
                validation_error("file.path", "must contain valid UTF-8 segments"));
        }
        if (segment.find('\0') != std::string::npos || segment.find('/') != std::string::npos ||
            segment.find('\\') != std::string::npos)
        {
            return Result<LogicalPath>::failure(
                validation_error("file.path", "segments must not contain NUL or path separators"));
        }
        if (segment.size() >= 2 && std::isalpha(static_cast<unsigned char>(segment[0])) != 0 &&
            segment[1] == ':')
        {
            return Result<LogicalPath>::failure(
                validation_error("file.path", "must not contain a Windows drive prefix"));
        }
    }
    return Result<LogicalPath>::success(LogicalPath(std::move(segments)));
}

const std::vector<std::string>& LogicalPath::segments() const noexcept
{
    return segments_;
}

std::string LogicalPath::to_string() const
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < segments_.size(); ++index)
    {
        if (index != 0)
        {
            stream << '/';
        }
        stream << segments_[index];
    }
    return stream.str();
}

bool operator==(const LogicalPath& lhs, const LogicalPath& rhs) noexcept
{
    return lhs.segments_ == rhs.segments_;
}

bool operator!=(const LogicalPath& lhs, const LogicalPath& rhs) noexcept
{
    return !(lhs == rhs);
}

TrackerUrl::TrackerUrl(std::string value, std::string comparison_key)
    : value_(std::move(value)), comparison_key_(std::move(comparison_key))
{
}

Result<TrackerUrl> TrackerUrl::parse(std::string value)
{
    auto parsed = parse_url(value, {"http", "https", "udp"}, "tracker.url");
    if (!parsed)
    {
        const auto message = parsed.error().issues.empty() ? std::string("contains an invalid URL")
                                                           : parsed.error().issues.front().message;
        return Result<TrackerUrl>::failure(validation_error("tracker.url", message));
    }
    return Result<TrackerUrl>::success(
        TrackerUrl(std::move(value), std::move(parsed).value().comparison_key));
}

const std::string& TrackerUrl::value() const noexcept
{
    return value_;
}

const std::string& TrackerUrl::comparison_key() const noexcept
{
    return comparison_key_;
}

bool operator==(const TrackerUrl& lhs, const TrackerUrl& rhs) noexcept
{
    return lhs.value_ == rhs.value_;
}

bool operator!=(const TrackerUrl& lhs, const TrackerUrl& rhs) noexcept
{
    return !(lhs == rhs);
}

TrackerTier::TrackerTier(std::vector<TrackerUrl> trackers) : trackers_(std::move(trackers)) {}

Result<TrackerTier> TrackerTier::create(std::vector<TrackerUrl> trackers)
{
    if (trackers.empty())
    {
        return Result<TrackerTier>::failure(
            validation_error("tracker.tier", "must contain at least one tracker"));
    }

    std::unordered_set<std::string> seen;
    std::vector<TrackerUrl> unique;
    unique.reserve(trackers.size());
    for (auto& tracker : trackers)
    {
        if (seen.insert(tracker.comparison_key()).second)
        {
            unique.push_back(std::move(tracker));
        }
    }
    return Result<TrackerTier>::success(TrackerTier(std::move(unique)));
}

const std::vector<TrackerUrl>& TrackerTier::trackers() const noexcept
{
    return trackers_;
}

TrackerList::TrackerList(std::vector<TrackerTier> tiers) : tiers_(std::move(tiers)) {}

Result<TrackerList> TrackerList::create(std::vector<TrackerTier> tiers)
{
    return Result<TrackerList>::success(TrackerList(std::move(tiers)));
}

const std::vector<TrackerTier>& TrackerList::tiers() const noexcept
{
    return tiers_;
}

WebSeedUrl::WebSeedUrl(std::string value) : value_(std::move(value)) {}

Result<WebSeedUrl> WebSeedUrl::parse(std::string value)
{
    auto parsed = parse_url(value, {"http", "https"}, "metadata.web_seed");
    if (!parsed)
    {
        const auto message = parsed.error().issues.empty() ? std::string("contains an invalid URL")
                                                           : parsed.error().issues.front().message;
        return Result<WebSeedUrl>::failure(validation_error("metadata.web_seed", message));
    }
    return Result<WebSeedUrl>::success(WebSeedUrl(std::move(value)));
}

const std::string& WebSeedUrl::value() const noexcept
{
    return value_;
}

DhtNode::DhtNode(std::string host, std::uint16_t port) : host_(std::move(host)), port_(port) {}

Result<DhtNode> DhtNode::create(std::string host, std::uint32_t port)
{
    if (host.empty() || !is_valid_utf8(host) || contains_uri_whitespace_or_control(host))
    {
        return Result<DhtNode>::failure(
            validation_error("metadata.dht_node.host", "must be a non-empty valid host"));
    }
    if (port == 0 || port > 65535)
    {
        return Result<DhtNode>::failure(
            validation_error("metadata.dht_node.port", "must be between 1 and 65535"));
    }
    return Result<DhtNode>::success(DhtNode(std::move(host), static_cast<std::uint16_t>(port)));
}

const std::string& DhtNode::host() const noexcept
{
    return host_;
}

std::uint16_t DhtNode::port() const noexcept
{
    return port_;
}

TorrentMetadata::TorrentMetadata(TorrentMetadataInput input) : data_(std::move(input)) {}

Result<TorrentMetadata> TorrentMetadata::create(TorrentMetadataInput input)
{
    const auto validate_optional_text = [](const std::optional<std::string>& text,
                                           const std::string& field) -> std::optional<Error> {
        if (text && !is_valid_utf8(*text))
        {
            return validation_error(field, "must contain valid UTF-8");
        }
        return std::nullopt;
    };

    if (auto error = validate_optional_text(input.comment, "metadata.comment"))
    {
        return Result<TorrentMetadata>::failure(std::move(*error));
    }
    if (auto error = validate_optional_text(input.creator, "metadata.creator"))
    {
        return Result<TorrentMetadata>::failure(std::move(*error));
    }
    if (auto error = validate_optional_text(input.source, "metadata.source"))
    {
        return Result<TorrentMetadata>::failure(std::move(*error));
    }
    for (const auto& collection : input.collections)
    {
        if (collection.empty() || !is_valid_utf8(collection))
        {
            return Result<TorrentMetadata>::failure(validation_error(
                "metadata.collection", "must contain non-empty valid UTF-8 values"));
        }
    }
    return Result<TorrentMetadata>::success(TorrentMetadata(std::move(input)));
}

const std::optional<std::string>& TorrentMetadata::comment() const noexcept
{
    return data_.comment;
}

const std::optional<std::string>& TorrentMetadata::creator() const noexcept
{
    return data_.creator;
}

const std::optional<std::string>& TorrentMetadata::source() const noexcept
{
    return data_.source;
}

const std::optional<std::int64_t>& TorrentMetadata::creation_time_unix_seconds() const noexcept
{
    return data_.creation_time_unix_seconds;
}

const std::vector<WebSeedUrl>& TorrentMetadata::web_seeds() const noexcept
{
    return data_.web_seeds;
}

const std::vector<std::string>& TorrentMetadata::collections() const noexcept
{
    return data_.collections;
}

const std::vector<DhtNode>& TorrentMetadata::dht_nodes() const noexcept
{
    return data_.dht_nodes;
}

FileEntry::FileEntry(LogicalPath path, std::uint64_t length, FileAttributes attributes,
                     std::optional<Sha256Digest> pieces_root, std::optional<Sha1Digest> sha1_hint,
                     std::optional<LogicalPath> symlink_target)
    : path_(std::move(path)), length_(length), attributes_(attributes), pieces_root_(pieces_root),
      sha1_hint_(sha1_hint), symlink_target_(std::move(symlink_target))
{
}

Result<FileEntry> FileEntry::create(LogicalPath path, std::uint64_t length,
                                    FileAttributes attributes,
                                    std::optional<Sha256Digest> pieces_root,
                                    std::optional<Sha1Digest> sha1_hint,
                                    std::optional<LogicalPath> symlink_target)
{
    if (attributes.symlink && length != 0U)
    {
        return Result<FileEntry>::failure(
            validation_error("file.length", "BEP 47 symlink entries must have zero length"));
    }
    if (attributes.symlink && !symlink_target)
    {
        return Result<FileEntry>::failure(
            validation_error("file.symlink_target", "BEP 47 symlink entries require a target"));
    }
    if (attributes.symlink && pieces_root)
    {
        return Result<FileEntry>::failure(validation_error(
            "file.pieces_root", "BEP 47 symlink entries must not have a V2 pieces root"));
    }
    if (!attributes.symlink && symlink_target)
    {
        return Result<FileEntry>::failure(validation_error(
            "file.symlink_target", "only BEP 47 symlink entries may have a target"));
    }

    return Result<FileEntry>::success(FileEntry(std::move(path), length, attributes, pieces_root,
                                                sha1_hint, std::move(symlink_target)));
}

const LogicalPath& FileEntry::path() const noexcept
{
    return path_;
}

std::uint64_t FileEntry::length() const noexcept
{
    return length_;
}

const FileAttributes& FileEntry::attributes() const noexcept
{
    return attributes_;
}

const std::optional<Sha256Digest>& FileEntry::pieces_root() const noexcept
{
    return pieces_root_;
}

const std::optional<Sha1Digest>& FileEntry::sha1_hint() const noexcept
{
    return sha1_hint_;
}

const std::optional<LogicalPath>& FileEntry::symlink_target() const noexcept
{
    return symlink_target_;
}

PieceInfo::PieceInfo(TorrentFormat format, std::uint64_t piece_length, std::uint64_t total_size,
                     std::vector<Sha1Digest> v1_piece_hashes)
    : format_(format), piece_length_(piece_length), total_size_(total_size),
      v1_piece_hashes_(std::move(v1_piece_hashes))
{
}

Result<PieceInfo> PieceInfo::create(TorrentFormat format, std::uint64_t piece_length,
                                    std::uint64_t total_size,
                                    std::vector<Sha1Digest> v1_piece_hashes)
{
    if (!is_known_format(format))
    {
        return Result<PieceInfo>::failure(
            validation_error("info.format", "must be V1, V2, or Hybrid"));
    }
    if (!is_power_of_two(piece_length))
    {
        return Result<PieceInfo>::failure(
            validation_error("info.piece_length", "must be a non-zero power of two"));
    }

    constexpr std::uint64_t kV2MinimumPieceLength = std::uint64_t{16} * 1024U;
    if (format != TorrentFormat::V1 && piece_length < kV2MinimumPieceLength)
    {
        return Result<PieceInfo>::failure(validation_error(
            "info.piece_length", "v2 and hybrid torrents require at least 16 KiB"));
    }

    const auto expected_v1_pieces =
        total_size / piece_length + (total_size % piece_length == 0 ? 0 : 1);
    if (format == TorrentFormat::V2 && !v1_piece_hashes.empty())
    {
        return Result<PieceInfo>::failure(
            validation_error("info.pieces", "v2-only torrents must not contain v1 piece hashes"));
    }
    if (format != TorrentFormat::V2 && v1_piece_hashes.size() != expected_v1_pieces)
    {
        return Result<PieceInfo>::failure(validation_error(
            "info.pieces", "v1 piece hash count does not match total size and piece length"));
    }
    return Result<PieceInfo>::success(
        PieceInfo(format, piece_length, total_size, std::move(v1_piece_hashes)));
}

TorrentFormat PieceInfo::format() const noexcept
{
    return format_;
}

std::uint64_t PieceInfo::piece_length() const noexcept
{
    return piece_length_;
}

std::uint64_t PieceInfo::total_size() const noexcept
{
    return total_size_;
}

const std::vector<Sha1Digest>& PieceInfo::v1_piece_hashes() const noexcept
{
    return v1_piece_hashes_;
}

TorrentInfo::TorrentInfo(std::string name, TorrentFormat format, InfoHashes info_hashes,
                         PieceInfo pieces, std::vector<FileEntry> files, bool is_private)
    : name_(std::move(name)), format_(format), info_hashes_(info_hashes),
      pieces_(std::move(pieces)), files_(std::move(files)), is_private_(is_private)
{
}

Result<TorrentInfo> TorrentInfo::create(std::string name, TorrentFormat format,
                                        InfoHashes info_hashes, PieceInfo pieces,
                                        std::vector<FileEntry> files, bool is_private)
{
    if (name.empty() || !is_valid_utf8(name))
    {
        return Result<TorrentInfo>::failure(
            validation_error("info.name", "must contain non-empty valid UTF-8"));
    }
    if (files.empty())
    {
        return Result<TorrentInfo>::failure(
            validation_error("info.files", "must contain at least one file"));
    }
    if (info_hashes.format() != format || pieces.format() != format)
    {
        return Result<TorrentInfo>::failure(
            validation_error("info.format", "must match the info hash and piece layout formats"));
    }

    std::uint64_t total_size = 0;
    std::unordered_set<std::string> paths;
    for (const auto& file : files)
    {
        if (!file.attributes().padding && !paths.insert(file.path().to_string()).second)
        {
            return Result<TorrentInfo>::failure(validation_error(
                "info.files", "non-padding files must not contain duplicate logical paths"));
        }
        if (file.length() > std::numeric_limits<std::uint64_t>::max() - total_size)
        {
            return Result<TorrentInfo>::failure(
                validation_error("info.files", "total file length overflows uint64"));
        }
        total_size += file.length();

        const bool is_hybrid_padding = format == TorrentFormat::Hybrid && file.attributes().padding;
        const bool needs_v2_root =
            format != TorrentFormat::V1 && !is_hybrid_padding && file.length() > 0;
        if (needs_v2_root != file.pieces_root().has_value())
        {
            return Result<TorrentInfo>::failure(validation_error(
                "info.files", "piece roots must match the torrent format and file length"));
        }
    }
    if (total_size != pieces.total_size())
    {
        return Result<TorrentInfo>::failure(
            validation_error("info.pieces", "total size must equal the sum of file lengths"));
    }

    return Result<TorrentInfo>::success(TorrentInfo(
        std::move(name), format, info_hashes, std::move(pieces), std::move(files), is_private));
}

const std::string& TorrentInfo::name() const noexcept
{
    return name_;
}

TorrentFormat TorrentInfo::format() const noexcept
{
    return format_;
}

const InfoHashes& TorrentInfo::info_hashes() const noexcept
{
    return info_hashes_;
}

const PieceInfo& TorrentInfo::pieces() const noexcept
{
    return pieces_;
}

const std::vector<FileEntry>& TorrentInfo::files() const noexcept
{
    return files_;
}

bool TorrentInfo::is_private() const noexcept
{
    return is_private_;
}

struct TorrentDocument::Details
{
    TorrentInfo info;
    TorrentMetadata metadata;
    TrackerList trackers;
    std::vector<DocumentWarning> warnings;
    bool has_retained_extensions{};
    std::shared_ptr<const detail::RetainedDocumentState> retained;
    std::vector<MetadataFieldInfo> metadata_fields;
};

TorrentDocument::TorrentDocument(std::shared_ptr<const Details> details)
    : details_(std::move(details))
{
}

TorrentDocument::TorrentDocument(const TorrentDocument&) noexcept = default;
TorrentDocument::TorrentDocument(TorrentDocument&&) noexcept = default;
TorrentDocument& TorrentDocument::operator=(const TorrentDocument&) noexcept = default;
TorrentDocument& TorrentDocument::operator=(TorrentDocument&&) noexcept = default;
TorrentDocument::~TorrentDocument() = default;

Result<TorrentDocument> TorrentDocument::create(TorrentInfo info, TorrentMetadata metadata,
                                                TrackerList trackers,
                                                std::vector<DocumentWarning> warnings)
{
    for (const auto& warning : warnings)
    {
        if (warning.field.empty() || !is_valid_utf8(warning.field) || warning.message.empty() ||
            !is_valid_utf8(warning.message))
        {
            return Result<TorrentDocument>::failure(validation_error(
                "document.warning", "field and message must be non-empty valid UTF-8"));
        }
    }

    auto details = std::make_shared<Details>(Details{std::move(info),
                                                     std::move(metadata),
                                                     std::move(trackers),
                                                     std::move(warnings),
                                                     false,
                                                     {},
                                                     {}});
    return Result<TorrentDocument>::success(TorrentDocument(std::move(details)));
}

const TorrentInfo& TorrentDocument::info() const noexcept
{
    return details_->info;
}

const TorrentMetadata& TorrentDocument::metadata() const noexcept
{
    return details_->metadata;
}

const TrackerList& TorrentDocument::trackers() const noexcept
{
    return details_->trackers;
}

const std::vector<DocumentWarning>& TorrentDocument::warnings() const noexcept
{
    return details_->warnings;
}

bool TorrentDocument::has_retained_extensions() const noexcept
{
    return details_->has_retained_extensions;
}

const std::vector<MetadataFieldInfo>& TorrentDocument::metadata_fields() const noexcept
{
    return details_->metadata_fields;
}

Result<TorrentDocument> detail::make_retained_document(
    TorrentInfo info, TorrentMetadata metadata, TrackerList trackers,
    std::vector<DocumentWarning> warnings, std::vector<MetadataFieldInfo> fields,
    std::shared_ptr<const RetainedDocumentState> retained, const bool has_extensions)
{
    auto document = TorrentDocument::create(std::move(info), std::move(metadata),
                                            std::move(trackers), std::move(warnings));
    if (!document)
    {
        return document;
    }
    auto details = std::make_shared<TorrentDocument::Details>(*document.value().details_);
    details->retained = std::move(retained);
    details->has_retained_extensions = has_extensions;
    details->metadata_fields = std::move(fields);
    return Result<TorrentDocument>::success(TorrentDocument(std::move(details)));
}

const detail::RetainedDocumentState*
detail::retained_state(const TorrentDocument& document) noexcept
{
    return document.details_->retained.get();
}

Result<TorrentDocument> TorrentDocument::with_metadata(TorrentMetadata metadata) const
{
    auto details = std::make_shared<Details>(*details_);
    details->metadata = std::move(metadata);
    return Result<TorrentDocument>::success(TorrentDocument(std::move(details)));
}

Result<TorrentDocument> TorrentDocument::with_trackers(TrackerList trackers) const
{
    auto details = std::make_shared<Details>(*details_);
    details->trackers = std::move(trackers);
    return Result<TorrentDocument>::success(TorrentDocument(std::move(details)));
}

} // namespace torrentutils::core
