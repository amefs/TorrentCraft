# Windows MSVC toolchain image

This directory defines the reproducible Windows C++17 build environment used by
the release workflow. It is a build environment, not a runtime image.

## Contents

- Visual Studio 2022 Build Tools with the x64/x86 MSVC toolset;
- the Windows SDK;
- Visual Studio's CMake and Ninja integration;
- a pinned vcpkg checkout;
- MinGit for vcpkg source acquisition.

\`Install-Toolchain.cmd\` installs the pinned vcpkg checkout and Visual Studio
components. \`Run-Msvc.cmd\` initializes the amd64 developer environment and adds
CMake, Ninja, Git, and vcpkg to \`PATH\`.

## Build policy

Pass \`BASE_IMAGE\` as a digest-pinned Windows Server Core image. Keep the base
image and toolchain versions under review when updating the build environment.
Build the image with process isolation on a compatible Windows host.

Do not put access tokens, Docker authentication files, or project source into the
build context or image layers. Credentials belong in the CI secret store and must
never be included in cache paths or release archives.

## Output encoding

\`Run-Msvc.cmd\` switches the container session to UTF-8 and sets \`VSLANG=1033\`.
It does not modify the host's global locale.
