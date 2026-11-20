// libFuzzer target: bencode parsing and torrent metadata decoding (src/bencode_adapter.cpp,
// src/metadata_engine.cpp). Any input must decode or be rejected without a crash, a sanitizer
// report or a leak.
#include "metadata_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    using torrentutils::core::detail::decode_torrent;
    using torrentutils::core::detail::MetadataReadMode;
    const std::vector<std::uint8_t> bytes(data, data + size);
    static_cast<void>(decode_torrent(bytes, MetadataReadMode::Lenient));
    static_cast<void>(decode_torrent(bytes, MetadataReadMode::Strict));
    return 0;
}
