# Getting Started

## Download & Installation

TorrentCraft is distributed as standalone, self-contained binaries for Linux (x86_64) and Windows (x86_64). Both CLI and GUI applications are available:

- **CLI executable**: `torrentcraft` (Linux) / `torrentcraft.exe` (Windows)
- **GUI executable**: `torrentcraft-gui` (Linux) / `torrentcraft-gui.exe` (Windows)

Download the latest release from the repository's Releases page. The CLI and GUI
executables are listed directly; also download the matching platform support
bundle and extract it beside the executables.

> **Verification & Integrity**: The platform support bundle contains `SHA256SUMS`,
> detailed SBOM (Software Bill of Materials), provenance, and security audit
> reports. After extracting it beside the downloaded executables, verify the
> release with `sha256sum -c SHA256SUMS` on Linux or `Get-FileHash` on PowerShell.
> *(Note: 1.0.0 release binaries are currently unsigned; code signing is planned
> for a future release).*

## Quick Verification

Once downloaded and placed in your system `PATH` (or current folder), verify the installation:

```bash
# Check the version
torrentcraft --version

# View global command help
torrentcraft --help
```

You can also view dedicated help and option lists for any specific subcommand:

```bash
torrentcraft create --help
torrentcraft verify --help
```

## Your First 5-Minute Workflow

Here is a quick walkthrough showing the complete lifecycle of creating, inspecting, viewing, and verifying a torrent:

```bash
# 1. Create a hybrid torrent from a local folder
torrentcraft create ./my-folder -o ./my-folder.torrent

# 2. Inspect the generated metadata and info hashes
torrentcraft inspect ./my-folder.torrent

# 3. View the logical directory tree stored in the torrent
torrentcraft tree ./my-folder.torrent

# 4. Verify that local files match the torrent perfectly
torrentcraft verify ./my-folder.torrent ./my-folder
```

### Tips for Scripts & Automation

When calling TorrentCraft in scripts or CI/CD pipelines:
- Append `--json` to receive structured, machine-parsable JSON output.
- Check the process exit code (`0` for success, non-zero for specific errors).
- Refer to [Output & Exit Codes](./output) for details.

## Configuration & Discovery

Both the CLI and GUI share the same configuration file (`torrentcraft.json`). By default, TorrentCraft automatically discovers configuration in this order:

1. `./torrentcraft.json` (the current working directory)
2. Platform-specific user configuration directory:
   - **Linux / macOS**: `~/.config/torrentcraft/torrentcraft.json`
   - **Windows**: `%APPDATA%\torrentcraft\torrentcraft.json`

For full configuration reference and preset management, see [Manage Configuration](./config).

## Working with File Paths

Paths can be relative (`./folder`) or absolute (`/data/folder` or `C:\Data\Folder`).

- **Spaces & Special Characters**: Always wrap paths in quotes if they contain spaces or special shell characters:
  ```bash
  torrentcraft create "./My Vacation Photos" -o "./My Vacation Photos.torrent"
  ```
- **Unicode Support**: TorrentCraft fully supports Unicode characters in file and directory paths across all platforms, including Windows PowerShell and Command Prompt.
