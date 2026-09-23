# protoJS Installation Guide

protoJS is installed by building it from source. No prebuilt packages or installers are published. The CMake build can produce native packages with CPack (see [Building packages](#building-packages)) for local use or for your own distribution.

protoJS is not production ready. The version configured in `CMakeLists.txt` is 0.1.0.

---

## Prerequisites

- **protoCore 2.0.0 or newer**, installed, with its CMake package configuration — required. protoJS links against the protoCore shared library (`libprotoCore.so.2`) and never bundles it. Build and install it from source: <https://github.com/numaes/protoCore>; see its `docs/INSTALLATION.md`.
- **CMake** 3.16 or newer.
- **A C++20 and C99 compiler.**
- **OpenSSL development files.** The runtime links `ssl` and `crypto` directly (for example the `libssl-dev` package on Debian/Ubuntu or `openssl-devel` on Fedora).
- **POSIX system libraries.** The build links `pthread`, `dl` and `m` and passes `-rdynamic` to the linker.
- **Git and network access at configure time** when tests are enabled (the default) and Catch2 is not installed: CMake then downloads Catch2 v3.5.2 with `FetchContent`.
- **Node.js** — only for the test and benchmark runner scripts under `tests/`; it is not needed to build or run protoJS.

### Platform support

protoJS is developed on Linux. `CMakeLists.txt` contains macOS settings (install RPATH, a DragNDrop CPack generator), but macOS builds are not verified. The build files do not support MSVC or Windows: they link POSIX libraries and use GCC/Clang linker flags. The repository has no continuous-integration configuration.

---

## Building from source

protoJS expects protoCore in a sibling directory, so that both repositories share a parent directory:

```
<parent>/
├── protoCore/
└── protoJS/
```

```bash
# 1. Build the protoCore shared library
git clone https://github.com/numaes/protoCore.git
cmake -S protoCore -B protoCore/build
cmake --build protoCore/build --target protoCore

# 2. Build protoJS
git clone https://github.com/gamarino/protoJS.git
cd protoJS
cmake -S . -B build
cmake --build build
```

protoJS prefers an **installed protoCore CMake package**: the configure step runs `find_package(protoCore 2.0 CONFIG)` first, and it is the only discovery mode that checks protoCore's version and ABI. The version floor is `2.0` and the ceiling is the next major version, because protoCore's major version and its soname move together; protoJS additionally asserts that the package's `SOVERSION` is `2`.

When no installed package is found *and* no prefix was named, CMake falls back to a sibling build directory of protoCore, in this order:

1. `../protoCore/build_release`
2. `../protoCore/build`
3. `../protoCore/build_check`

The first directory that holds the library wins, and CMake stops with the error "protoCore shared library not found" if none of them does. The fallback prints a `WARNING`: it performs no package version check (it does require `libprotoCore.so.2` beside the library it found) and must not be used to produce a distributable package. Pass `-DPROTOCORE_REQUIRE_PACKAGE=ON` to turn it into a hard error; **every packaging build sets it**. `build_release` is searched first because it is the directory protoCore's release workflow writes to: if a stale `../protoCore/build` is left over from an earlier checkout, protoJS would otherwise link against it silently, and the mismatch only shows up later as a run-time crash or a missing symbol. The chosen path is printed at configure time as `-- Found protoCore: <path>`; check that line when a build behaves as though protoCore changes had not landed.

- The build type defaults to `Release` when `CMAKE_BUILD_TYPE` is not given.
- `cmake --build build` also builds the Catch2 unit-test executable and two test addons. Pass `-DBUILD_TESTING=OFF` at configure time to skip the unit tests, or build only the runtime with `cmake --build build --target protojs`.
- The executable is `build/protojs`. Its build-tree RPATH points at the protoCore build directory, so no `LD_LIBRARY_PATH` is needed:

```bash
./build/protojs --version        # prints: protoJS v0.1.0
./build/protojs -e "console.log('Hello, protoJS')"
```

### Building against an installed protoCore

Name the prefix with `PROTO_CORE_PREFIX` or `CMAKE_PREFIX_PATH`; both are added to CMake's package search path:

```bash
cmake -S . -B build -DPROTO_CORE_PREFIX=$HOME/.local -DPROTOCORE_REQUIRE_PACKAGE=ON
cmake --build build
```

The prefix must hold `lib/cmake/protoCore/protoCoreConfig.cmake`. **A prefix holding only `libprotoCore` and `protoCore.h` is no longer accepted**: without the package configuration there is no way to tell protoCore 1.x from 2.x, and linking the wrong major version is silent.

Switching a build directory between the two modes leaves a stale `PROTOCORE_LIBRARY` cache entry; delete the build directory rather than reconfiguring in place.

### Installing

The install rule installs a single file, the `protojs` executable, into `<prefix>/bin` (the standard modules are compiled into the binary). The installed executable carries the RPATH `$ORIGIN/../<libdir>` (`@executable_path/../<libdir>` on macOS), so it finds `libprotoCore` when protoCore is installed under the same prefix.

```bash
# Default prefix (/usr/local)
sudo cmake --install build

# User-local prefix, no root required; add $HOME/.local/bin to PATH
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build
cmake --install build
```

If protoCore is installed in a different prefix, set `LD_LIBRARY_PATH` (Linux) or `DYLD_LIBRARY_PATH` (macOS) so the loader can find `libprotoCore`.

---

## Building packages

`CMakeLists.txt` configures CPack. After building, run `cpack` from the build directory:

```bash
cmake --build build
cd build
cpack -G DEB      # or: cpack -G RPM, cpack -G TGZ, or plain cpack for every configured generator
```

| Platform | CPack generators configured | Output (CPack default naming) |
|----------|-----------------------------|-------------------------------|
| Linux    | `TGZ`, plus `DEB` when `dpkg` is found and `RPM` when `rpmbuild` is found | `protojs-0.1.0-Linux.deb`, `protojs-0.1.0-Linux.rpm`, `protojs-0.1.0-Linux.tar.gz` |
| macOS    | `DragNDrop`                 | `protojs-0.1.0-Darwin.dmg` |
| Windows  | `NSIS`, `ZIP`               | not buildable with the current build files (see [Platform support](#platform-support)) |

Notes:

- The package name is pinned explicitly rather than left to each generator's default casing: `protojs` for DEB, `protoJS` for RPM. The packages contain only the `protojs` executable.
- Both declare a bounded dependency on protoCore's own package: `Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)` for DEB, `Requires: protoCore >= 2.0.0, protoCore < 3.0.0` for RPM. The names match the packages protoCore's own CPack configuration produces, which pins them too.
- The DEB and RPM generators are enabled **only when their tools are present**. `cpack` runs every configured generator in one pass and a missing tool is fatal, not a skip, so an unconditional `DEB;RPM;TGZ` made `cpack` fail outright on a host without `rpmbuild` — taking the DEB and the TGZ down with it. Each configure prints whether a generator was enabled or disabled, and why.

Installing and removing a locally built Debian package (install the protoCore package first):

```bash
sudo dpkg -i protojs-0.1.0-Linux.deb
protojs --version
sudo apt remove protojs
```

Installing and removing a locally built RPM package:

```bash
sudo rpm -ivh protojs-0.1.0-Linux.rpm
protojs --version
sudo rpm -e protojs
```

The `packaging/` directory also holds hand-maintained installer templates and a `.deb` build script that are independent of CPack. See [packaging/PROCEDURES.md](../packaging/PROCEDURES.md).

### Two packaging paths, two package names

This repository can produce **two** different Debian packages, and they must not be installed at the same time — both claim `/usr/bin/protojs`, and `dpkg` treats them as unrelated packages:

| Path | Package name | Built by |
|------|--------------|----------|
| CPack | `protojs` | `cpack -G DEB` in the build directory |
| Templates | `protoJS` | `packaging/build_deb.sh`, from `packaging/templates/linux/` |

The template path adds a `preinst` script that checks the installed protoCore's version is in `[2.0.0, 3.0.0)` **and** that the package actually provides the `libprotoCore.so.2` soname — the package version is metadata, the soname is what the loader will use. `build_deb.sh` also refuses to build a package whose binary is not linked against that soname, and it stages into `build_release/` rather than the repository root.

### Platform verification status

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux | CPack TGZ/DEB/RPM as `protojs`; `packaging/build_deb.sh` as `protoJS` | Both `.deb` files built; extracted and smoke-tested from the package payload |
| macOS | `packaging/templates/macos/preinstall.template`, CPack DragNDrop | Configured and reviewed, **never built** — no macOS host |
| Windows | `packaging/templates/windows/protoJS.wxs.template` (WiX v3), CPack NSIS/ZIP | Configured and reviewed, **never built** — no Windows host |

RPM packaging is configured and reviewed but **never executed**: `rpmbuild` is not installed on the host this was verified on.

---

## Troubleshooting

- **"protoCore shared library not found" at configure time** — build protoCore in `../protoCore/build_release` (or `../protoCore/build`, or `../protoCore/build_check`), or configure with `-DPROTO_CORE_PREFIX=<prefix>` for an installed protoCore.
- **protoJS behaves as though a protoCore change had not landed** — a stale sibling build directory earlier in the search order was picked. Re-read the `-- Found protoCore: <path>` line from the configure output, and remove the stale directory or configure with `-DPROTO_CORE_PREFIX=<prefix>`.
- **"No protoCore >= 2.0.0 CMake package was found under ..."** — the prefix must contain `lib/cmake/protoCore/protoCoreConfig.cmake`, which protoCore's own install rules emit. A prefix holding only `lib/libprotoCore` and `include/protoCore.h` is deliberately refused: it cannot be told apart from a protoCore 1.x.
- **Linker errors mentioning `ssl` or `crypto`** — install the OpenSSL development package.
- **`error while loading shared libraries: libprotoCore...` at run time** — the executable was moved away from its RPATH layout, or protoCore is installed in another prefix. Set `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`, or install protoCore and protoJS under the same prefix.
- **Configure step fails while fetching Catch2** — install Catch2 v3 system-wide, allow network access, or configure with `-DBUILD_TESTING=OFF`.

For runtime problems see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
