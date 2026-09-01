#pragma once

namespace torrentutils::core::detail {

enum class TorrentEngineFault
{
    None,
    CreateMetadataEncoding,
    VerifyBackendIo,
    ReadSymlinkAccessDenied,
    InspectBackendInitialization,
    InspectRestrictedCapabilities,
    VerifyProgressPublisherConstruction
};

/** Scoped, thread-local fault injection for private Torrent Engine tests. */
class ScopedTorrentEngineFault
{
  public:
    explicit ScopedTorrentEngineFault(TorrentEngineFault fault) noexcept;
    ~ScopedTorrentEngineFault();

    ScopedTorrentEngineFault(const ScopedTorrentEngineFault&) = delete;
    ScopedTorrentEngineFault& operator=(const ScopedTorrentEngineFault&) = delete;

  private:
    TorrentEngineFault previous_;
};

[[nodiscard]] bool torrent_engine_fault_is_active(TorrentEngineFault fault) noexcept;

} // namespace torrentutils::core::detail
