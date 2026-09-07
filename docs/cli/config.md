# Manage Configuration

TorrentCraft uses a single, canonical JSON configuration file named `torrentcraft.json`, shared seamlessly between the CLI and Desktop GUI.

```text
torrentcraft config path|show|init|get <key>|set <key> <value> [options]
```

## Configuration Discovery Order

When no `--config PATH` is explicitly provided, TorrentCraft searches for configuration in this order:

1. `./torrentcraft.json` (the current working directory)
2. `torrentcraft.json` beside the running executable (portable configuration)
3. The platform-specific user configuration directory:
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

# Select the default creation preset shared by CLI and GUI
torrentcraft config set default_preset '"release"'
torrentcraft config get default_preset

# Remove a setting by assigning null
torrentcraft config set defaults.comment null
```

## Annotated Configuration Example

```json
{
  "schema": "torrentcraft.config/v1",
  "default_preset": "release",
  "defaults": {
    "format": "hybrid",
    "piece_size": "auto",
    "private": false,
    "created_by": "TorrentCraft",
    "file_order": "lexicographical",
    "file_filter": {
      "mode": "common-artifacts",
      "case_sensitive": false,
      "patterns": []
    },
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

- **`default_preset`**: Optional shared fallback preset for CLI and GUI create/dry-run. Explicit
  `--preset` or `--preset-file` overrides it; legacy `gui.default_preset` is read and migrated on write.
- **`defaults`**: Global fallback creation settings for new torrents.
- **`presets`**: Named templates that can be applied with `--preset <name>`.
- **`verify` / `disk_io` / `memory_working_set_limit`**: Control hashing threads, caching buffers, and disk I/O modes.

## How creation settings are resolved

TorrentCraft resolves creation settings in layers, starting with built-in defaults and ending
with values specific to the current CLI or GUI operation:

~~~mermaid
flowchart TD
    BuiltIn["Built-in engine defaults"] --> Defaults["Global defaults: config.defaults"]
    Defaults --> PresetChoice{"Was --preset or --preset-file provided?"}
    PresetChoice -- "Yes" --> ExplicitPreset["Selected preset"]
    PresetChoice -- "No" --> DefaultChoice{"Is default_preset configured?"}
    DefaultChoice -- "Yes" --> DefaultPreset["Configured default preset"]
    DefaultChoice -- "No" --> Effective["Resolved base settings"]
    ExplicitPreset --> Effective
    DefaultPreset --> Effective
    Effective --> CliOverrides["Explicit CLI options and filter overrides"]
    CliOverrides --> CliResult["Final CLI creation settings"]
    Effective --> GuiForm["GUI Create form"]
    GuiForm --> GuiEdits["Manual edits on the form"]
    GuiEdits --> GuiResult["Final GUI creation settings"]
~~~

`default_preset` and an explicitly selected preset are alternative sources. When
`--preset` or `--preset-file` is provided, TorrentCraft uses that preset and skips
the `default_preset` fallback; the two presets are not merged. The GUI follows the
same resolution order, then applies any manual edits on the Create form as the
final values for that operation. The creator checkbox is a GUI-specific exception
documented in
[Desktop GUI](./gui#creator-override).

## File filter defaults and preset overrides

The optional `defaults.file_filter` object controls filtering for directory creates that do not
provide another filter policy:

~~~json
{
  "file_filter": {
    "mode": "common-artifacts",
    "case_sensitive": false,
    "patterns": []
  }
}
~~~

The supported modes are `disabled`, `common-artifacts`, and `custom-rules`. Custom rules use only
the listed Glob patterns; they do not extend the built-in common-artifact set. Matching is
case-insensitive unless `case_sensitive` is `true`. See [Create Torrents](./create#file-filtering)
for the complete pattern language and built-in rule list.

A named preset may declare its own complete `file_filter` object. A preset without that member
inherits the complete global default. A preset with the member atomically replaces the complete
default, so its `mode`, `case_sensitive`, and `patterns` do not merge independently:

~~~json
{
  "presets": {
    "archive": {
      "file_filter": {
        "mode": "custom-rules",
        "case_sensitive": true,
        "patterns": ["*.tmp", "cache/"]
      }
    }
  }
}
~~~

Use `mode: "disabled"` in a preset to explicitly turn filtering off. Existing configurations
without `file_filter` retain the disabled behavior. The Advanced GUI page edits the global
default, while the Create page displays the effective policy after preset resolution.

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
