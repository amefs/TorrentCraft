# Manage Trackers

The `tracker` command allows you to view, add, remove, or completely replace tracker announce tiers in existing `.torrent` files.

```text
torrentcraft tracker list|add|remove|replace <torrent> ... [options]
```

Changes are written directly to the target `.torrent` file. Use `--dry-run` to preview edits safely or `--backup` to automatically create a `.bak` copy before saving.

## 1. List Trackers (`list`)

```bash
# View trackers grouped by tier in human-readable format
torrentcraft tracker list ./payload.torrent

# Output structured JSON
torrentcraft tracker list ./payload.torrent --json
```

## 2. Add a Tracker (`add`)

```bash
# Add a tracker to Tier 1
torrentcraft tracker add ./payload.torrent \
  'https://tracker.example/announce' --tier 1
```

- If `--tier` is omitted, Tier 0 (primary) is used.
- New tiers can be created at the next sequential tier index.

## 3. Remove a Tracker (`remove`)

Remove a tracker by specifying its zero-based tier index and tracker index within that tier:

```bash
# Remove the first tracker (index 0) from Tier 1
torrentcraft tracker remove ./payload.torrent 1 0
```

> **Tip**: Run `torrentcraft tracker list ./payload.torrent` first to see the exact tier and index numbers.

## 4. Replace All Trackers (`replace`)

Completely overwrite the existing tracker list with a new set of tiers:

```bash
torrentcraft tracker replace ./payload.torrent \
  --tracker 'https://primary.example/announce' --tier 0 \
  --tracker 'https://backup.example/announce' --tier 1 \
  --backup
```

- Tier indexes range from `0` to `64`.
- All URLs are validated for correct URI syntax before writing.

## Safe Controlled Editing

Tracker updates are strictly controlled and non-destructive. Modifying tracker tiers updates the announce dictionary without touching file boundaries or content hashes.

## Option Reference

| Option | Description |
| --- | --- |
| `--dry-run` | Validate and preview changes without modifying the file. |
| `--backup` | Create a `.bak` backup copy before writing. |
| `--json` | Emit structured JSON output. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
