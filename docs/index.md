# Overview

TorrentCraft is a modern, high-performance toolkit for creating, inspecting, verifying, and editing BitTorrent files. It is available both as a scriptable command-line interface and as an intuitive desktop application.

## Two Ways to Work

- **[CLI Guide](/cli/)** — A lightweight, non-interactive command-line tool (`torrentcraft`) built for terminal power users, automation scripts, batch processing, and CI/CD pipelines. Supports stable JSON output and shell autocompletion.
- **[Desktop GUI](/cli/gui)** — An interactive Qt application (`torrentcraft-gui`) featuring visual file selection, drag-and-drop support, real-time progress tracking, and visual configuration management.

Both applications share the same underlying core engine and canonical configuration file (`torrentcraft.json`), ensuring consistent behavior whether you work in the terminal or on the desktop.

## Core Capabilities

- **Create Torrents** — Build V1, V2, and hybrid torrents from files or directories, with automatic or customized piece sizing, flexible file ordering, and opt-in filtering for common operating-system artifacts or custom Glob rules.
- **Inspect & Explore** — View torrent metadata, info hashes (SHA-1 / SHA-256), piece specs, and browse the logical file tree without needing the original data files.
- **Verify Content** — Check local files and folders against a torrent to ensure data integrity, with multi-threaded hashing and configurable memory limits.
- **Validate Structure** — Check torrent files for syntax correctness and BitTorrent specification compliance.
- **Edit Metadata & Trackers** — Safely update comments, creators, web seeds, identity fields, and tracker tiers, or batch-rewrite tracker URLs across folders while keeping each torrent's passkey intact.
- **Unified Configuration & Presets** — Save reusable creation settings (tracker lists, piece sizes, private flags, filter rules) as named presets, or import a preset directly from an existing `.torrent` file.

## Quick Navigation

| What do you want to do? | Recommended starting point |
| --- | --- |
| Get started and run your first workflow | [Getting Started](/cli/getting-started) |
| Explore all CLI commands and options | [CLI Guide](/cli/) |
| Use the desktop interface | [Desktop GUI](/cli/gui) |
| Create a new torrent | [Create Torrents](/cli/create) |
| Inspect a torrent or view its file tree | [Inspect Torrents](/cli/inspect) · [Print File Tree](/cli/tree) |
| Verify local data against a torrent | [Verify Content](/cli/verify) |
| Add, remove, replace, or rewrite tracker URLs (keeping passkeys) | [Manage Trackers](/cli/tracker) |
| Edit torrent metadata | [Edit Metadata](/cli/metadata) |
| Manage presets, import from a torrent, or edit configuration | [Manage Presets](/cli/preset) · [Manage Configuration](/cli/config) |
