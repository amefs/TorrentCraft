#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <torrentutils/core/torrent_engine.hpp>

namespace torrentutils::core::detail {

struct VerificationBackendCapabilities
{
    bool verification{true};
    bool v1_format{true};
    bool v2_format{true};
    bool hybrid_format{true};
    bool file_attributes{true};
    bool bep47_symlinks{true};
    std::uint64_t max_file_size{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t max_file_offset{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t max_file_count{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t max_piece_size{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t max_piece_count{std::numeric_limits<std::uint64_t>::max()};
};

using VerificationBackendCapabilitiesProvider =
    std::function<Result<VerificationBackendCapabilities>()>;

[[nodiscard]] Result<VerificationBackendCapabilities>
libtorrent_verification_backend_capabilities();

[[nodiscard]] Result<InspectionReport>
inspect_verification_capability(const TorrentDocument& document,
                                const VerificationBackendCapabilitiesProvider& provider);

} // namespace torrentutils::core::detail
