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
