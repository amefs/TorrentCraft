# Manage Presets

Presets are reusable creation configuration templates stored under `presets.<name>` inside `torrentcraft.json`. They allow you to apply complex sets of creation options (format, piece size, private flag, sorting, and file filtering) with a single `--preset` flag.

```text
torrentcraft preset list|show <name>|add <file>|remove <name> [options]
torrentcraft preset add --from-torrent <torrent> [--name NAME] [--fields LIST|all] [options]
```

## 1. List & Show Presets

```bash
# List all configured preset names
torrentcraft preset list
torrentcraft preset list --json

# View the detailed configuration of a specific preset
torrentcraft preset show release
```

## 2. Import a Preset File (`add`)

Create a JSON file with your desired settings (e.g. `preset_release.json`):

```json
{
  "format": "hybrid",
  "piece_size": 4096,
  "private": true,
  "file_order": "natural",
  "file_filter": {
    "mode": "custom-rules",
    "case_sensitive": false,
    "patterns": ["*.tmp", "cache/"]
  }
}
```

Import it into your configuration:

```bash
torrentcraft preset add ./preset_release.json
```

- The preset name is automatically derived from the filename stem (e.g. `preset_release.json` becomes `release`).
- If a preset with the same name already exists, add `--force` to overwrite it.

### Import a preset from an existing torrent

You can also create a preset directly from an existing `.torrent` file. TorrentCraft extracts its reusable creation settings and leaves behind content-specific data (torrent name, file list, piece hashes, info hashes, creation date, DHT nodes, and collections).

```bash
# Import the default reusable settings and name the preset after the file stem
torrentcraft preset add --from-torrent ./release.torrent

# Preview what would be saved (including full tracker URLs) without writing
torrentcraft preset add --from-torrent ./release.torrent --dry-run

# Pick specific fields and set a custom preset name
torrentcraft preset add --from-torrent ./release.torrent --name tracker-a \
  --fields trackers,piece-size,private,source
```

| Field | Imported value | Included by default |
| --- | --- | --- |
| `trackers` | Announce tiers (`announce-list`, or single `announce`) | Yes |
| `piece-size` | Fixed `piece length` (must be a power of two from 16 KiB to 16 MiB) | Yes |
| `private` | `info.private` (`false` when absent) | Yes |
| `source` | `info.source` (a top-level `source` field is ignored) | Yes |
| `web-seeds` | HTTP/HTTPS `url-list` entries | Yes |
| `format` | `v1`, `v2`, or `hybrid` as declared by the torrent | Yes |
| `comment` | Top-level `comment` | No |
| `created-by` | Top-level `created by` | No |

How importing works:

- **Unselected fields** are omitted from the saved preset, so they continue to inherit from `default_preset`, global config defaults, and built-in defaults.
- **Missing or unsupported fields** in the torrent (for example, no trackers or web seeds, unreadable text, or a non-standard piece size) are skipped with a warning—even if you listed them in `--fields`. The command fails only if none of the requested fields can be imported.
- **Non-standard metadata is tolerated**: unknown or `x-*` extension keys are ignored, invalid tracker or web seed URLs are skipped while valid ones are kept, and non-UTF-8 text simply makes that text field unavailable (each reported as a warning). Only unreadable or structurally broken torrents (corrupt bencode, invalid `info` dictionary or piece hashes, unsafe file paths) are rejected.
- **`--json` output** returns the saved `settings` along with `load_warnings` and `skipped_fields`.
- **Passkeys are preserved**: tracker URLs (including private passkeys) are saved as-is into `torrentcraft.json`.
- **Desktop GUI**: You can do the same in the GUI via **Preset → Import From File**, pick a `.torrent` file, and check the fields you want to keep in the **Import from torrent** dialog (see [Desktop GUI](./gui#preset-management-workflow)).

## 3. Remove a Preset (`remove`)

```bash
torrentcraft preset remove release
```

## 4. Applying Presets During Creation

```bash
# Create a torrent using the imported preset
torrentcraft create ./payload -o ./payload.torrent --preset release
```

### Precedence Order

```text
Explicit CLI options > Explicit Preset > Default Preset > Config Defaults > Built-in Engine Defaults
```

The optional top-level `default_preset` config key is used only when no `--preset` or
`--preset-file` is supplied. It is a shared CLI/GUI create fallback and overlays the config
defaults; an explicit preset replaces it.

For `created_by`, the selected preset value, including a `default_preset` fallback,
overrides the config default. The CLI `--created-by` option is the explicit
highest-level override, and an explicitly empty value remains meaningful. See
[Creation setting resolution](./config#how-creation-settings-are-resolved) for the
full resolution diagram.


## File-filter override semantics

A preset `file_filter` member is one complete policy. If it is absent, the preset inherits
`defaults.file_filter`. If it is present, it replaces the complete global policy, including
the mode, case policy, and pattern list. Use `mode: "disabled"` to explicitly disable filtering
for that preset.

CLI filter flags are applied after the selected preset and remain field-level overrides. For
example, `--filter-case-sensitive` changes only case matching and preserves the effective
mode and patterns.

## Option Reference

| Option | Description |
| --- | --- |
| `--config PATH` | Custom path to `torrentcraft.json`. |
| `--force` | Force overwrite an existing preset when adding. |
| `--from-torrent PATH` | Add a preset from a torrent template's reusable settings. |
| `--name NAME` | Preset name for `add` (default: file name stem). |
| `--fields LIST\|all` | Comma-separated template fields to adopt with `--from-torrent`. |
| `--dry-run` | Validate changes without writing to disk. |
| `--backup` | Create a `.bak` backup copy before writing. |
| `--json` | Emit structured JSON output. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
