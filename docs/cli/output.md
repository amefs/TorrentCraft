# Output & Exit Codes

TorrentCraft is designed for reliable terminal usability, clean stream redirection, and robust automation in CI/CD pipelines.

## Standard Output & Stream Separation

- **Standard Output (`stdout`)**: Dedicated exclusively to command results, summaries, and machine-readable JSON data.
- **Standard Error (`stderr`)**: Dedicated to real-time progress bars, diagnostic logs, and status messages.

This separation ensures that scripts can redirect or pipe results cleanly without contamination from progress bars:

```bash
# Save pure summary text without progress lines
torrentcraft inspect ./payload.torrent > summary.txt

# Capture progress logs separately
torrentcraft create ./payload -o ./payload.torrent --progress=plain 2> progress.log
```

## Structured JSON Output (`--json`)

Adding `--json` produces structured JSON envelopes:

### Success Response:
```json
{
  "ok": true,
  "data": { ... }
}
```

### Error Response:
```json
{
  "ok": false,
  "error": {
    "code": "input_validation_error",
    "message": "Detailed description of what went wrong",
    "fields": { ... }
  }
}
```

If a filesystem path contains non-UTF-8 bytes (for example on Linux), invalid byte sequences are replaced with `U+FFFD` when serializing JSON, ensuring `--json` output is always valid UTF-8 and never fails after an operation has already finished.

## File-filter reports

For a directory create, both a successful write and `--dry-run` expose the entries excluded
by the effective file filter. Human-readable output lists the relative path, entry kind, matched
rule, and byte summary. A matched directory is one aggregate record with descendant counts and
bytes; its descendants are not repeated.

In `--json` output, the create result includes `data.filtered.count`,
`data.filtered.bytes`, and `data.filtered.entries`. An empty report means no entries were
filtered. The report is informational and never deletes or modifies source files.

> **Note on Verification**: In [`verify`](./verify), detecting mismatched or missing files is considered a successfully completed report (`ok: true`), but the process exits with code `6` so shell scripts can instantly detect integrity failures.

## Progress Modes (`--progress`)

Supported by `create` and `verify`:
- `plain`: Emits one clean text progress line at a time (ideal for CI logs).
- `tty`: Displays an interactive terminal progress bar (automatically active when stderr is a terminal).
- `json`: Emits newline-delimited JSON (NDJSON) progress events for integration into third-party UIs.

## Process Exit Codes

TorrentCraft uses stable, standardized exit codes categorized similarly to HTTP status codes:

| Exit Code | Meaning | Typical Cause |
| ---: | --- | --- |
| `0` | **Success** | The operation completed successfully. |
| `2` | **Usage Error** | Invalid flags, missing required arguments, or unknown subcommands. |
| `3` | **Validation Error** | File failed syntax validation, invalid JSON value, or corrupt header. |
| `4` | **Not Found** | Input file, directory, or config path does not exist. |
| `5` | **Permission Error** | Insufficient permissions to read input or write to target path. |
| `6` | **Conflict / Mismatch** | Target file exists (without `--overwrite`), or verification found corrupted files. |
| `7` | **Cancelled** | Operation was cancelled by user signal or GUI prompt. |
| `8` | **I/O Error** | Disk read/write failure or unreadable file system block. |
| `9` | **Resource Limit** | Process memory limit exceeded or worker budget exhausted. |
| `10` | **Internal Error** | Unhandled internal exception or unsupported operation. |

## Cancelling with Ctrl+C

Pressing **Ctrl+C** during `create`, `verify`, or `tracker rewrite` stops the operation cleanly at the next safe checkpoint, removes any temporary output files, and exits with code `7`. An interrupted `tracker rewrite` keeps the files it has already finished updating and includes `"cancelled": true` (with `"ok": false`) in `--json` output. Press **Ctrl+C** a second time to force an immediate exit.

- **Interactive Windows terminals**: Always run CLI commands with `torrentcraft.exe`. Although `torrentcraft-gui.exe` also accepts CLI arguments, it is linked as a GUI subsystem binary—the shell returns to the prompt without waiting for it and does not forward Ctrl+C. If you do run a command through `torrentcraft-gui.exe`, press **Ctrl+Break** to cancel (in PowerShell with PSReadLine, Ctrl+Break also enters the script debugger; type `c` and press Enter to leave it).
- **Calling from scripts on Windows**: Automation scripts (such as Python `subprocess`) should always invoke `torrentcraft.exe`. Because `torrentcraft-gui.exe` reattaches its streams to the console window, pipe capture and `--json` parsing are not reliable with the GUI binary. To cancel a running `torrentcraft.exe` process from a script on Windows, launch it in a new process group (`CREATE_NEW_PROCESS_GROUP`), send `CTRL_BREAK_EVENT`, and wait for exit code `7`.
