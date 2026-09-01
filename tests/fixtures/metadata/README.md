# Metadata Engine golden fixtures

This directory contains deterministic, versioned byte fixtures. Files ending in `.torrent` exercise
torrent-to-Domain mapping and metadata patch fidelity; files ending in `.bencode` exercise the
private bounded bencode decoder directly. Production code computes torrent identities from the exact
retained encoded `info` span rather than from a re-encoded tree.

## Valid torrent and fidelity fixtures

| Fixture | Mode / expected result | Purpose |
|---|---|---|
| `valid-v1.torrent` | strict + lenient success | Complete V1 single-file metadata, tracker tiers, web seeds, collections, nodes and info-scoped source |
| `valid-v1-multifile.torrent` | strict + lenient success | V1 multi-file path and length mapping |
| `valid-v2.torrent` | strict + lenient success | V2 single-file tree, SHA-256 info hash and validated piece layer |
| `valid-v2-multifile.torrent` | strict + lenient success | Nested V2 multi-file tree, path, length and pieces-root mapping |
| `valid-hybrid.torrent` | strict + lenient success | Dual hashes, V1/V2 multi-file layout correspondence and padding alignment |
| `valid-unicode-metadata.torrent` | strict + lenient success | Valid UTF-8 name, comment, creator and source |
| `patched-v1.torrent` | success | Golden deterministic top-level metadata/tracker patch |
| `top-level-source-v1.torrent` | success | Safe top-level source edit input |
| `patched-top-level-source-v1.torrent` | success | Golden top-level source patch |
| `unknown-extensions.torrent` | lenient + strict warning | `x-*` top-level and info extension retention in both modes |
| `similar-extension.torrent` | lenient + strict warning | BEP 38 `similar` and BEP 39 `update-url`/`originator` extension retention |
| `unknown-field.torrent` | lenient warning / strict `UnsupportedFeature` | Non-`x-*` unknown top-level and info keys |
| `valid-hidden-file-attribute.torrent` | strict + lenient success | V1 `h` attribute mapping |
| `valid-bep47-basic-symlink.torrent` | strict + lenient success | Minimal V1 zero-length `l` entry and symlink target |
| `valid-bep47-symlink.torrent` | strict + lenient success | V1 BEP 47 `h`/`l` attributes, SHA-1 hint and exposed torrent-relative symlink target |
| `valid-bep47-v2.torrent` | strict + lenient success | V2 BEP 47 link identity without pieces root or payload bytes |
| `valid-bep47-hybrid.torrent` | strict + lenient success | Matching V1/V2 link metadata; zero-byte link after payload needs no piece alignment |
| `valid-bep47-v1-symlink-without-length.torrent` | strict + lenient success | V1 link omits `length`; parser implies zero while retained bytes stay unchanged |
| `valid-bep47-v2-symlink-without-length.torrent` | strict + lenient success | V2 link omits `length`; parser implies zero while retained bytes stay unchanged |
| `valid-bep47-hybrid-symlink-without-length.torrent` | strict + lenient success | Matching V1/V2 link sides both omit `length` and normalize to zero |
| `valid-bep47-padding-without-path.torrent` | strict + lenient success | V1 padding omits irrelevant `path`; Domain synthesizes `.pad/<length>` |
| `valid-bep47-duplicate-padding-paths.torrent` | strict + lenient success | Duplicate padding paths are accepted because BEP 47 gives them no identity |
| `valid-bep47-single-file-attributes.torrent` | strict + lenient success | V1 single-file `x` and `h` attribute mapping |
| `valid-file-sha1-hint.torrent` | strict + lenient success | V1 file SHA-1 hint without an `attr` field |
| `unknown-file-attribute.torrent` | strict + lenient warning | Unknown `attr` character ignored semantically and retained for faithful writing |
| `invalid-path.torrent` | `InvalidTorrent` | Unsafe V1 traversal path |
| `invalid-v2-piece-layer.torrent` | `InvalidTorrent` | Piece-layer Merkle root mismatch |
| `invalid-utf8-comment.torrent` | lenient warning / strict `InvalidTorrent` | Raw invalid optional text retention |
| `invalid-utf8-creator.torrent` | lenient warning / strict `InvalidTorrent` | Raw invalid creator retention |
| `invalid-utf8-source.torrent` | lenient warning / strict `InvalidTorrent` | Raw invalid source retention |
| `invalid-utf8-name.torrent` | lenient unavailable-name warning / strict `InvalidTorrent` | Raw invalid required text retention |

## Invalid BEP 47 fixtures

All fixtures in this section are rejected as `InvalidTorrent` in both strict and lenient modes.

