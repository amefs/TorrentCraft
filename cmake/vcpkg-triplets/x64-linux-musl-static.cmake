set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

# Linux X11 ports default to empty system packages. GTK3 needs X11 extension
# libraries, so force vcpkg to build their static archives for this triplet.
set(X_VCPKG_FORCE_VCPKG_X_LIBRARIES ON)

# gettext-libintl also defaults to an empty system package on Linux. Pango's
# Meson build then records Alpine's /usr/lib/libintl.so and fails when the
# static musl linker consumes that dynamic object. Build libintl in vcpkg so
# the target dependency resolves to the triplet's static archive instead.
set(X_VCPKG_FORCE_VCPKG_GETTEXT_LIBINTL ON)

# Qt's configure probes otherwise select shared X11/XCB objects and then fail
# when the musl triplet supplies -static to the linker. The Alpine release job
# installs its missing static Xau/xcb-util dependencies under /usr/local/lib.
#
# CMAKE_FIND_LIBRARY_SUFFIXES must be set after the platform toolchain has
# initialized its defaults; passing it only as a cache option is overwritten
# before Qt calls find_library(). CMAKE_PROJECT_INCLUDE provides that
# post-toolchain hook for every target port configured by vcpkg.
list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_PROJECT_INCLUDE:FILEPATH=${CMAKE_CURRENT_LIST_DIR}/../vcpkg-toolchains/linux-musl-static.cmake"
)

# This triplet is intentionally used only inside the pinned Alpine x86_64
# release job. Native Alpine compilers target musl; the vcpkg Linux toolchain
# adds -static for a static CRT linkage request.
