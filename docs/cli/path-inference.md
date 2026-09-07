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

## Options and configuration

After the target operation is inferred, the remaining options are parsed by the same command handler
as an explicit subcommand. Create options such as `--config`, `--preset`, `--preset-file`,
`--format`, `--piece-size`, `--file-order`, file-filter options, tracker options, and progress or
resource limits can appear before or after the paths:

```bash
torrentcraft --config ./torrentcraft.json --preset release ./payload --dry-run --json
torrentcraft ./payload --format v1 --piece-size 1024 --output ./release.torrent
```

Inferred create therefore uses the same `default_preset` and overlay precedence as
`torrentcraft create`: `config.defaults` is loaded first, `default_preset` is overlaid when no
explicit preset is selected, and explicit CLI options are applied last. Configuration discovery
is also unchanged: an explicit `--config` wins, otherwise the current working directory, the
executable directory, and then the user configuration directory are checked. The directory
containing the input is not searched automatically. See [Creation setting resolution](./config#how-creation-settings-are-resolved)
for the shared resolution diagram.

For create, the automatic `<input>.torrent` target is added only when `-o`/`--output` was not
provided. An explicit output always wins, including the directory-plus-file form.

## Precedence & Disambiguation

- **Keyword Precedence**: Known subcommands (e.g. `create`, `inspect`, `verify`, `config`) always take precedence over files with the same name.
- **When to Use Explicit Subcommands**: Use an explicit subcommand when you want its command-specific help or when the paths are ambiguous. Otherwise, path inference supports the same options and configuration precedence as the inferred target command.
