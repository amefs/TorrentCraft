#include "torrent_engine_fault_injection.hpp"

namespace torrentutils::core::detail {
namespace {

thread_local TorrentEngineFault active_fault{TorrentEngineFault::None};

} // namespace

ScopedTorrentEngineFault::ScopedTorrentEngineFault(TorrentEngineFault fault) noexcept
    : previous_(active_fault)
{
    active_fault = fault;
}

ScopedTorrentEngineFault::~ScopedTorrentEngineFault()
{
    active_fault = previous_;
}

bool torrent_engine_fault_is_active(TorrentEngineFault fault) noexcept
{
    return active_fault == fault;
}

} // namespace torrentutils::core::detail
