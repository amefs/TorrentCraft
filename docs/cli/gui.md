# Desktop GUI

TorrentCraft includes a standalone Qt desktop application named `torrentcraft-gui`. It provides a clean, visual interface for creating, modifying, inspecting, verifying, and configuring torrents, backed by the exact same engine and configuration file as the CLI.

## Launching the GUI

```bash
torrentcraft-gui
```

- **Linux**: Release builds ship with Qt's X11/XCB platform backend rather than native Wayland. When started without arguments, `torrentcraft-gui` checks that `DISPLAY` is set and an X11 or XWayland server can be reached; if not, it prints a short note to `stderr` and shows CLI usage instead of failing inside Qt. On Wayland desktops, make sure XWayland is enabled (`WAYLAND_DISPLAY` alone is not enough for the release binary). Note that this display check does not cover missing host packages such as `xcb-cursor0`.
- **Windows**: Built statically (`/MT`) with no external runtime dependencies. While `torrentcraft-gui.exe` can also run CLI commands when given arguments, the Windows shell does not wait for GUI executables and intercepts Ctrl+C. For command-line or script use, run `torrentcraft.exe` instead—or press **Ctrl+Break** if you need to cancel a command started via `torrentcraft-gui.exe` (see [Cancelling with Ctrl+C](./output#cancelling-with-ctrl-c)).

---

## 1. Create (Make New Torrents)

The **Create** page is your starting point for generating a new `.torrent` file from any file or folder.

![Create page](/screenshots/Create.png)

### Key Features:
- **Input & Output**: Select the source folder/file and choose where to save the output `.torrent`. Toggle the overwrite safeguard if the destination file already exists.
- **Settings Group**:
  - **Format**: Choose between **V1** (maximum compatibility with legacy clients), **V2** (modern BitTorrent v2 standard with SHA-256 piece trees), or **Hybrid** (recommended; compatible with all clients).
  - **Piece Size**: Set to **Automatic** (optimal piece size calculated from payload volume) or choose a power-of-two size from 16 KiB to 16 MiB. Advanced uses the same dropdown for creation defaults. Presets override these defaults; sizes are stored in KiB (8 MiB is 8192 KiB).
  - **File Ordering**: Choose between Lexicographical, Natural, Canonical Alignment, or Breadth-First.
  - **File Filter**: Choose **Disabled**, **Common system artifacts**, or **Custom exclusion rules**.
    Custom rules use one Glob pattern per line and replace the built-in common-artifact list.
    The **Case-sensitive** checkbox controls matching for either built-in or custom rules.
  - **Private Torrent**: Mark the torrent as private to disable DHT, PEX, and local peer discovery for private trackers.
- **Fields Group**: Add Tracker announce tiers, HTTP/HTTPS Web Seeds, comments, creator signature, and custom info source tags.
- **Filter report**: After a directory create or dry run, view the relative paths and byte summaries of entries excluded by the effective filter.
- **Creator override**: The Create page shows the effective creator value. **Use custom creator**
  enables the current preset value as a temporary override; if no preset value exists, it starts
  from the config default or the built-in `TorrentCraft` value. Clearing the checkbox skips the
  preset creator field and falls back to config or the built-in default.
- **Dry Run & Progress**: Test and validate creation settings without writing to disk, and monitor real-time hashing progress with a cancellation option.
- **Reset**: Clears the input/output paths and manual edits on the page while keeping the currently selected preset and reapplying its settings.

> **Preset Integration**: Fields automatically populate from your active preset and global defaults. Any manual adjustments made on the Create page take precedence for the current session without overwriting your saved preset.

The Advanced page's **Default preset** is stored in the shared top-level `default_preset` key.
When Create has no explicit preset selection, both CLI and GUI start with `config.defaults` and
overlay that fallback preset on top. An explicit preset selection replaces `default_preset`; CLI
flags and GUI form edits are then applied at their respective final layers. See [Creation setting
override flow](./config#how-creation-settings-are-resolved) for the shared resolution chain.

### Creator Override

The creator checkbox is intentionally more specific than the general field precedence:

~~~mermaid
flowchart TD
    CreatorToggle["Use custom creator checkbox"] --> Checked{"Checked?"}
    Checked -- "No" --> ConfigCreator["config.defaults.created_by"]
    Checked -- "Yes" --> PresetCreator["Active preset created_by"]
    PresetCreator --> HasPresetCreator{"Preset value exists?"}
    HasPresetCreator -- "Yes" --> CreatorResult["Use preset creator"]
    HasPresetCreator -- "No" --> ConfigCreator
    ConfigCreator --> HasConfigCreator{"Config value exists?"}
    HasConfigCreator -- "Yes" --> CreatorResult
    HasConfigCreator -- "No" --> BuiltInCreator["Built-in TorrentCraft creator"]
~~~

When the checkbox is cleared, the active preset's `created_by` is deliberately skipped. When it
is checked, the active preset may provide the temporary creator override; missing values still
fall back through config and the built-in value.

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
- **Strict Loading (`Strict`)**: Check **Strict** before loading to reject torrents that rely on lenient compatibility workarounds, matching [`--strict` in the CLI](./validate#strict-mode-vs-lenient-loading).
- **Separate Output Path**: Input and output paths are separate by default, preventing accidental overwrite of original torrent files.
- **Dry Run, Backup & Reset**: Preview changes or create a `.bak` backup before writing, or click **Reset** to unload the torrent and restore the page to its initial state.

> [!NOTE]
> Structural changes that alter file boundaries, file paths, or content hashes require creating a fresh torrent rather than editing.

---

## 3. Inspect (View Torrent Metadata & File Trees)

The **Inspect** page provides instant, read-only analysis of a `.torrent` file without needing the original content data on disk.

![Inspect page](/screenshots/Inspect.png)

### Information Displayed:
- **Summary Header**: Displays torrent name, protocol format (V1 / V2 / Hybrid), privacy state, total piece count, piece length in human-readable IEC units (for example, 8 MiB), total payload size, creator, and available Info Hashes (SHA-1 and SHA-256).
- **Logical File Tree**: Browse the full directory hierarchy stored inside the torrent with folder expand/collapse and file size labels. Multi-file (directory) torrents show the torrent's root folder as the top-level node, matching `torrentcraft tree`.
- **Tracker & Metadata Lists**: Inspect all configured tracker announce tiers, web seeds, and raw metadata fields.
- **Diagnostics, Strict Mode & Reset**: Surfaces non-standard structures, legacy encodings, or compatibility warnings. Enable **Strict** to reject torrents that require lenient loading (see [Strict Mode vs. Lenient Loading](./validate#strict-mode-vs-lenient-loading)), or click **Reset** to clear the loaded torrent.

---

## 4. Verify (Check Local Content Integrity)

The **Verify** page compares a local folder or file against a `.torrent` file by re-calculating piece hashes to ensure every file is 100% complete and uncorrupted.

![Verify page](/screenshots/Verify.png)

### Verification Highlights:
- **Layout-Aware Content Picker**: Once a torrent is loaded, the content selector automatically matches its file layout: a single-file torrent prompts you to pick a file, while a directory torrent prompts you to pick a folder (even if that folder contains only one file). Before a torrent is loaded, either can be chosen. Passing a file where a folder is expected—or vice versa—is rejected with a clear message rather than guessing or rewriting your path.
- **Visual Per-File Status**: Expands the full file tree (with the root folder as the top-level node for directory torrents) and shows clear status badges (*Verified*, *Mismatch*, *Missing*, *Pending*).
- **Resource Management**: Uses the worker thread count, memory cache buffer, and disk I/O modes configured in the **Advanced** tab.
- **Safe Cancellation & Reset**: If you close the window or cancel while verification is underway, a confirmation prompt prevents accidental interruption, and diagnostic results stay on screen afterward. Click **Reset** when idle to clear the page.
- **Cancellable Hashing**: Cancellation is cooperative at piece boundaries; a live cancellation token does not force a slower disk backend, so the configured mmap/default path remains available.

---

## 5. Tracker (Batch Manage Multiple Torrents)

The **Tracker** page allows you to batch-update tracker lists across an entire folder of `.torrent` files in a single operation.

![Tracker page — Replace list mode](/screenshots/TrackerReplace.png)

### How It Works:
1. Select the **Source Directory** containing your `.torrent` files and choose a **Target Output Directory**. Check **Include subfolders** if you also want to process torrents in nested directories (when an output folder is set, the subfolder structure is mirrored automatically).
2. Choose **Replace list** (to overwrite every torrent's tracker list with a new set of tiers) or **Rewrite (find and replace)** (to edit each torrent's existing URLs in place).
3. Click **Reload Folder** to scan files, then click **Batch Convert** to process the folder—or **Reset** to clear the file list and restore the page to its initial state.
4. Protect your files using **Dry run** and **Backup** checkboxes.

### Rewrite (Find and Replace) Mode

Select **Rewrite (find and replace)** when you want to update each torrent's own tracker URLs instead of replacing the entire list—for example, migrating to a new tracker domain while keeping each torrent's unique passkey intact. Rules work just like find-and-replace in a text editor:

![Tracker page — Rewrite mode](/screenshots/TrackerRewrite.png)

- Enter the text to search for in **Find** and the new text in **Replace**. Every match across every tracker URL is updated. For example, setting Find to `tracker.old.example` and Replace to `tracker.new.example` swaps the domain while preserving the rest of the URL (including any passkey).
- **Aa** (Match case) is enabled by default so case-sensitive passkeys are not altered accidentally.
- **.\*** (Use regular expression) switches Find to [RE2 regular expression](https://github.com/google/re2/wiki/Syntax) matching. In Replace, use `$1`–`$9` for captured groups, `$0` or `$&` for the whole match, and `$$` for a literal `$` (for example, Find `^udp://(tracker\.example):6969` and Replace `https://$1`).
- Click **+ Add rule** to chain multiple rules from top to bottom. Rows with an empty Find box are skipped, and **×** removes a row. (In the CLI, use `-e 's#old#new#g'` with [`tracker rewrite`](tracker.md).)
- **Live Preview**: Click any torrent in the list to see its **Original URL**, **Result**, and **Status** (*Changed*, *Unchanged*, *Merged*, or *Invalid* with the reason), updated live as you type. Click **Preview all** to check the entire batch and populate the Status column (*Will change*, *No change*, or *Will fail*).
- **Safety**: **Batch Convert** is automatically disabled if any rule has invalid syntax (pointing out the rule number). Because rewrite rules may contain private passkeys, they are kept only for the current session and are never written to `torrentcraft.json`.

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
  - Set default protocol format, automatic or fixed piece size, file order, private state, file-filter policy, tracker tiers, and creator signatures for all new torrents.
  - The global **File Filter** default is used when the selected preset does not declare its own filter object.
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
- **Import From File**: Import a named preset from an external `.json` preset file or directly from an existing `.torrent` file. When you select a `.torrent` file, the **Import from torrent** dialog lets you set the preset name and pick which reusable fields (trackers, piece size, private flag, source, web seeds, format, comment, or creator) to adopt.
- **Load Preset**: Apply a saved preset onto the current Create page.
- **Save Preset**: Save current Create page settings under a preset name.
- **Delete Preset**: Delete an existing preset from your configuration.

![Import preset from torrent dialog](/screenshots/ImportFromTorrent.png)

### Effective Resolution Order

When creating a torrent, settings are resolved in this precedence:

```text
Manual edits on Create page > Selected Preset (or `default_preset`) > Global Config Defaults > Built-in Engine Defaults
```

---

## Additional Features

- **Drag & Drop**: Drag files, folders, or `.torrent` files directly into the window or onto path input fields.
- **Language Switching**: Switch between English and 简体中文 (Simplified Chinese) anytime via the **Language** menu.
- **Desktop Theme Integration**: On Linux, TorrentCraft automatically inherits your system GTK theme, host fonts, and system file icons, falling back to embedded SVG icons when unavailable.