| Fixture | Purpose |
|---|---|
| `invalid-symlink-length.torrent` | Non-zero BEP 47 link length |
| `invalid-bep47-sha1-length.torrent` | SHA-1 hint is not 20 bytes |
| `invalid-bep47-sha1-type.torrent` | SHA-1 hint is not a byte string |
| `invalid-bep47-regular-missing-length.torrent` | Non-symlink file omits required `length` |
| `invalid-bep47-symlink-length-type.torrent` | Symlink `length` is present with a non-integer type |
| `invalid-bep47-missing-symlink-target.torrent` | `l` attribute without `symlink path` |
| `invalid-bep47-symlink-target-type.torrent` | Symlink target is not a component list |
| `invalid-bep47-symlink-target-parent.torrent` | Parent traversal target component |
| `invalid-bep47-symlink-target-absolute.torrent` | Absolute target component |
| `invalid-bep47-symlink-target-backslash.torrent` | Native separator in target component |
| `invalid-bep47-symlink-target-empty.torrent` | Empty target component |
| `invalid-bep47-symlink-target-utf8.torrent` | Invalid UTF-8 target component |
| `invalid-bep47-attr-type.torrent` | `attr` is not a byte string |
| `invalid-bep47-target-without-link-attr.torrent` | Target present without the `l` attribute |
| `invalid-bep47-hybrid-hidden-mismatch.torrent` | V1/V2 `h` mismatch |
| `invalid-bep47-hybrid-sha1-mismatch.torrent` | V1/V2 SHA-1 hint mismatch |
| `invalid-bep47-hybrid-target-mismatch.torrent` | V1/V2 symlink target mismatch |

## Malformed and duplicate-key bencode fixtures

All malformed and duplicate-key fixtures are rejected as `InvalidBencode` in both torrent read
modes. Duplicate keys are compared as raw bytes at every dictionary depth.

| Fixture | Purpose |
|---|---|
| `malformed-invalid-prefix.bencode` | Invalid value prefix |
| `malformed-truncated-dictionary.bencode` | Truncated dictionary |
| `malformed-leading-zero-integer.bencode` | Non-canonical positive integer |
| `malformed-negative-zero-integer.bencode` | Forbidden negative zero integer |
| `malformed-positive-integer-overflow.bencode` | Positive signed 64-bit integer overflow |
| `malformed-negative-integer-overflow.bencode` | Negative signed 64-bit integer overflow |
| `malformed-leading-zero-string-length.bencode` | Non-canonical string length |
| `malformed-string-length-overflow.bencode` | String length overflow |
| `malformed-trailing-data.bencode` | Extra value after the root value |
| `duplicate-top-level-key.bencode` | Duplicate root dictionary key |
| `duplicate-info-key.bencode` | Duplicate key inside `info` |
| `duplicate-nested-key.bencode` | Duplicate key inside an otherwise unknown nested dictionary |
| `duplicate-non-utf8-key.bencode` | Duplicate raw non-UTF-8 dictionary key |
| `unsorted-dictionary.bencode` | Unique non-canonical dictionary order accepted with input order retained |

## Resource-limit bencode fixtures

Each fixture is used twice: a configured limit one unit below the fixture requirement must reject
with `InvalidBencode`, while the exact required limit must decode successfully. This covers inclusive
boundary semantics for all private `BencodeLimits` fields.

| Fixture | Boundary covered |
|---|---|
| `limit-input.bencode` | Total input bytes |
| `limit-string.bencode` | Single string bytes |
| `limit-depth.bencode` | Nesting depth |
| `limit-tokens.bencode` | Parsed value tokens |
| `limit-list-entries.bencode` | List container entries |
| `limit-dictionary-entries.bencode` | Dictionary container entries |
| `limit-integer-digits.bencode` | Integer digits excluding sign |

## Independently generated torrent identities

- V1 single-file SHA-1: `f8baeab808eb4ddda341070f076fbb1f9fc32807`
- V1 multi-file SHA-1: `6132234443d5fa3de1a37b85d47b18fe20d5fbe2`
- V2 single-file SHA-256: `44a661ac3079f4a8478e9175e44ca3df74c2cbe1ed4a1fac2750d4eed00f48e5`
- V2 multi-file SHA-256: `0b0c2d2d48eac569fa628aa2f22d060489c5e1bf4952b9261e9c169fc665611f`
- Hybrid SHA-1: `8e50ec8648f10b05744e2bd10acad2ccb97dc2dd`
- Hybrid SHA-256: `c9807f71c38ded171f6b938e2dfdf9f11a76ba727dc94f40bc03b20461294840`
