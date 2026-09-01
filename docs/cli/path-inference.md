# Path Inference

TorrentCraft features smart path inference: when you pass a file or directory path as the first argument instead of a command name, it automatically detects your intent and executes the appropriate command.

## Inference Rules

| Input Arguments | Inferred Operation |
| --- | --- |
| **1 `.torrent` file path** | Automatically runs [`inspect`](./inspect) on the torrent. |
| **1 file or directory path** | Automatically runs [`create`](./create) to build `<name>.torrent` beside it. |
| **1 `.torrent` + 1 file/directory** | Automatically runs [`verify`](./verify) to check local files against the torrent. |
| **1 directory + 1 regular file** | Automatically creates a torrent for the file inside that directory. |
| **Ambiguous combinations** | Exits safely with a usage error without guessing. |

## Practical Examples

```bash
# 1. Quick inspect (equivalent to: torrentcraft inspect ./payload.torrent)
torrentcraft ./payload.torrent

# 2. Quick create (equivalent to: torrentcraft create ./payload -o ./payload.torrent)
torrentcraft ./payload

# 3. Quick verify (torrent and data path can be supplied in either order)
torrentcraft ./payload.torrent ./payload
torrentcraft ./payload ./payload.torrent --json
```

## Precedence & Disambiguation

- **Keyword Precedence**: Known subcommands (e.g. `create`, `inspect`, `verify`, `config`) always take precedence over files with the same name.
- **When to Use Explicit Subcommands**: Path inference is intentionally conservative. When you need fine-grained control over piece size, protocol format, tracker tiers, memory budgets, or custom config paths, always specify the full subcommand (e.g. `torrentcraft create ...`).
