# Changelog

All notable user-visible changes are recorded here.

## [1.2.0] - 2026-11-20

### Added

- Import a named creation preset from an existing torrent: choose which reusable settings
  (trackers, piece size, private flag, info source, web seeds, format, and optionally comment or
  creator) to adopt through GUI **Import From File** or `preset add --from-torrent`. Torrent-specific
  metadata such as name, files, hashes and creation date is never imported. Non-standard metadata
  and settings the torrent lacks are skipped with a warning instead of rejecting the torrent.
- Rewrite tracker URLs with ordered sed-style rules (`s#old#new#g`, RE2 syntax), keeping each
  torrent's passkey when a tracker changes domain: `tracker rewrite <torrent|folder>... -e RULE`
  with `--recursive`, `--output-dir`, `--force`, `--dry-run` and `--json`. The GUI Tracker page
  has a **Rewrite (find and replace)** mode with editor-style Find/Replace rows (plain text by
  default, optional regular expressions with `$1` groups, Match case), a live per-URL preview and
  **Preview all**. Invalid results fail only the affected torrent.
- The GUI Tracker page can include subfolders, mirroring them in the output folder.
- CLI: Ctrl+C stops `create`, `verify` and `tracker rewrite` cleanly, removing temporary output
  and exiting with code 7; a second Ctrl+C exits immediately. An interrupted `tracker rewrite`
  keeps the files already edited and reports `"cancelled": true` in `--json` output.
  `torrentcraft-gui.exe` running a CLI command cannot receive Ctrl+C because the shell does not
  wait for it; it now says to press Ctrl+Break instead, or to use `torrentcraft.exe`.
- GUI: every page (Create, Modify, Inspect, Verify, Tracker) has a **Reset** button that clears the
  page and restores its initial values; Create keeps the selected preset and reapplies it.

### Fixed

- Verify follows the file layout recorded in the torrent instead of the number of payload files: a
  directory torrent holding one file expects a folder, and a content path of the wrong type is
  rejected with `verify.content_root` (exit code 3) instead of being mapped incorrectly.
- Verify follows a final symlink on a normal file entry and checks its target, as qBittorrent,
  libtorrent and Transmission do; a dangling link is reported as *Missing*. Trailing separators on
  the content path are ignored, which fixes a crash for single-file torrents, and the filesystem
  root is rejected as a content path.
- GUI: the Inspect and Verify file trees show a directory torrent's root folder as the top-level
  node, matching `torrentcraft tree`.
- Linux GUI: a launch without arguments always requires a reachable X11 or XWayland display, because
  the release build ships only Qt's XCB backend; `WAYLAND_DISPLAY` alone no longer counts, and the
  GUI shows CLI usage with an explanatory note instead of failing inside Qt.
- GUI: Reset buttons and the Tracker source, recursion and mode controls are disabled while a task
  runs, so they can no longer clear or change data a running task is using. Closing the window waits
  for the task to stop before releasing its data.
- GUI: **Convert** and **Preview all** use rewrite rules edited a moment earlier instead of the
  previous rules. The Tracker torrent list follows language changes, no longer keeps old lists in
  memory, and the batch summary lists at most 50 failed files.
- GUI: a failed **Apply** on the Advanced page no longer leaves partial settings that a later save
  would write. Log rotation works in folders with non-ASCII names on Windows.
- GUI: the Modify and Inspect option **Reject leniently loaded torrents** is now labelled **Strict**
  (matching `--strict`, with the full text as a tooltip), so the Modify page no longer scrolls
  sideways at the minimum window width.
- Config edits are size-checked against the indented file actually written, so a config close to
  the 1 MiB limit can no longer be saved in a form that later fails to load. A rejected preset no
  longer leaves an empty `presets` section behind.
- CLI: `--json` output no longer aborts with an internal error when a path is not valid UTF-8
  (Linux); invalid bytes are replaced. Failed create/verify runs no longer print 100% progress.
- Tracker rewrite: a symlinked torrent edited in place now fails in `--dry-run` and the GUI preview,
  as the real run does. An over-long tracker URL is reported invalid without running the rules on
  it. A failed save no longer leaves a partial `.bak-*` backup.

### Verification and release policy

- The GitHub Actions `v1.2.0` tag workflow publishes the release after the release contract,
  static Linux/Windows builds, tests, JUnit validation, SBOM generation, security audit, packaging,
  and checksum validation pass.
- The source tree adds compile-database clang-tidy, fatal UBSan, ThreadSanitizer, and libFuzzer
  presets. GitHub Actions keeps its lean pull-request matrix; the sanitizer and fuzzer presets
  remain available for local and release validation.
- Binary signing remains deferred for 1.2.0. SHA-256, provenance, SBOM, security, and LGPL
  sidecars remain part of the release evidence.

## [1.1.2] - 2026-10-02

### Fixed

- Fall back to CLI usage when the GUI cannot connect to a display, allowing one binary to serve
  both graphical and headless environments.
- Scale Create/Modify multiline editors with the active font, preserve their useful minimum sizes,
  and allow constrained pages to scroll without compressing controls.

### Verification and release policy

- The GitHub Actions `v1.1.2` tag workflow publishes the release after static Linux/Windows builds,
  tests, SBOM generation, security audit, and checksum validation pass.
- Binary signing remains deferred for 1.1.2. SHA-256, provenance, SBOM, security, and LGPL
  sidecars remain part of the release evidence.

## [1.1.1] - 2026-09-07

### Fixed

- Restore libtorrent default/mmap hashing for cancellable create operations instead of forcing the slower POSIX backend.
- Add bounded cooperative cancellation for GUI long-running operations while preserving output atomicity and piece-boundary cancellation semantics.
- Clean up interrupted libtorrent 2.0.14 mmap jobs, including completion jobs left queued after an interrupted callback.
- Keep explicit `disk_io` modes and safe mmap fallback behavior consistent across create and verify.

### Verification and release policy

- The GitHub Actions `v1.1.1` tag workflow publishes the release after build, install-consumer checks, SBOM generation, security audit, and checksum validation pass.
- Binary signing remains deferred for 1.1.1. SHA-256, provenance, SBOM, security, and LGPL sidecars remain part of the release evidence.

## [1.1.0] - 2026-09-05

### Added

- Opt-in directory file filtering for common operating-system artifacts or custom Glob rules.
- Shared file-filter defaults, atomic preset overrides, case-sensitivity controls, and CLI/GUI
  filter reports for create and dry-run operations.

### Verification and release policy

- The GitHub Actions `v1.1.0` tag workflow publishes the release after the build, install-consumer checks, SBOM generation, security audit, and checksum validation pass.
- Binary signing remains deferred for 1.1.0. SHA-256, provenance, SBOM, security, and LGPL
  sidecars remain part of the release evidence.

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
