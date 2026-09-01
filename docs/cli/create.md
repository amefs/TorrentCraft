# Create Torrents

Use the `create` command to generate a new `.torrent` file from a single file or a directory.

```text
torrentcraft create <content> -o <target.torrent> [options]
```

The output file is written only after all source files have been read and hashed successfully. If the target file already exists, the command will fail safely to protect your data unless `--overwrite` is explicitly provided.

## Practical Examples

```bash
# 1. Create a hybrid torrent with automatically calculated piece size
torrentcraft create ./my-folder -o ./my-folder.torrent

# 2. Create a v1 torrent with a fixed 1 MiB piece size
torrentcraft create ./my-folder -o ./my-folder.torrent \
  --format v1 --piece-size 1024

# 3. Test and validate settings without writing to disk
torrentcraft create ./my-folder -o ./my-folder.torrent --dry-run

# 4. Output machine-readable JSON for scripts and CI
torrentcraft create ./my-folder -o ./my-folder.torrent --json
```

## Protocol Format & Piece Sizing

- `--format` accepts `v1`, `v2`, or `hybrid`. Default is `hybrid`.
  - **`v1`**: Traditional BitTorrent v1 format using SHA-1 root hashing. Best for legacy clients.
  - **`v2`**: Next-generation BitTorrent v2 standard using SHA-256 Merkle trees per file.
  - **`hybrid`**: Includes both V1 and V2 trees, allowing modern and legacy clients to share data in the same swarm seamlessly.
- `--piece-size` accepts `auto` or a fixed size in KiB (e.g. `512`, `1024`, `2048`, `4096`). Fixed values must be powers of two from `16` KiB to `16384` KiB (16 MiB). Default is `auto`.

## File Ordering Policies

`--file-order` controls the sequence in which files are arranged inside the torrent dictionary:

- `lexicographical` — Sorted by raw byte-order (default; standard compliance).
- `natural` — Numbers inside filenames are sorted in natural numerical order (e.g. `file2` before `file10`), case-insensitive.
- `canonical_alignment` — Reorders files according to BitTorrent canonical alignment specifications.
- `breadth_first` — Sorted by directory hierarchy depth first, then lexicographically.

```bash
torrentcraft create ./series -o ./series.torrent \
  --format hybrid --piece-size auto --file-order natural
```

## Trackers and Web Seeds

Add announce URLs and arrange them into failover tiers:

```bash
torrentcraft create ./payload -o ./payload.torrent \
  --tracker 'https://tracker1.example/announce' --tier 0 \
  --tracker 'https://tracker2.example/announce' --tier 0 \
  --tracker 'https://backup.example/announce' --tier 1 \
  --web-seed 'https://cdn.example/payload.zip'
```

- Each `--tracker` may be followed by `--tier N` (indexes 0 to 64; default is tier 0).
- Providing any tracker on the CLI completely overrides the default tracker list inherited from configuration.
- `--web-seed` can be repeated to add multiple HTTP/HTTPS direct download sources.

## Creation Metadata & Privacy

```bash
torrentcraft create ./dataset -o ./dataset.torrent \
  --comment 'Official release build' \
  --created-by 'MyTeam CI' \
  --creation-date 1735689600 \
  --source 'internal-repo' \
  --private
```

- `--private`: Mark as a private torrent. Disables DHT, PEX, and local peer discovery in clients.
- `--comment`: Embed a descriptive comment string.
- `--created-by`: Embed creator signature information.
- `--creation-date`: Set a specific Unix timestamp (defaults to current time).
- `--source`: Embed the `info["source"]` tag (useful for private tracker segregation; changes the info hash).

## Using Configuration & Presets

Instead of passing dozens of flags each time, you can reuse templates defined in `torrentcraft.json`:

```bash
# Use a named preset from the default configuration
torrentcraft create ./payload -o ./payload.torrent --preset release

# Use a preset from a custom configuration file
torrentcraft create ./payload -o ./payload.torrent \
  --config ./custom-config.json --preset release

# Load an external preset JSON file
torrentcraft create ./payload -o ./payload.torrent \
  --preset-file ./preset_release.json
```

### Precedence Hierarchy

Settings are resolved in the following order:

```text
CLI flags > Selected Preset > Config Defaults > Built-in Engine Defaults
```

## Performance & Progress Control

- `--memory-working-set-limit SIZE`: Limits maximum process working set memory (defaults to `512 MiB`; primarily on Windows).
- `--progress <mode>`: Controls progress output written to standard error (`plain`, `tty`, `json`).
- `--quiet`: Suppresses non-error output.

## Option Reference

| Option | Description |
| --- | --- |
| `-o`, `--output PATH` | Output `.torrent` file destination (required). |
| `--format v1\|v2\|hybrid` | Select protocol format (default: `hybrid`). |
| `--piece-size KIB\|auto` | Select piece size in KiB or `auto` (default: `auto`). |
| `--file-order POLICY` | File sort policy: `lexicographical`, `natural`, `canonical_alignment`, `breadth_first`. |
| `--private`, `--no-private`, `--public` | Set or clear the private torrent flag. |
| `--tracker URL` | Add a tracker URL (optionally followed by `--tier N`; repeatable). |
| `--web-seed URL` | Add an HTTP/HTTPS web seed URL (repeatable). |
| `--comment TEXT` | Top-level comment string. |
| `--created-by TEXT` | Top-level creator signature string. |
| `--creation-date N` | Creation date as a Unix timestamp in seconds. |
| `--source TEXT` | Value for `info["source"]`. |
| `--preset NAME` | Apply a named preset from `torrentcraft.json`. |
| `--preset-file PATH` | Load an external preset JSON file. |
| `--config PATH` | Custom path to `torrentcraft.json`. |
| `--memory-working-set-limit SIZE` | Limit process memory working set (e.g. `512MiB`). |
| `--overwrite` | Allow replacing an existing output file. |
| `--dry-run` | Validate and test without writing to disk. |
| `--quiet` | Suppress progress and informational messages. |
| `--json` | Output structured JSON. |
| `--progress MODE` | Progress reporting mode (`json`, `plain`, `tty`). |
| `-h`, `--help` | Show command help. |
