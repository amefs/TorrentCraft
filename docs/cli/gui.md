# Desktop GUI

TorrentCraft includes a standalone Qt desktop application named `torrentcraft-gui`. It provides a clean, visual interface for creating, modifying, inspecting, verifying, and configuring torrents, backed by the exact same engine and configuration file as the CLI.

## Launching the GUI

```bash
torrentcraft-gui
```

- **Linux**: Built statically with musl and the X11/XCB Qt platform backend (requires an active X11 display environment).
- **Windows**: Built statically using `/MT` with zero external runtime dependencies.

---

## 1. Create (Make New Torrents)

The **Create** page is your starting point for generating a new `.torrent` file from any file or folder.

![Create page](/screenshots/Create.png)

### Key Features:
- **Input & Output**: Select the source folder/file and choose where to save the output `.torrent`. Toggle the overwrite safeguard if the destination file already exists.
- **Settings Group**:
  - **Format**: Choose between **V1** (maximum compatibility with legacy clients), **V2** (modern BitTorrent v2 standard with SHA-256 piece trees), or **Hybrid** (recommended; compatible with all clients).
  - **Piece Size**: Set to **Automatic** (optimal piece size calculated from payload volume) or choose a fixed size from 16 KiB to 16 MiB.
  - **File Ordering**: Choose between Lexicographical, Natural, Canonical Alignment, or Breadth-First.
  - **Private Torrent**: Mark the torrent as private to disable DHT, PEX, and local peer discovery for private trackers.
- **Fields Group**: Add Tracker announce tiers, HTTP/HTTPS Web Seeds, comments, creator signature, and custom info source tags.
- **Dry Run & Progress**: Test and validate creation settings without writing to disk, and monitor real-time hashing progress with a cancellation option.

> **Preset Integration**: Fields automatically populate from your active preset and global defaults. Any manual adjustments made on the Create page take precedence for the current session without overwriting your saved preset.

---

## 2. Modify (Safely Edit Existing Torrents)

The **Modify** page allows you to open an existing `.torrent` file and safely edit its metadata fields without altering or re-hashing the underlying payload data.

![Modify page](/screenshots/Modify.png)

### What You Can Edit:
- Torrent name, comments, creator signature, and creation timestamp.
- Tracker tiers, Web Seeds, DHT bootstrap nodes, and Private flag.
- Custom `info["source"]` tags.

### Safety & Clear Options:
- **Checkbox Clear Mode**: Explicitly clear individual metadata fields (e.g. remove existing trackers, wipe comments).
- **Separate Output Path**: Input and output paths are separate by default, preventing accidental overwrite of original torrent files.
- **Dry Run & Backup**: Preview changes or create a `.bak` backup before writing.

> [!NOTE]
> Structural changes that alter file boundaries, file paths, or content hashes require creating a fresh torrent rather than editing.

---

## 3. Inspect (View Torrent Metadata & File Trees)

The **Inspect** page provides instant, read-only analysis of a `.torrent` file without needing the original content data on disk.

![Inspect page](/screenshots/Inspect.png)

### Information Displayed:
- **Summary Header**: Displays torrent name, protocol format (V1 / V2 / Hybrid), privacy state, total piece count, piece length, total payload size, creator, and available Info Hashes (SHA-1 and SHA-256).
- **Logical File Tree**: Browse the full directory hierarchy stored inside the torrent with folder expand/collapse and file size labels.
- **Tracker & Metadata Lists**: Inspect all configured tracker announce tiers, web seeds, and raw metadata fields.
- **Diagnostics & Warnings**: Identifies non-standard structures, legacy encodings, or potential compatibility issues.

---

## 4. Verify (Check Local Content Integrity)

The **Verify** page compares a local folder or file against a `.torrent` file by re-calculating piece hashes to ensure every file is 100% complete and uncorrupted.

![Verify page](/screenshots/Verify.png)

### Verification Highlights:
- **Visual Per-File Status**: Expands the full file table with clear status badges (e.g. *Verified*, *Mismatch*, *Missing*, *Pending*).
- **Resource Management**: Uses the worker thread count, memory cache buffer, and disk I/O modes configured in the **Advanced** tab.
- **Safe Cancellation**: If you close the window or cancel while verification is underway, a confirmation prompt prevents accidental data loss, and diagnostic results remain visible.

---

## 5. Tracker (Batch Manage Multiple Torrents)

The **Tracker** page allows you to batch-update tracker lists across an entire folder of `.torrent` files in a single operation.

![Tracker page](/screenshots/Tracker.png)

### How It Works:
1. Select the **Source Directory** containing your `.torrent` files and choose a **Target Output Directory**.
2. Construct your desired Tracker list using **Add**, **Edit**, **Remove**, **Move Up**, and **Move Down** buttons.
3. Click **Reload** to scan files, then click **Batch Convert** to process the whole folder.
4. Protect your files using **Dry Run** and **Backup** checkboxes.

---

## 6. Advanced (Configuration Hub)

The **Advanced** tab is the central settings hub for managing global configuration defaults, performance limits, display themes, and logging.

![Advanced page](/screenshots/Advanced.png)

### Key Settings Groups:

- **Configuration File**:
  - Displays the active `torrentcraft.json` path.
  - Actions: **Browse** (open another config), **Reload** (re-read external edits), **Initialize** (create a clean config template), and **Show** (view raw JSON).
- **Default Save Location**:
  - Choose where new torrents are saved by default: *Current directory*, *Recent location*, or a *Specified directory*.
- **Creation Defaults**:
  - Set default protocol format, automatic piece size, default private state, default tracker tiers, and creator signatures for all new torrents.
  - Set the **Default Preset** to load at GUI startup.
- **Performance & I/O**:
  - Configure **Disk Mode** (`mmap` or standard I/O), **Verification Workers** (parallel threads), **Verification Memory Buffer (MiB)**, and **Working-Set Memory Limit**.
- **Appearance & Diagnostics**:
  - Choose GUI style, typography, and optional display of BEP 52 padding files.
  - Configure logging levels (Debug, Info, Warning, Error), custom log file paths, and one-click diagnostic log copy.

---

## Preset Management Workflow

Presets allow you to store different creation templates (for example, a *Private Tracker Preset* vs. a *Public Distribution Preset*) directly in `torrentcraft.json`.

![Preset dialog and menu](/screenshots/Preset.png)

The top-level **Preset** menu provides:
- **Import From File**: Import an external `.json` settings file as a named preset.
- **Load Preset**: Apply a saved preset onto the current Create page.
- **Save Preset**: Save current Create page settings under a preset name.
- **Delete Preset**: Delete an existing preset from your configuration.

### Effective Resolution Order

When creating a torrent, settings are resolved in this precedence:

```text
Manual edits on Create page > Selected Preset > Global Config Defaults > Built-in Engine Defaults
```

---

## Additional Features

- **Drag & Drop**: Drag files, folders, or `.torrent` files directly into the window or onto path input fields.
- **Language Switching**: Switch between English and 简体中文 (Simplified Chinese) anytime via the **Language** menu.
- **Desktop Theme Integration**: On Linux, TorrentCraft automatically inherits your system GTK theme, host fonts, and system file icons, falling back to embedded SVG icons when unavailable.
