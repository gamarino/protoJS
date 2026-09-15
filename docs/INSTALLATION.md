# protoJS Installation Guide

protoJS is installed by building it from source. No prebuilt packages or installers are published. The CMake build can produce native packages with CPack (see [Building packages](#building-packages)) for local use or for your own distribution.

protoJS is not production ready. The version configured in `CMakeLists.txt` is 0.1.0.

---

## Prerequisites

- **protoCore** — required. protoJS links against the protoCore shared library (`libprotoCore`). Build it from source: <https://github.com/numaes/protoCore>.
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

When `PROTO_CORE_PREFIX` is not set, CMake looks for `libprotoCore` only in `../protoCore/build` and `../protoCore/build_check`, and stops with the error "protoCore shared library not found" if neither contains it.

- The build type defaults to `Release` when `CMAKE_BUILD_TYPE` is not given.
- `cmake --build build` also builds the Catch2 unit-test executable and two test addons. Pass `-DBUILD_TESTING=OFF` at configure time to skip the unit tests, or build only the runtime with `cmake --build build --target protojs`.
- The executable is `build/protojs`. Its build-tree RPATH points at the protoCore build directory, so no `LD_LIBRARY_PATH` is needed:

```bash
./build/protojs --version        # prints: protoJS v0.1.0
./build/protojs -e "console.log('Hello, protoJS')"
```

### Building against an installed protoCore

To use a protoCore that is already installed under a prefix, set `PROTO_CORE_PREFIX`. CMake then searches for `libprotoCore` under `<prefix>/lib` or `<prefix>/lib64` and for `protoCore.h` under `<prefix>/include`:

```bash
cmake -S . -B build -DPROTO_CORE_PREFIX=/usr/local
cmake --build build
```

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
| Linux    | `DEB`, `RPM`, `TGZ`         | `protojs-0.1.0-Linux.deb`, `protojs-0.1.0-Linux.rpm`, `protojs-0.1.0-Linux.tar.gz` |
| macOS    | `DragNDrop`                 | `protojs-0.1.0-Darwin.dmg` |
| Windows  | `NSIS`, `ZIP`               | not buildable with the current build files (see [Platform support](#platform-support)) |

Notes:

- The package name is `protojs`. The packages contain only the `protojs` executable.
- The DEB package declares `Depends: protocore`, and the RPM package declares `Requires: protoCore`. Both names match the packages that protoCore's own CPack configuration produces (the DEB generator lowercases protoCore's package name). No minimum version is declared.
- Plain `cpack` runs every generator listed for the platform, and the RPM generator needs `rpmbuild`. Select generators with `-G` when a packaging tool is not installed.

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

---

## Troubleshooting

- **"protoCore shared library not found" at configure time** — build protoCore in `../protoCore/build` (or `../protoCore/build_check`), or configure with `-DPROTO_CORE_PREFIX=<prefix>` for an installed protoCore.
- **"PROTO_CORE_PREFIX=... set but protoCore not found"** — the prefix must contain `lib/libprotoCore` (or `lib64/`) and `include/protoCore.h`.
- **Linker errors mentioning `ssl` or `crypto`** — install the OpenSSL development package.
- **`error while loading shared libraries: libprotoCore...` at run time** — the executable was moved away from its RPATH layout, or protoCore is installed in another prefix. Set `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`, or install protoCore and protoJS under the same prefix.
- **Configure step fails while fetching Catch2** — install Catch2 v3 system-wide, allow network access, or configure with `-DBUILD_TESTING=OFF`.

For runtime problems see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
