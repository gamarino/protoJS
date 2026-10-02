# protoJS Installation Guide

protoJS is installed by building it from source. No prebuilt packages or installers are published. The CMake build can produce native packages with CPack (see [Building packages](#building-packages)) for local use or for your own distribution.

protoJS is not production ready. The version configured in `CMakeLists.txt` is 0.1.0.

---

## Prerequisites

- **protoCore 2.7.0 or newer**, installed, with its CMake package configuration — required. 2.7.0 is the first release that declares `proto::proto_long` / `proto::proto_ulong`, the spelling of protoCore's 64-bit integers protoJS uses (they are `long` / `unsigned long` on Linux and macOS, so the ABI is the one 2.0 had). protoJS links against the protoCore shared library and never bundles it. Build and install it from source: <https://github.com/numaes/protoCore>; see its `docs/INSTALLATION.md`.
- **CMake** 3.16 or newer.
- **A C++20 and C99 compiler**: GCC or Clang, or MSVC 19.44 (Visual Studio 2022) on Windows.
- **OpenSSL development files.** The runtime links `ssl` and `crypto` directly (for example the `libssl-dev` package on Debian/Ubuntu or `openssl-devel` on Fedora).
- **POSIX system libraries.** The build links `pthread`, `dl` and `m` and passes `-rdynamic` to the linker (on Windows: Winsock, `iphlpapi` and `psapi`; see [Windows (MSVC)](#windows-msvc)).
- **Git and network access at configure time** when tests are enabled (the default) and Catch2 is not installed: CMake then downloads Catch2 v3.5.2 with `FetchContent`.
- **Node.js** — only for the test and benchmark runner scripts under `tests/`; it is not needed to build or run protoJS.

### Platform support

protoJS is developed on Linux. It also builds and runs natively on Windows with MSVC (see [Windows (MSVC)](#windows-msvc)) and on macOS with Apple clang. CI builds and tests all three (`.github/workflows/ci.yml` on Linux, `cross-platform.yml` on macOS 14 arm64 and Windows Server 2022); the macOS DragNDrop package is not built by CI.

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

protoJS prefers an **installed protoCore CMake package**: the configure step runs `find_package(protoCore 2.7 CONFIG)` first, and it is the only discovery mode that checks protoCore's version and ABI. The version floor is `2.7` and the ceiling is the next major version, because protoCore's major version and its soname move together; protoJS additionally asserts that the package's `SOVERSION` is `3`.

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

## Windows (MSVC)

protoJS builds and runs natively on Windows with Visual Studio 2022 (MSVC
19.44 verified, Windows 11), using the CMake and Ninja that ship with it.
Build protoCore 2.7.0 or newer first (its `docs/INSTALLATION.md`, "Windows
(MSVC)") and install it into a prefix: on Windows protoJS builds against an
installed protoCore package only, because the sibling-tree fallback finds the
library by its soname, which a DLL does not have. From an "x64 Native Tools
Command Prompt":

```bat
set PREFIX=%LOCALAPPDATA%\Programs\proto
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=%PREFIX% -DCMAKE_INSTALL_PREFIX=%PREFIX% ^
      "-DOPENSSL_ROOT_DIR=C:/Program Files/OpenSSL-Win64"
cmake --build build
ctest --test-dir build -j8
cmake --install build
%PREFIX%\bin\protojs --version
```

Any OpenSSL for Windows with headers, import libraries and DLLs works as
`OPENSSL_ROOT_DIR`. The DLL names follow from the version found
(`libcrypto-3-x64.dll` and `libssl-3-x64.dll` for OpenSSL 3), and configure
fails if they are not in the installation's `bin`. The build copies the DLLs
protojs needs (protoCore's -- `protoCore.dll`, or `protoCore-3.dll` from
protoCore 2.9.0 on -- and OpenSSL's) into `build/bin/`, so `protojs.exe` and
the tests run in place.

`cmake --install` (and `cpack`) put in `<prefix>/bin` `protojs.exe`, the
protoCore DLL (taken from the imported `protoCore::protoCore` target, whatever
it is named), the OpenSSL DLLs and the Visual C++ runtime DLLs
(`InstallRequiredSystemLibraries`, app-local, so the VC++ redistributable need
not be installed); `protojs.lib` (for native addons) goes in `<prefix>/lib` and
OpenSSL's licence in `<prefix>/share/doc/protojs/OpenSSL-LICENSE.txt` (the
distribution's own file, or for OpenSSL 3 the Apache License 2.0 copy in
`packaging/windows/`; `-DPROTOJS_OPENSSL_LICENSE_FILE=` names another). The
installed `bin` is therefore self-contained. `cpack -G ZIP` produces
`protojs-<version>-win64.zip` with that layout; an NSIS installer is added
when `makensis` is on `PATH`.

How Windows differs, by design:

- **Same output bytes everywhere.** The standard streams are binary, so
  `console.log` writes `\n` as on Linux, and the console is switched to UTF-8.
  Script and module sources and `fs` data are read and written in binary mode.
- **UTF-8 throughout.** `protojs.exe` carries a manifest that makes UTF-8 the
  process code page (Windows 10 1903 or later), so arguments, environment
  variables and file names with non-ASCII characters work as on Linux.
- **Paths.** A script may be named with a drive letter and either separator
  (`C:\dir\main.js`, `C:/dir/main.js`); `require` resolves relative modules
  from it, and module identities use `/`. `process.platform()` is `win32`.
- **Native addons** are `.dll` files. They link `protojs.lib` and
  `protoCore.lib`, and export their module information with
  `PROTOJS_ADDON_EXPORT` (`src/native/NativeModuleABI.h`):
  `extern "C" PROTOJS_ADDON_EXPORT ProtoJSNativeModuleInfo protojs_native_module_info(...)`.
  The macro is empty on Linux and macOS.
- **The REPL** reads a console with `ReadConsoleW` (UTF-16, converted to
  UTF-8), so non-ASCII input works whatever the console code page, and drops
  the `\r\n` line terminator, so `.exit` and blank lines work. The console's
  input and output code pages are switched to UTF-8 while protojs runs and
  restored when it exits.
- **`path`** is `path.win32`, as in Node: `\` separator, `;` delimiter, drive
  letters and UNC roots, and `resolve` consulting the per-drive current
  directories (`=C:` variables). `path.posix` is available as on every
  platform.
- **Module identity.** A module is identified by its canonical path
  (`GetFinalPathNameByHandleW`): symbolic links and junctions resolved and each
  component in the case the file system stores, so `./lib/a.js` and
  `./LIB/A.js` are one module, for `require` and `import` alike. (On macOS,
  as in Node, two spellings of a file on a case-insensitive volume remain two
  modules.)
- **Child processes.** `child_process.spawn` starts the program with
  `CreateProcessW`, and `exec` runs the line through `%ComSpec% /c`. A child
  inherits the standard handles and nothing else
  (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), and sockets are created
  non-inheritable, so a child never holds a server's port. `kill(0)` only
  checks that the child runs; any other signal terminates it. protoJS keeps
  each child's process handle until it exits itself, so a child's id cannot be
  reused under a later `kill()`. `cluster.fork()` returns `undefined`: there
  is no `fork()`.
- **Sockets** are Winsock. A server does not set `SO_REUSEADDR`, which on
  Windows would let a second server take a port already in use.
- **Event loop.** Windows wakes sleeping threads on a 15.6 ms timer by
  default; while the event loop waits for work protojs sets it to 1 ms
  (`timeBeginPeriod`) and restores it when the loop ends.
- **Stack.** `protojs.exe` reserves a 64 MiB stack, and every thread that
  runs JavaScript -- `worker_threads` workers, and threads protoCore creates --
  gets the same reservation (workers are created with it explicitly; protoJS
  asks protoCore for it with `setThreadStackBytes`, which protoCore honours on
  Windows from 2.9.0, and a thread created without a size gets the
  executable's reservation anyway). MSVC gives the
  interpreter's `runBytecode` a frame of about 46 KiB (GCC: 7.7 KiB), so
  64 MiB allows about 1,400 nested JavaScript calls where Linux's 8 MiB allows
  about 1,000. As on Linux, going deeper ends the process instead of raising
  a `RangeError`. The frame size and what it costs per call are discussed in
  [PERFORMANCE_DISPATCH.md](PERFORMANCE_DISPATCH.md).
- **Dates** cover the whole JavaScript range: the C runtime's time functions
  stop at 1970..3000, so UTC conversions are protoJS's own calendar arithmetic
  and local time outside that range is computed in a year with the same
  calendar.

Test status. CI (`.github/workflows/cross-platform.yml`) builds and tests
Windows Server 2022 (MSVC, x64) three times -- against protoCore 2.8.0, 2.9.0
(whose DLL is `protoCore-3.dll`) and 2.7.0, the floor -- and macOS 14 (arm64,
protoCore 2.8.0), on every push to `master`:

- `ctest`: the whole suite with the Linux gate's exclusion (`-E
  "integration|network"`): 85 cases on 2026-10-02 -- the Catch2 units, the
  CLI fixtures in `tests/cli` (run through Git for Windows' `bash`, the Python
  ones through the interpreter CMake finds) and the asserting JavaScript
  fixtures, Node's own `path` tests among them. All pass on all three
  platforms.
- Test262: the per-commit regression gate (`built-ins/Object`, `Reflect`,
  `Proxy`; 3,875 tests) with the Linux expected-failures baseline. macOS gives
  the Linux failure set exactly and is gated. Windows gives 3,617 to 3,619,
  the difference always in tests that observe property enumeration order,
  which follows key addresses and on Windows changes from run to run
  ([TEST262_STATUS.md](TEST262_STATUS.md#property-enumeration-order)); there
  the gate's diff is reported, not enforced. The whole corpus is measured on
  Linux only.
- The ZIP is built (with protoCore 2.8.0's `protoCore.dll` and with 2.9.0's
  `protoCore-3.dll`), unpacked into an empty directory and run with a PATH
  holding only the Windows system directories.

A 7,316-test Test262 subset was also run once by hand during the port, on a
Windows 11 host (6,325 passing against 6,324 on Linux); CI does not
reproduce that run.

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
| Windows  | `ZIP`, plus `NSIS` when `makensis` is found | `protojs-0.1.0-win64.zip` (see [Windows (MSVC)](#windows-msvc)) |

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

Last verified 2026-09-27 against protoJS 0.1.0 and protoCore 2.5.0
(`PROTOCORE_ABI_SOVERSION 3`), built with `-DPROTOCORE_REQUIRE_PACKAGE=ON` so the
sibling developer fallback was a hard error, and with no `-j` at any point.

| Platform | Packaging | Status |
|----------|-----------|--------|
| Linux / Debian-Ubuntu | CPack TGZ/DEB as `protojs` | **VERIFIED.** The CPack DEB was installed with `dpkg -i` as root in a throwaway `ubuntu:24.04` container and `protojs` ran a script there from `/usr/bin`, outside any repository, with no `LD_LIBRARY_PATH` set. |
| Linux / Debian-Ubuntu | `packaging/build_deb.sh` as `protoJS` | **NOT BUILT — the script refuses, correctly.** See below. |
| Linux / Fedora-RHEL | TGZ, RPM | **VERIFIED, with a caveat.** `cpack -G RPM` executed in a throwaway `fedora:41` container and the RPM installed and ran. It required a **writable** source tree; see below. |
| macOS | `packaging/templates/macos/preinstall.template`, CPack DragNDrop | **UNVERIFIED.** Configured and reviewed only; there is no macOS host here. Review is not verification. |
| Windows | CPack ZIP | **VERIFIED BY CI** (`cross-platform.yml`, Windows Server 2022, protoCore 2.8.0 and 2.9.0): `cpack -G ZIP` builds `protojs-0.1.0-win64.zip` with `protojs.exe`, `protojs.lib`, the protoCore DLL, the OpenSSL DLLs and licence and the Visual C++ runtime; CI unpacks it into an empty directory and runs a script that uses `crypto` with a `PATH` holding only the Windows system directories. The REPL from the ZIP is not exercised by CI (`cli/repl-commands` runs the build tree's `protojs.exe`). |
| Windows | `packaging/templates/windows/protoJS.wxs.template` (WiX v3), CPack NSIS | **UNVERIFIED.** No WiX and no NSIS on the Windows host. The WiX condition reads `HKLM\SOFTWARE\protoCore\Soversion`, which protoCore's NSIS installer writes — and that has never run either, so both halves of that check are unverified. |

### The hand-built `packaging/` pipeline is pinned to the wrong SONAME

`packaging/build_deb.sh`, `packaging/templates/linux/preinst.template` and
`packaging/templates/linux/protoJS.spec.template` all hard-code
`libprotoCore.so.2`, and `control.template` declares
`Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)`. protoCore is now 2.5.0
with `PROTOCORE_ABI_SOVERSION 3`, so `build_deb.sh` stops with:

```
ERROR: build_pkg/protojs is not linked against libprotoCore.so.2.
  NEEDED               libprotoCore.so.3
Rebuild protoJS against protoCore 2.x.
```

The guard works; the constant it checks against is stale. This is worth being
precise about, because the numbers are inverted relative to reality: as written,
the `preinst` check would **reject a correct protoCore 2.5.0** and **accept an
ABI-incompatible 2.1.0**. Until the three files derive the SONAME from
`PROTOCORE_ABI_SOVERSION` rather than repeating `2`, this pipeline cannot produce
a usable package. The CPack DEB is unaffected and is the one that was verified.

### Building the RPM needs a writable source tree

`cpack` always runs the `preinstall` target, which depends on `all`, and `all`
includes the `fixture_addon` target, whose output path is inside the source tree
(`tests/integration/native_addons/fixture.so`, `CMakeLists.txt:264`). With a
read-only source checkout the link fails with
`cannot open output file ...: Read-only file system` and `cpack` fails with it.
Isolated by building the identical tree twice, read-only and writable: only the
writable one produced an RPM. Two consequences: an RPM cannot be built in a
sandboxed build root that mounts the sources read-only, and any protoJS build
writes a file into its own source tree (it is `.gitignore`d, so the repository
stays clean).

### Known defect: the DEB dependency floor does not encode the ABI

The `Depends` field is a *version range*, and on its own that range is not an ABI
check. `PROTOCORE_ABI_SOVERSION` went from `2` to `3` in protoCore **2.2.0**, so
protoCore 2.0.0 and 2.1.0 carry `libprotoCore.so.2` while 2.2.0 and later carry
`libprotoCore.so.3`. A floor of ``2.0.0`` therefore admits a protoCore whose
SONAME this package was not linked against.

This was demonstrated, not argued. A decoy `protocore` 2.1.0 package providing
only `libprotoCore.so.2` was installed in a container; `dpkg -i` then accepted
this package, and the installed binary failed to start with
`libprotoCore.so.3: cannot open shared object file`. The install succeeded and
the program did not run.

Two things limit the damage, and one closes it:

- At **build** time the failure is loud, not silent. `find_package(protoCore …)`
  alone does accept a SOVERSION-2 protoCore, but `CMakeLists.txt` follows it with
  an explicit `protoCore_SOVERSION` assertion against `PROTOCORE_ABI_SOVERSION`,
  which stops configuration with a `FATAL_ERROR` naming both numbers. Verified by
  configuring against a complete forged 2.1.0 / SOVERSION 2 prefix.
- The **RPM** does not have this hole. `rpm` generates
  `Requires: libprotoCore.so.3()(64bit)` automatically from the linked binary, and
  that requirement is on the SONAME rather than the version. Verified: the decoy
  protoCore 2.1.0 does not satisfy it and `rpm -i` refuses.
- Raising the DEB floor to `2.2.0`, the first protoCore that shipped SOVERSION 3,
  would make the DEB range agree with the ABI. That is a packaging change for the
  maintainer to take, and it is not made here.

### Known defect: the DEB does not refresh the shared-library cache

Neither this package nor protoCore's carries a `postinst` or an `ldconfig`
trigger, so `ldconfig -p` does not list `libprotoCore.so.3` after `dpkg -i`.
Programs still start, because each binary carries
`RUNPATH $ORIGIN/../${CMAKE_INSTALL_LIBDIR}` and because the library lands in a
directory the dynamic loader searches by default, but the cache is misleading.
Run `ldconfig` after installing. The RPM has no such defect.

---

## Troubleshooting

- **"protoCore shared library not found" at configure time** — build protoCore in `../protoCore/build_release` (or `../protoCore/build`, or `../protoCore/build_check`), or configure with `-DPROTO_CORE_PREFIX=<prefix>` for an installed protoCore.
- **protoJS behaves as though a protoCore change had not landed** — a stale sibling build directory earlier in the search order was picked. Re-read the `-- Found protoCore: <path>` line from the configure output, and remove the stale directory or configure with `-DPROTO_CORE_PREFIX=<prefix>`.
- **"No protoCore >= 2.0.0 CMake package was found under ..."** — the prefix must contain `lib/cmake/protoCore/protoCoreConfig.cmake`, which protoCore's own install rules emit. A prefix holding only `lib/libprotoCore` and `include/protoCore.h` is deliberately refused: it cannot be told apart from a protoCore 1.x.
- **Linker errors mentioning `ssl` or `crypto`** — install the OpenSSL development package.
- **`error while loading shared libraries: libprotoCore...` at run time** — the executable was moved away from its RPATH layout, or protoCore is installed in another prefix. Set `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH`, or install protoCore and protoJS under the same prefix.
- **Configure step fails while fetching Catch2** — install Catch2 v3 system-wide, allow network access, or configure with `-DBUILD_TESTING=OFF`.

For runtime problems see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
