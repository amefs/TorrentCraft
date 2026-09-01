# Manage Configuration

TorrentCraft uses a single, canonical JSON configuration file named `torrentcraft.json`, shared seamlessly between the CLI and Desktop GUI.

```text
torrentcraft config path|show|init|get <key>|set <key> <value> [options]
```

## Configuration Discovery Order

When no `--config PATH` is explicitly provided, TorrentCraft searches for configuration in this order:

1. `./torrentcraft.json` (the current working directory)
2. The platform-specific user configuration directory:
   - **Linux / macOS**: `$XDG_CONFIG_HOME/torrentcraft/torrentcraft.json`, or `$HOME/.config/torrentcraft/torrentcraft.json`
   - **Windows**: `%APPDATA%\torrentcraft\torrentcraft.json`

An explicit `--config` path or discovered configuration file is authoritative. If the file contains invalid JSON or unsupported keys, the command reports an error immediately without falling back to defaults.

## Common Operations

```bash
# Show the active configuration path
torrentcraft config path

# Display the entire configuration in JSON format
torrentcraft config show --json

# Initialize a clean configuration file template
torrentcraft config init --config ./torrentcraft.json

# Force overwrite an existing configuration file
torrentcraft config init --config ./torrentcraft.json --force
```

## Reading & Updating Settings

Keys use dot-notation paths. You can modify creation defaults, verification parameters, or named presets:

```bash
# Read a specific setting
torrentcraft config get defaults.piece_size

# Update settings (use JSON syntax for values)
torrentcraft config set defaults.private true
torrentcraft config set verify.workers 4
torrentcraft config set verify.memory '"64 MiB"'
torrentcraft config set disk_io '"mmap"'

# Remove a setting by assigning null
torrentcraft config set defaults.comment null
```

## Annotated Configuration Example

```json
{
  "schema": "torrentcraft.config/v1",
  "defaults": {
    "format": "hybrid",
    "piece_size": "auto",
    "private": false,
    "created_by": "TorrentCraft",
    "file_order": "lexicographical",
    "tracker_list": [],
    "web_seeds": []
  },
  "presets": {
    "release": {
      "piece_size": 4096,
      "private": true
    }
  },
  "verify": {
    "workers": 1,
    "memory": "32 MiB"
  },
  "disk_io": "mmap",
  "memory_working_set_limit": "512 MiB"
}
```

- **`defaults`**: Global fallback creation settings for new torrents.
- **`presets`**: Named templates that can be applied with `--preset <name>`.
- **`verify` / `disk_io` / `memory_working_set_limit`**: Control hashing threads, caching buffers, and disk I/O modes.

## Option Reference

| Option | Description |
| --- | --- |
| `--config PATH` | Custom path to `torrentcraft.json`. |
| `--dry-run` | Validate the edit without writing to disk. |
| `--backup` | Create a `.bak` backup copy before writing. |
| `--force` | Allow `config init` to overwrite an existing file. |
| `--json` | Emit structured JSON output. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
