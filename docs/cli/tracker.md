# Manage Trackers

The `tracker` command allows you to view, add, remove, or completely replace tracker announce tiers in existing `.torrent` files.

```text
torrentcraft tracker list|add|remove|replace <torrent> ... [options]
torrentcraft tracker rewrite <torrent|folder>... -e RULE [-e RULE]... [options]
```

Changes are written directly to the target `.torrent` file. Use `--dry-run` to preview edits safely or `--backup` to automatically create a `.bak` copy before saving.

## 1. List Trackers (`list`)

```bash
# View trackers grouped by tier in human-readable format
torrentcraft tracker list ./payload.torrent

# Output structured JSON
torrentcraft tracker list ./payload.torrent --json
```

## 2. Add a Tracker (`add`)

```bash
# Add a tracker to Tier 1
torrentcraft tracker add ./payload.torrent \
  'https://tracker.example/announce' --tier 1
```

- If `--tier` is omitted, Tier 0 (primary) is used.
- New tiers can be created at the next sequential tier index.

## 3. Remove a Tracker (`remove`)

Remove a tracker by specifying its zero-based tier index and tracker index within that tier:

```bash
# Remove the first tracker (index 0) from Tier 1
torrentcraft tracker remove ./payload.torrent 1 0
```

> **Tip**: Run `torrentcraft tracker list ./payload.torrent` first to see the exact tier and index numbers.

## 4. Replace All Trackers (`replace`)

Completely overwrite the existing tracker list with a new set of tiers:

```bash
torrentcraft tracker replace ./payload.torrent \
  --tracker 'https://primary.example/announce' --tier 0 \
  --tracker 'https://backup.example/announce' --tier 1 \
  --backup
```

- Tier indexes range from `0` to `64`.
- All URLs are validated for correct URI syntax before writing.

## 5. Rewrite Tracker URLs (`rewrite`)

Update each torrent's own tracker URLs using sed-style rules instead of replacing the entire tracker list. This is ideal when a private tracker changes its domain or protocol and you want to preserve each torrent's unique passkey:

```bash
# Preview the change for every torrent in a folder and its subfolders
torrentcraft tracker rewrite ./torrents -r \
  -e 's#^(https?://)tracker\.old\.org(/.*)$#\1tracker.new.org\2#' --dry-run

# Apply it, writing the results below ./converted (subfolders are mirrored)
torrentcraft tracker rewrite ./torrents -r \
  -e 's#tracker\.old\.org#tracker.new.org#g' --output-dir ./converted
```

Rule syntax: `s<d>pattern<d>replacement<d>flags`

- `<d>` can be any punctuation character except a backslash (`/`, `#`, `|`, etc.). Escape it as `\<d>` if it appears literally inside the pattern or replacement.
- `pattern` uses [RE2 regular expression syntax](https://github.com/google/re2/wiki/Syntax) and matches anywhere in the URL. Capture groups, named groups `(?P<name>...)`, non-capturing groups, and `(?i)` are supported; backreferences inside the pattern and lookaround assertions are not.
- In `replacement`, use `\0`–`\9` or `&` to insert the full match or a capture group, and `\&` or `\\` for a literal `&` or backslash.
- Flags: `g` replaces all matches in the URL (default replaces only the first); `i` makes matching case-insensitive.
- Multiple `-e` rules run in order, each feeding its result into the next. The final URL is validated after all rules have run.

How batch rewriting behaves:

- **Inputs**: Pass individual `.torrent` files or folders. Folders scan top-level `*.torrent` files by default, or all nested subfolders with `-r` / `--recursive` (directory symlinks are not followed).
- **Output & In-Place Edits**: Without `--output-dir`, torrents are updated in place (add `--backup` to keep a `.bak` copy). With `--output-dir`, existing files in the destination are treated as conflicts unless `--force` is specified.
- **Per-Torrent Isolation**: If a rewritten URL is invalid (or a tracker URL in the torrent already exceeds the maximum length), only that torrent fails and remains unwritten while the rest of the batch continues. Duplicate URLs created within the same tier are merged (keeping the first occurrence). Torrents where no URL matched are reported as `unchanged` and are not rewritten on disk.
- **Output & Exit Codes**: Terminal output prints `old -> new` for every modified URL followed by a batch summary. `--json` returns `data.files[]` (`path`, `status`, `changes[]`, `merged`, `error`) and `data.summary`, setting `ok: false` if any file failed. Exit code is `0` when all torrents succeeded (changed or unchanged), `2` for invalid rules or flags, or the highest per-file exit code on failure (`3` for invalid resulting URLs, `6` for file conflicts, `7` if cancelled).
- **Passkey Redaction in Errors**: Rule syntax errors identify the rule by its 1-based position (`rule[1]`) and never echo the rule text back to the console or logs, protecting any passkeys in your expressions. Rewritten URLs are shown in full during previews.
- **Safe Cancellation**: Pressing Ctrl+C stops the batch cleanly after the current file, keeps any torrents already written, and exits with code `7` (see [Cancelling with Ctrl+C](./output#cancelling-with-ctrl-c)).

## Safe Controlled Editing

Tracker updates are strictly controlled and non-destructive. Modifying tracker tiers updates the announce dictionary without touching file boundaries or content hashes.

## Option Reference

| Option | Description |
| --- | --- |
| `-e`, `--expression RULE` | Add a sed-style rewrite rule (`rewrite`; repeatable). |
| `-r`, `--recursive` | Include subfolders of folder inputs (`rewrite`). |
| `--output-dir DIR` | Write rewritten torrents below `DIR`, mirroring subfolders (`rewrite`). |
| `--force` | Overwrite existing files in `--output-dir` (`rewrite`). |
| `--dry-run` | Validate and preview changes without modifying the file. |
| `--backup` | Create a `.bak` backup copy before writing. |
| `--json` | Emit structured JSON output. |
| `--quiet` | Suppress successful human-readable output. |
| `-h`, `--help` | Show command help. |
