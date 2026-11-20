# Verify Content

Use `verify` to compare local files or directories against a `.torrent` file, recalculating piece hashes to ensure all data is complete and uncorrupted.

```text
torrentcraft verify <torrent> <content> [options]
```

## Practical Examples

```bash
# 1. Verify a downloaded folder against its torrent
torrentcraft verify ./payload.torrent ./payload

# 2. Output detailed per-file verification report in JSON
torrentcraft verify ./payload.torrent ./payload --json

# 3. Run multi-threaded verification with custom memory limits
torrentcraft verify ./payload.torrent ./payload \
  --verify-workers 4 \
  --verify-memory 64MiB
```

## Content Input Follows the Torrent File Layout

`verify` matches your `<content>` path against the file layout recorded in the torrent, rather than the number of files inside it:

- A **single-file** torrent expects `<content>` to point directly to that local file.
- A **directory** torrent expects `<content>` to point to the local folder that holds the torrent's files—*even if that folder contains only a single file*.

If the path type does not match the torrent's layout (for example, passing a folder to a single-file torrent, or passing an individual file inside a directory torrent), the command rejects it with error `verify.content_root` and exit code `3`. TorrentCraft never guesses, strips, or rewrites the path you provide. If the target path simply does not exist on disk yet, `verify` still completes normally and reports the files as *Missing* (exit code `6`).

Trailing path separators on `<content>` are normalized automatically (so `./payload/` and `./payload` behave identically), while passing the filesystem root itself (`/` or `C:\`) is rejected. Individual files inside the torrent are opened through standard OS path resolution, matching qBittorrent, libtorrent, and Transmission: symlinks pointing to regular files are followed and verified, dangling symlinks are reported as *Missing*, and special non-regular files (such as device nodes or sockets) fail validation with `not_regular_file`.

## Per-File Results & Scripting Behavior

Verification inspects data integrity per logical file:
- In terminal output, each file is labeled with its status (*Verified*, *Mismatch*, or *Missing*).
- **Automation & Exit Codes**: If any file fails verification or is missing, the command finishes its full report and exits with code `6`. In JSON mode, the envelope retains `ok: true` because the verification task completed successfully without crashing. This design makes CI/CD failure detection simple and robust:
  ```bash
  torrentcraft verify ./payload.torrent ./payload || echo "Integrity check failed!"
  ```

## Large Torrents & Output Optimization

- **Large File Lists**: In terminal mode, when a torrent contains more than 50 files, TorrentCraft automatically shows an aggregate summary and prints only the rows of failed or missing files, keeping your terminal uncluttered.
- **Progress Reporting**: `--progress <mode>` controls real-time hashing progress sent to `stderr` (`plain`, `tty`, or `json`). Use `--quiet` to suppress progress.

## Performance Tuning & Resource Limits

```bash
torrentcraft verify ./payload.torrent ./payload \
  --verify-workers 4 \
  --verify-memory 128MiB \
  --memory-working-set-limit 1GiB
```

- `--verify-workers N`: Number of parallel hashing worker threads (default: `1`). Increase on multi-core systems with fast NVMe drives.
- `--verify-memory SIZE`: Memory buffer allocated for block hashing (e.g. `64MiB`; default: `32MiB`).
- `--memory-working-set-limit SIZE`: Hard process memory working-set ceiling (default: `512MiB`; primarily on Windows).

## Option Reference

| Option | Description |
| --- | --- |
| `--config PATH` | Load verification settings from a custom config file. |
| `--verify-workers N` | Number of parallel hashing threads (default: 1). |
| `--verify-memory SIZE` | Memory buffer for verification hashing (default: 32 MiB). |
| `--memory-working-set-limit SIZE` | Limit process memory working set (default: 512 MiB). |
| `--progress MODE` | Progress reporting mode (`json`, `plain`, `tty`). |
| `--json` | Emit structured JSON output. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
