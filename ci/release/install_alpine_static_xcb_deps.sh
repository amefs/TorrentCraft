#!/bin/sh
set -eu

# Alpine 3.22 deliberately builds libXau and xcb-util without static archives.
# Qt's static XCB feature probe requires both archives, while libxcb.a also
# needs its pkg-config private dependencies (Xau and Xdmcp) at link time.

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/torrentcraft-static-xcb.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

download_and_verify() {
    url="$1"
    checksum="$2"
    archive="$work_dir/${url##*/}"

    curl --fail --location --retry 5 --output "$archive" "$url"
    echo "$checksum  $archive" | sha512sum -c -
}

libxau_version=1.0.12
libxau_checksum=4bbe8796f4a14340499d5f75046955905531ea2948944dfc3d6069f8b86c1710042bfc7918d459320557883e6631359d48e6173c69c62ff572314e864ff97c5e
xcb_util_version=0.4.1
xcb_util_checksum=da67f2f017d2a1788dcf35f28d6956e171303a622a1dd085cd3d69fdb2ed77965d83c557cc926ebf9b32e905eb2cbb5921987250192d78a2f5edc4d437ed7d2b

download_and_verify \
    "https://www.x.org/releases/individual/lib/libXau-$libxau_version.tar.xz" \
    "$libxau_checksum"
download_and_verify \
    "https://xorg.freedesktop.org/archive/individual/lib/xcb-util-$xcb_util_version.tar.xz" \
    "$xcb_util_checksum"

tar -xf "$work_dir/libXau-$libxau_version.tar.xz" -C "$work_dir"
meson setup "$work_dir/libxau-build" "$work_dir/libXau-$libxau_version" \
    --prefix=/usr/local \
    --libdir=lib \
    --buildtype=release \
    -Ddefault_library=static
meson compile -C "$work_dir/libxau-build"
meson install -C "$work_dir/libxau-build"

tar -xf "$work_dir/xcb-util-$xcb_util_version.tar.xz" -C "$work_dir"
(
    cd "$work_dir/xcb-util-$xcb_util_version"
    ./configure \
        --prefix=/usr/local \
        --libdir=/usr/local/lib \
        --disable-dependency-tracking \
        --disable-shared \
        --enable-static
)
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
make -C "$work_dir/xcb-util-$xcb_util_version" -j"$jobs"
make -C "$work_dir/xcb-util-$xcb_util_version" install

test -f /usr/local/lib/libXau.a
test -f /usr/local/lib/libxcb-util.a
test -f /usr/lib/libxcb.a
test -f /usr/lib/libXdmcp.a

# Keep libxcb's private dependencies attached for non-Qt CMake consumers that
# resolve the system archive through /usr/local. The Qt overlay separately
# propagates the same dependencies through its XCB::XCB imported target.
cat >/usr/local/lib/libxcb.a <<'EOF'
/* Static libxcb plus the private dependencies declared by xcb.pc. */
GROUP ( /usr/lib/libxcb.a /usr/local/lib/libXau.a /usr/lib/libXdmcp.a )
EOF

cat >"$work_dir/xcb-static-smoke.c" <<'EOF'
#include <xcb/xcb.h>

int main(void)
{
    int screen = 0;
    xcb_connection_t *connection = xcb_connect(0, &screen);
    xcb_disconnect(connection);
    return 0;
}
EOF
cc -static "$work_dir/xcb-static-smoke.c" /usr/local/lib/libxcb.a \
    -o "$work_dir/xcb-static-smoke"

cat >"$work_dir/find-static-xcb.cmake" <<'EOF'
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
list(PREPEND CMAKE_LIBRARY_PATH "/usr/local/lib")
find_library(static_xau NAMES Xau REQUIRED)
find_library(static_xcb NAMES xcb REQUIRED HINTS /usr/lib)
find_library(static_xcb_util NAMES xcb-util REQUIRED HINTS /usr/lib)
if(NOT static_xau STREQUAL "/usr/local/lib/libXau.a"
        OR NOT static_xcb STREQUAL "/usr/local/lib/libxcb.a"
        OR NOT static_xcb_util STREQUAL "/usr/local/lib/libxcb-util.a")
    message(FATAL_ERROR
        "CMake did not select the bootstrapped static XCB libraries: "
        "${static_xau};${static_xcb};${static_xcb_util}")
endif()
EOF
cmake -P "$work_dir/find-static-xcb.cmake"
