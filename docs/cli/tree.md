# Print File Tree

Use `tree` to visualize the logical file and directory hierarchy stored inside a `.torrent` file.

```text
torrentcraft tree <torrent> [--depth N] [options]
```

## Practical Examples

```bash
# 1. Print the complete human-readable directory tree
torrentcraft tree ./payload.torrent

# 2. Print only the root folder and top-level items
torrentcraft tree ./payload.torrent --depth 0

# 3. Limit tree depth to 1 level of subdirectories
torrentcraft tree ./payload.torrent --depth 1

# 4. Emit a nested JSON tree for scripts and automation
torrentcraft tree ./payload.torrent --json
```

## Depth Control & Formatting

- `--depth N`: Limits output depth to a non-negative integer. Depth `0` shows only the root node. Omitting `--depth` prints the entire hierarchy.
- **Automatic BEP 52 Filtering**: Internal BitTorrent v2 padding files are automatically hidden from both terminal and JSON outputs, ensuring a clean and readable file list.
- **Terminal Rendering**: Uses clear tree connector glyphs (`├──` and `└──`). Automatically falls back to standard ASCII connectors on legacy consoles.

## Option Reference

| Option | Description |
| --- | --- |
| `--depth N` | Limit hierarchy traversal depth (non-negative integer). |
| `--json` | Emit nested JSON tree structure. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
