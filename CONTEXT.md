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

**Configuration discovery** checks, in order: explicit `--config PATH`, the
current directory's `torrentcraft.json`, `torrentcraft.json` beside the running
executable, and the user configuration directory. A selected file that is malformed
causes an error rather than silently falling back to another file.

**Named preset** is a reusable entry under \`presets.<name>\`, selected with
\`--preset NAME\`.

**Overlay merge** applies settings in the order explicit CLI arguments, an explicit
preset or the configured default preset, configuration defaults, and Core defaults.
An explicit preset replaces the default-preset fallback rather than merging with it;
a higher layer replaces a complete value at the same key.

**Canonical configuration path** is the platform user configuration location
shared by CLI and GUI. The current working directory is only a discovery fallback,
not a second persistent configuration store.

**Qt-free CLI** is the independent command-line artifact. It does not include or
link Qt and uses the same configuration frontend as the GUI.

## Release distribution

**Flat executable asset** is a Linux or Windows CLI/GUI binary published directly
on the GitHub Releases page. It is the only release material intentionally kept
outside an archive for direct download.

**Platform support bundle** is the platform-specific archive containing release
metadata, compliance and relinking materials, test results, source artifacts, and
the SHA-256 manifest for the flat executables and bundle contents.

**Release SHA-256 manifest** is the `SHA256SUMS` file inside a platform support
bundle. It verifies the matching flat executables and the files inside that bundle;
the outer bundle is not listed because that would create a self-referential hash.

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

## Cancellation and hashing

**Cancellation request** is the control-thread action that asks a running operation
to stop. It is an intent signal; the worker remains busy until it reaches a safe
cancellation checkpoint and returns cancellation completion.

**Safe cancellation checkpoint** is a bounded point at which an operation may inspect
its token and return without corrupting backend state or leaving an owned temporary
output. File I/O uses bounded chunks where supported, while libtorrent creation
checks cancellation at piece callbacks.

**Adaptive hashing disk I/O** is the create/verify policy where an absent mode uses
libtorrent's platform default, explicit POSIX remains opt-in, and explicit mmap falls
back to the platform default when unavailable. A live cancellation token does not
force the slower POSIX backend.

**Commit linearization point** is the instant atomic target replacement begins.
Cancellation wins before that point; after it, the replacement commits and the
operation reports success.

## File filtering

**File filter mode**:
The explicit creation policy `disabled`, `common-artifacts`, or `custom-rules`; absent configuration
resolves to `disabled`, and custom rules replace rather than extend the common artifact set.
_Avoid_: implicit hidden-file filtering, merged custom/common rules

**File filter Glob**:
The shared relative-path matcher supporting `*`, `?`, `**`, basename-at-any-depth patterns, and
trailing-slash directory-only rules. It is intentionally not gitignore or rsync syntax.
_Avoid_: gitignore compatibility, rsync filter

**Filter report**:
The Core-owned list of excluded relative entries, matched rules, entry kinds, and byte summaries
returned by both create and create-plan operations; matched directories aggregate descendants.
_Avoid_: source deletion, GUI-only exclusion list, per-descendant expansion

**Global creation filter default**:
The complete `defaults.file_filter` policy edited on Advanced and used when a selected preset
has no explicit filter object.
_Avoid_: treating Advanced as the selected preset, independent field defaults

**Effective creation filter**:
The complete filter policy shown on Create after global defaults, preset replacement, and any
CLI field-level overrides have been resolved.
_Avoid_: showing raw defaults as the Create value

**Atomic preset filter override**:
A preset `file_filter` object that replaces the complete global filter policy; an absent object
inherits the global policy, while `mode: disabled` explicitly disables it.
_Avoid_: field-by-field preset merging, implicit disablement

**CLI file-filter override**:
A field-level command-line adjustment applied after config and preset resolution, preserving
unspecified filter fields.
_Avoid_: treating one CLI flag as a complete filter replacement
