# Validate Torrents

Use the `validate` command to quickly check whether a `.torrent` file is well-formed, syntactically correct, and compliant with BitTorrent specifications without reading or requiring payload data.

```text
torrentcraft validate <torrent> [options]
```

## Practical Examples

```bash
# 1. Perform standard validation on a torrent file
torrentcraft validate ./payload.torrent

# 2. Perform strict specification validation with JSON output
torrentcraft validate ./payload.torrent --strict --json
```

## Strict Mode vs. Lenient Loading

- **Standard Mode (Default)**: Validates the torrent while allowing common real-world edge cases (such as minor non-standard metadata keys or encoding fallbacks).
- **Strict Mode (`--strict`)**: Rejects any torrent that requires compatibility workarounds or departs from official BitTorrent specifications. Ideal for release verification in automated pipelines.

> **Validate vs. Verify**:
> - **`validate`**: Fast schema and structural check of the `.torrent` file itself. Does not read or hash content files.
> - **[`verify`](./verify)**: Compares local content files against the `.torrent` using cryptographic piece hashing.

## Option Reference

| Option | Description |
| --- | --- |
| `--strict` | Reject non-standard torrents that rely on lenient compatibility rules. |
| `--json` | Emit structured JSON output (`{"ok": true, "data": {"valid": true}}`). |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
