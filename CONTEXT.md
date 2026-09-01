# Public project vocabulary

This document records stable terms used by the public API and user-facing tools.
It is a compact reference for contributors and downstream users, not a development
roadmap or a record of private implementation decisions.

## Core model

**Foundation** is the installed CMake target that provides cancellation, result,
error, progress, and logging types without third-party domain dependencies.

**Domain** is the immutable, validated model of torrent formats, hashes, logical
paths, file layouts, trackers, metadata, and torrent documents.

**Torrent Engine** creates torrent files and verifies content. It owns backend
integration without exposing backend types through the SDK.

**Metadata Engine** loads and edits a torrent document while preserving supported
wire data and rejecting ambiguous or unsafe input.

**Inspection** is a read-only operation over a loaded torrent document. It reports
format capabilities and diagnostics; it does not verify content.

**Capability assessment** states whether the configured engine can perform a
verification operation. It is distinct from loading and from the final verification
outcome.

**Controlled Edit** is a validated application action for supported metadata,
tracker, web-seed, DHT-node, or identity changes. It cannot silently alter
content-defining info fields.

**Rebuild Request** describes a content-affecting or unmodeled info change that
requires reconstructing the torrent instead of applying an in-place edit.

**Result<T>** contains exactly one success value or one error. A successful value
may be accessed only as a success result, and an error may be accessed only as a
failure result.

## Torrent semantics

**Torrent format** is one of V1, V2, or Hybrid. Reading reports the actual input
format; creation uses the requested format and does not silently convert it.

**Logical relative path** contains validated torrent-relative path segments. It
cannot contain an absolute root, drive prefix, UNC prefix, empty segment, dot,
dot-dot, or NUL.

**Piece** is the format-level cryptographic verification unit. It is not a backend
request block.

**File order policy** is the explicit creation choice controlling logical file
order, piece boundaries, padding, and resulting info hashes.

**Lexicographical** is the deterministic default policy using normalized byte order.

**Natural** compares digit runs numerically with a case-insensitive comparison and
matches the default ordering used by qBittorrent's creator.

**Breadth first** orders paths by directory depth, then by deterministic
lexicographical order.

**Hybrid padding file** participates in the V1-aligned layout of a Hybrid torrent
but is not part of the V2 file tree.

**BEP 47 symlink entry** is a zero-length logical file with the \`l\` attribute and
a validated target relative to the torrent root. Verification checks the link and
declared target without following it outside the content root.

**Tracker raw URL** preserves the user's validated URL text for display and writing.
A private comparison key is used only for same-tier deduplication.

## Frontend configuration

**torrentcraft configuration** is the shared canonical JSON file
\`torrentcraft.json\` used by CLI and GUI. It maps to typed create requests and
does not become Core data.

**Configuration discovery** checks, in order: explicit \`--config PATH\`, the
current directory's \`torrentcraft.json\`, and the user configuration directory.
A selected file that is malformed causes an error rather than silently falling
back to another file.

**Named preset** is a reusable entry under \`presets.<name>\`, selected with
\`--preset NAME\`.

**Overlay merge** applies settings in the order CLI arguments, named preset,
configuration defaults, and Core defaults. A higher layer replaces a complete
value at the same key.

**Canonical configuration path** is the platform user configuration location
shared by CLI and GUI. The current working directory is only a discovery fallback,
not a second persistent configuration store.

**Qt-free CLI** is the independent command-line artifact. It does not include or
link Qt and uses the same configuration frontend as the GUI.

## Verification

**Verification outcome** is \`Verified\`, \`Mismatched\`, or \`Incomplete\`, with
file- and piece-level evidence. It is a successful result value, not an SDK error.

**Recoverable verification discrepancy** is a missing, short, or mismatched content
condition that verification can report while continuing.

**Unrecoverable verification I/O failure** prevents reliable continuation and is
returned as an SDK error.

**Verification progress** is synchronous work information delivered while the
verify call is active. It is separate from the completed verification report.

**Piece range** is a half-open \`[begin, end)\` range of completed pieces emitted
for transient progress display. It is not a retained report of every piece.

**Shared piece mismatch** is a mismatched piece overlapping multiple logical files.
Each affected file records its overlap; no single file is assigned exclusive blame.
