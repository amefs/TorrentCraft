#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 VCPKG_ROOT" >&2
    exit 2
fi

vcpkg_root=$1
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
patch_file="$script_dir/../cmake/vcpkg-patches/qtbase-glib-static-linkage.patch"

git -C "$vcpkg_root" apply --check --unidiff-zero --recount "$patch_file"
git -C "$vcpkg_root" apply --unidiff-zero --recount "$patch_file"

overlay_root="$vcpkg_root/overlays"
for port in qtbase glib harfbuzz gtk3; do
    port_overlay="$overlay_root/$port"
    mkdir -p "$port_overlay"
    cp -a "$vcpkg_root/ports/$port/." "$port_overlay/"
done

printf 'prepared patched Qt/GLib static linkage overlays at %s\n' "$overlay_root"
