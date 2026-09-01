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
