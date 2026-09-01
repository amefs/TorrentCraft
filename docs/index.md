# Overview

TorrentCraft is a modern, high-performance toolkit for creating, inspecting, verifying, and editing BitTorrent files. It is available both as a scriptable command-line interface and as an intuitive desktop application.

## Two Ways to Work

- **[CLI Guide](/cli/)** — A lightweight, non-interactive command-line tool (`torrentcraft`) built for terminal power users, automation scripts, batch processing, and CI/CD pipelines. Supports stable JSON output and shell autocompletion.
- **[Desktop GUI](/cli/gui)** — An interactive Qt application (`torrentcraft-gui`) featuring visual file selection, drag-and-drop support, real-time progress tracking, and visual configuration management.

Both applications share the same underlying core engine and canonical configuration file (`torrentcraft.json`), ensuring consistent behavior whether you work in the terminal or on the desktop.

## Core Capabilities

- **Create Torrents** — Build V1, V2, and hybrid torrents from files or directories, with automatic or customized piece sizing and flexible file ordering.
- **Inspect & Explore** — View torrent metadata, info hashes (SHA-1 / SHA-256), piece specs, and browse the logical file tree without needing the original data files.
- **Verify Content** — Check local files and folders against a torrent to ensure data integrity, with multi-threaded hashing and configurable memory limits.
- **Validate Structure** — Check torrent files for syntax correctness and BitTorrent specification compliance.
- **Edit Metadata & Trackers** — Safely update tracker tiers, comments, creators, web seeds, and identity fields with built-in backup and dry-run safeguards.
- **Unified Configuration & Presets** — Save reusable creation settings (e.g. tracker lists, piece sizes, private flags) as named presets for quick reuse.

## Quick Navigation

| What do you want to do? | Recommended starting point |
| --- | --- |
| Get started and run your first workflow | [Getting Started](/cli/getting-started) |
| Explore all CLI commands and options | [CLI Guide](/cli/) |
| Use the desktop interface | [Desktop GUI](/cli/gui) |
| Create a new torrent | [Create Torrents](/cli/create) |
| Inspect a torrent or view its file tree | [Inspect Torrents](/cli/inspect) · [Print File Tree](/cli/tree) |
| Verify local data against a torrent | [Verify Content](/cli/verify) |
| Add, remove, or update trackers | [Manage Trackers](/cli/tracker) |
| Edit torrent metadata | [Edit Metadata](/cli/metadata) |
| Manage presets and configuration | [Manage Presets](/cli/preset) · [Manage Configuration](/cli/config) |
