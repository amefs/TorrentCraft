# TorrentCraft

TorrentCraft is a C++17 toolkit for reading, creating, editing, validating, and verifying
BitTorrent metadata. It provides:

- the dependency-free public domain and foundation model;
- the TorrentUtils Core SDK and installable CMake package;
- a Qt-free `torrentcraft` command-line application;
- a Qt Widgets `torrentcraft-gui` desktop application.

## Features

- V1, V2, and Hybrid torrent metadata.
- Safe logical paths, metadata editing, tracker tiers, web seeds, and BEP 47 links.
- Deterministic file ordering and piece-length policies.
- Atomic creation and save operations.
- Shared configuration and presets for CLI and GUI.
- English and Simplified Chinese user interfaces.
- Static Linux musl and Windows release builds.

## Requirements

- CMake 3.25 or newer
- Ninja
- Clang 16+ or GCC 12+ on Linux
- MSVC 2022 on Windows
- vcpkg at the baseline recorded in `vcpkg.json`

Set `VCPKG_ROOT` to a vcpkg checkout before using the presets:

~~~bash
export VCPKG_ROOT=/path/to/vcpkg
~~~

## Build and test

The normal development build uses the CMake presets:

~~~bash
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
~~~

Other presets cover Clang and GCC release builds, sanitizers, coverage, the Qt GUI,
and MSVC builds on Windows. The presets enable warnings as errors and include the
install-tree consumer test.

To install the SDK:

~~~bash
cmake --preset linux-clang-release
cmake --build --preset linux-clang-release
cmake --install out/build/linux-clang-release \
    --prefix out/install/linux-clang-release
~~~

A downstream CMake project can consume the installed package:

~~~cmake
find_package(TorrentUtilsCore 1.0.0 EXACT CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE TorrentUtils::Core)
~~~

The public umbrella header is:

~~~cpp
#include <torrentutils/core/core.hpp>
~~~

## Command-line and GUI documentation

Start with the [CLI getting-started guide](docs/cli/getting-started.md).
The [GUI guide](docs/cli/gui.md) covers creation, inspection, verification,
metadata, tracker editing, configuration, presets, drag-and-drop, and system
font and icon integration.

The documentation site can be built locally with Node.js:

~~~bash
npm install
npm run docs:build
~~~

## Static release builds

The release workflow builds and validates static Linux musl and Windows packages.
It also produces checksums, provenance, dependency license inventories, SBOMs,
security reports, and Qt LGPL relinking materials. Release helpers are kept under
[ci/release](ci/release), while the public workflow definitions are under
[.github/workflows](.github/workflows).

Static releases are published on the repository's Releases page. The Linux and
Windows CLI/GUI executables are the only flat assets. Each platform also has one
support bundle containing its compliance materials, test results, source archive,
provenance, SBOM, security reports, and `SHA256SUMS`. Download the two executables
and the matching platform bundle, extract the bundle beside them, and run
`sha256sum -c SHA256SUMS` (or the equivalent PowerShell hash check) before
installing a release.

## Project vocabulary

Stable API and configuration terminology is documented in [CONTEXT.md](CONTEXT.md).
The implementation deliberately keeps libtorrent, Boost, Qt, JSON, and bencode
implementation types behind the public target boundary.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local checks and contribution guidance.
Pull requests are expected to pass formatting, static analysis, documentation,
build, install-consumer, and functional test checks.

## Acknowledgements

TorrentCraft is inspired by the design and user experience of [qBittorrent](https://github.com/qbittorrent/qBittorrent) and [TorrentUtils](https://github.com/airium/TorrentUtils). It uses [libtorrent](https://github.com/arvidn/libtorrent) as its BitTorrent engine. We thank their authors and contributors for the ideas, code, and open-source libraries that helped make this project possible.

## License

TorrentCraft is licensed under the MIT License. See [LICENSE](LICENSE).
