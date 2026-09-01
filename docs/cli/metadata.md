# Edit Metadata

Use the `metadata` command to inspect, update, or clear top-level metadata and identity fields on existing `.torrent` files without re-hashing content data.

```text
torrentcraft metadata show|set|clear <torrent> ... [options]
```

All modifications are validated thoroughly before saving. Use `--dry-run` to preview changes and `--backup` to preserve the original file.

## 1. Show Metadata (`show`)

```bash
torrentcraft metadata show ./payload.torrent
torrentcraft metadata show ./payload.torrent --json
```

Displays comment, creator signature, creation date, web seeds, collections, DHT bootstrap nodes, and privacy status.

## 2. Set Metadata (`set`)

```bash
torrentcraft metadata set ./payload.torrent \
  --comment 'Official verified release' \
  --created-by 'TorrentCraft' \
  --creation-time now \
  --backup
```

### Supported Set Options:

| Option | Description |
| --- | --- |
| `--comment TEXT` | Set the top-level descriptive comment. |
| `--created-by TEXT` | Set the creator signature string. |
| `--info-source TEXT` | Set the `info["source"]` tag. |
| `--name TEXT` | Rename the torrent's root info name. |
| `--creation-time N\|now` | Set creation timestamp (`now` or Unix epoch seconds). |
| `--web-seed URL` | Set the web seed list to the specified URL. |
| `--dht-node HOST:PORT` | Set the DHT bootstrap node list. |
| `--private` | Set the private torrent flag. |
| `--no-private`, `--public` | Clear the private torrent flag. |

## 3. Clear Metadata (`clear`)

Explicitly wipe specific metadata fields from a torrent:

```bash
# Clear comment, creator, and web seeds in a single command
torrentcraft metadata clear ./payload.torrent \
  --comment --created-by --web-seeds \
  --backup
```

Supported flags for `clear`: `--comment`, `--created-by`, `--info-source`, `--creation-time`, `--web-seeds`, `--dht-nodes`.

## Identity Fields vs. Content Structure

- **Identity Fields (`--name`, `--private`, `--info-source`)**: Changing these fields modifies the torrent's identity and updates its Info Hash, but does **not** require re-hashing content files.
- **Content Structure**: Adding, removing, or renaming individual payload files alters piece boundaries and cannot be done through metadata editing. To change file contents, create a fresh torrent using [`create`](./create).

## Option Reference

| Option | Description |
| --- | --- |
| `--dry-run` | Validate and preview changes without modifying the file. |
| `--backup` | Create a `.bak` backup copy before writing. |
| `--json` | Emit structured JSON output. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
