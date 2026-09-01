#pragma once

namespace torrentutils::core {

/** BitTorrent metadata format represented by a document or requested operation. */
enum class TorrentFormat
{
    V1,
    V2,
    Hybrid
};

} // namespace torrentutils::core
