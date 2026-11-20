#!/usr/bin/env bash
# Runs each libFuzzer target for a fixed time, seeded from the repository.
# Usage: ci/run-fuzzers.sh BUILD_DIR [SECONDS]
# Crashing inputs are written to out/fuzz/ as <target>-crash-*; the script fails on the first one.
set -euo pipefail

build_dir=${1:?usage: ci/run-fuzzers.sh BUILD_DIR [SECONDS]}
seconds=${2:-60}
root=$(cd "$(dirname "$0")/.." && pwd)
work="$root/out/fuzz"
mkdir -p "$work"

seeds_for() {
    case "$1" in
    fuzz_torrent_decode) echo "$root/tests/fixtures/metadata" ;;
    fuzz_tracker_rewrite) echo "$root/tests/fuzz/seeds/tracker_rewrite" ;;
    fuzz_config_json) echo "$root/tests/fuzz/seeds/config_json" ;;
    *) return 1 ;;
    esac
}

for target in fuzz_torrent_decode fuzz_tracker_rewrite fuzz_config_json; do
    corpus="$work/corpus-$target"
    rm -rf "$corpus"
    mkdir -p "$corpus"
    echo "== $target (${seconds}s)"
    # The first directory receives new inputs; the seed directory is only read.
    "$build_dir/tests/fuzz/$target" "$corpus" "$(seeds_for "$target")" \
        -max_total_time="$seconds" -max_len=65536 -timeout=10 -rss_limit_mb=2560 \
        -print_final_stats=1 -artifact_prefix="$work/$target-"
done
