# Inspect Torrents

The `inspect` command performs a fast, read-only analysis of a `.torrent` file without reading or requiring the original content data on disk.

```text
torrentcraft inspect <torrent> [options]
```

## Usage Examples

```bash
# 1. Inspect a torrent and view a clean terminal summary
torrentcraft inspect ./payload.torrent

# 2. Output detailed, structured JSON for scripts
torrentcraft inspect ./payload.torrent --json
```

## Information Displayed

The terminal summary provides:
- **General Info**: Torrent name, protocol format (`v1`, `v2`, `hybrid`), private flag state, total payload size, piece count, piece length, creator signature, and creation date.
- **Info Hashes**: V1 SHA-1 (Hex & Base32) and V2 SHA-256 (Hex) root hashes.
- **Trackers & Web Seeds**: Full list of announce URLs grouped by tier, plus any HTTP/HTTPS web seeds.
- **Verification Capabilities**: Details which hashing protocols and features can be verified against local files.
- **Diagnostics & Warnings**: Identifies non-standard fields, UTF-8 encoding irregularities, or legacy structures.

## Scripting & JSON Output

When `--json` is supplied, `inspect` returns a structured envelope:

```json
{
  "ok": true,
  "data": {
    "name": "my-payload",
    "format": "hybrid",
    "private": false,
    "total_size": 104857600,
    "piece_length": 1048576,
    "piece_count": 100,
    "info_hash_v1": "...",
    "info_hash_v2": "...",
    "trackers": [
      { "tier": 0, "url": "https://tracker.example/announce" }
    ]
  }
}
```

> **Inspect vs. Verify vs. Validate**:
> - **`inspect`**: Reads the `.torrent` file metadata and capabilities without touching local payload files.
> - **[`verify`](./verify)**: Compares local files against the `.torrent` to verify data completeness.
> - **[`validate`](./validate)**: Fast syntactic and schema check on the `.torrent` file itself.

## Option Reference

| Option | Description |
| --- | --- |
| `--json` | Emit structured JSON output. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
