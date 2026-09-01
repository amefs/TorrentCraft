#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    printf 'usage: %s URL SHA512 DESTINATION\n' "$0" >&2
    exit 2
fi

source_url=$1
expected_sha512=${2,,}
destination=$3

case "$source_url" in
    https://ftpmirror.gnu.org/gnu/*)
        gnu_path=${source_url#https://ftpmirror.gnu.org/gnu/}
        ;;
    https://ftpmirror.gnu.org/*)
        gnu_path=${source_url#https://ftpmirror.gnu.org/}
        ;;
    https://ftp.gnu.org/pub/gnu/*)
        gnu_path=${source_url#https://ftp.gnu.org/pub/gnu/}
        ;;
    https://ftp.gnu.org/gnu/*)
        gnu_path=${source_url#https://ftp.gnu.org/gnu/}
        ;;
    *)
        exit 1
        ;;
esac

if [[ ! $expected_sha512 =~ ^[[:xdigit:]]{128}$ ]]; then
    printf 'invalid SHA-512 for %s\n' "$source_url" >&2
    exit 1
fi

mkdir -p -- "$(dirname -- "$destination")"
partial="${destination}.part"
trap 'rm -f -- "$partial"' EXIT

for mirror_root in \
    https://mirrors.kernel.org/gnu/ \
    https://mirrors.ocf.berkeley.edu/gnu/
do
    mirror_url="${mirror_root}${gnu_path}"
    if curl --fail --location --silent --show-error \
        --retry 2 --retry-delay 1 --retry-all-errors \
        --connect-timeout 20 --max-time 300 \
        --output "$partial" "$mirror_url"
    then
        actual_sha512=$(sha512sum "$partial")
        actual_sha512=${actual_sha512%% *}
        if [[ $actual_sha512 == "$expected_sha512" ]]; then
            mv -f -- "$partial" "$destination"
            trap - EXIT
            exit 0
        fi
        printf 'SHA-512 mismatch from %s\n' "$mirror_url" >&2
    fi
done

exit 1
