# Changelog

All notable user-visible changes are recorded here.

## [1.0.0]

### Added

- First stable TorrentCraft release with a Qt-free CLI and Qt Widgets GUI.
- Linux musl x86_64 and Windows x86_64 static release packages.
- Shared CLI/GUI \`torrentcraft.json\` configuration discovery and named presets.
- Torrent creation, inspection, validation, verification, metadata, tracker, and
  file-order workflows.
- System font and icon integration, local file drag-and-drop, and English/Simplified
  Chinese UI translations.
- Checksums, provenance, SPDX SBOMs, security reports, and Qt LGPL relinking materials.

### Compatibility

- Core public headers do not expose libtorrent, Boost, Qt, JSON, or bencode implementation types.
- Expected failures are returned through \`Result<T>\`; public operations do not use exceptions
  for normal validation and I/O outcomes.
- The installed CMake package exports \`TorrentUtils::Foundation\`, \`TorrentUtils::Domain\`,
  \`TorrentUtils::Core\`, and \`TorrentUtils::Frontend\`.
