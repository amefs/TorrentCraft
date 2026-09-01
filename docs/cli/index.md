# CLI Guide

`torrentcraft` is a powerful, non-interactive command-line tool for working with BitTorrent files. It produces clean, human-readable terminal output by default and can emit deterministic, machine-readable JSON for automated scripts and CI pipelines.

If you prefer a graphical interface, see [Desktop GUI](./gui).

## Command Overview

| Command | Description |
| --- | --- |
| [`create`](./create) | Create a new V1, V2, or hybrid torrent from a file or directory. |
| [`inspect`](./inspect) | Display a detailed summary of torrent metadata, info hashes, and verification capabilities. |
| [`tree`](./tree) | Visualize the logical file tree structure stored inside a torrent. |
| [`verify`](./verify) | Check local files and directories against a torrent to ensure data integrity. |
| [`validate`](./validate) | Verify that a `.torrent` file is well-formed and compliant without checking payload data. |
| [`tracker`](./tracker) | List, add, remove, or replace tracker URLs and tier structures. |
| [`metadata`](./metadata) | View, update, or clear top-level metadata and identity fields (comment, creator, source, etc.). |
| [`config`](./config) | Inspect, initialize, and modify the shared `torrentcraft.json` configuration file. |
| [`preset`](./preset) | Manage named creation presets for quickly applying reusable settings. |
| [`completion`](./completion) | Generate shell autocompletion scripts for Bash, Zsh, or Fish. |

> **Smart Path Inference**: If the first argument passed to `torrentcraft` is a file or directory path rather than a command name, TorrentCraft will intelligently infer what you want to do (e.g. running `torrentcraft sample.torrent` automatically inspects it). See [Path Inference](./path-inference) for details.

## Global Help & Version

```bash
# Display general help and available commands
torrentcraft --help

# Check the installed version
torrentcraft --version

# View detailed options and examples for a specific command
torrentcraft <command> --help
```

## Shared CLI Conventions

All TorrentCraft subcommands follow consistent, predictable Unix conventions:

- **Standard Streams**: Successful results and primary data are sent to **stdout**. Progress bars, status notices, and diagnostic messages are sent to **stderr**, making pipe and redirection workflows clean and reliable.
- **Machine-Readable Output**: Pass `--json` to any supported command to receive a structured, stable JSON response envelope (e.g. `{"ok": true, "data": ...}`).
- **Quiet Mode**: Use `--quiet` to suppress progress and human-readable notices when only exit codes matter.
- **Safety First**:
  - Commands that create files refuse to overwrite existing files unless `--overwrite` is explicitly provided.
  - Commands that modify existing files support `--dry-run` (to test changes safely without writing to disk) and `--backup` (to automatically create a `.bak` copy before saving).

To begin with practical examples, head to [Getting Started](./getting-started) or jump directly into any command reference above.
