# Manage Presets

Presets are reusable creation configuration templates stored under `presets.<name>` inside `torrentcraft.json`. They allow you to apply complex sets of creation options (format, piece size, private flag, sorting) with a single `--preset` flag.

```text
torrentcraft preset list|show <name>|add <file>|remove <name> [options]
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
  "file_order": "natural"
}
```

Import it into your configuration:

```bash
torrentcraft preset add ./preset_release.json
```

- The preset name is automatically derived from the filename stem (e.g. `preset_release.json` becomes `release`).
- If a preset with the same name already exists, add `--force` to overwrite it.

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
CLI options > Selected Preset > Config Defaults > Built-in Engine Defaults
```

## Option Reference

| Option | Description |
| --- | --- |
| `--config PATH` | Custom path to `torrentcraft.json`. |
| `--force` | Force overwrite an existing preset when adding. |
| `--dry-run` | Validate changes without writing to disk. |
| `--backup` | Create a `.bak` backup copy before writing. |
| `--json` | Emit structured JSON output. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
