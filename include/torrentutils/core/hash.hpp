#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <torrentutils/core/result.hpp>
#include <torrentutils/core/torrent_format.hpp>

namespace torrentutils::core {

/** Strong 20-byte SHA-1 digest used by BitTorrent v1 structures. */
class Sha1Digest
{
  public:
    using Bytes = std::array<std::uint8_t, 20>;

    [[nodiscard]] static Sha1Digest from_bytes(Bytes bytes) noexcept;
    [[nodiscard]] static Result<Sha1Digest> from_hex(std::string value);

    [[nodiscard]] const Bytes& bytes() const noexcept;
    [[nodiscard]] std::string to_hex() const;

    friend bool operator==(const Sha1Digest& lhs, const Sha1Digest& rhs) noexcept;
    friend bool operator!=(const Sha1Digest& lhs, const Sha1Digest& rhs) noexcept;

  private:
    explicit Sha1Digest(Bytes bytes) noexcept;

    Bytes bytes_{};
};

/** Strong 32-byte SHA-256 digest used by BitTorrent v2 structures. */
class Sha256Digest
{
  public:
    using Bytes = std::array<std::uint8_t, 32>;

    [[nodiscard]] static Sha256Digest from_bytes(Bytes bytes) noexcept;
    [[nodiscard]] static Result<Sha256Digest> from_hex(std::string value);

    [[nodiscard]] const Bytes& bytes() const noexcept;
    [[nodiscard]] std::string to_hex() const;

    friend bool operator==(const Sha256Digest& lhs, const Sha256Digest& rhs) noexcept;
    friend bool operator!=(const Sha256Digest& lhs, const Sha256Digest& rhs) noexcept;

  private:
    explicit Sha256Digest(Bytes bytes) noexcept;

    Bytes bytes_{};
};

/** Format-compatible set of info hashes for one torrent document. */
class InfoHashes
{
  public:
    [[nodiscard]] static Result<InfoHashes>
    create(TorrentFormat format, std::optional<Sha1Digest> v1, std::optional<Sha256Digest> v2);

    [[nodiscard]] TorrentFormat format() const noexcept;
    [[nodiscard]] const std::optional<Sha1Digest>& v1() const noexcept;
    [[nodiscard]] const std::optional<Sha256Digest>& v2() const noexcept;

  private:
    InfoHashes(TorrentFormat format, std::optional<Sha1Digest> v1, std::optional<Sha256Digest> v2);

    TorrentFormat format_;
    std::optional<Sha1Digest> v1_;
    std::optional<Sha256Digest> v2_;
};

} // namespace torrentutils::core
