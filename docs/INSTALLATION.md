# protoJS Installation Guide

This guide covers installing protoJS on **Linux** (.deb and .rpm), **macOS**, and **Windows**. protoJS depends on the **protoCore** shared library; you must install or build protoCore first.

---

## Prerequisites

- **protoCore** — Required. The runtime expects `libprotoCore.so` (Linux), `libprotoCore.dylib` (macOS), or `protoCore.dll` (Windows). Install a compatible protoCore package or build it from source (see [protoCore](https://github.com/gamarino/protoCore)).
- **C++20** compiler (GCC 10+, Clang 12+, or MSVC 2019+)
- **CMake** 3.16+
- **OpenSSL** development headers (for crypto module; often `libssl-dev` / `openssl-devel` / system SDK)

---

## Building from Source (all platforms)

Build protoCore first, then protoJS. From the directory that contains both `protoCore` and `protoJS`:

```bash
# 1. Build protoCore shared library
cd protoCore
cmake -B build -S .
cmake --build build --target protoCore
cd ..

# 2. Build protoJS
cd protoJS
mkdir -p build && cd build
cmake ..
cmake --build .
```

The executable will be `build/protojs` (or `build/protojs.exe` on Windows). Run from the project root:

```bash
./build/protojs -e "console.log('Hello, protoJS')"
```

---

## Linux

### Option A: Install from .deb (Debian / Ubuntu)

1. **Install protoCore** (if not already installed). The protoCore .deb built with CPack installs with package name **`protocore`** (lowercase). Install it first, e.g.:
   ```bash
   sudo dpkg -i protoCore-1.0.0-Linux.deb
   ```
   Or build protoCore from source and install to a path where the linker can find it.
2. **Build the protoJS .deb** from the current project (the package’s preinst checks for the `protocore` package; an old .deb built before this fix will fail). From the protoJS repo root:
   ```bash
   ./packaging/build_deb.sh
   # optional: export VERSION=0.1.0 MAINTAINER="Your Name <email@example.com>"
   ```
3. **Install the protoJS .deb package:**

   ```bash
   sudo dpkg -i protoJS_0.1.0_amd64.deb
   ```

   If dependency check fails (e.g. protoCore not installed), install protoCore first, then:

   ```bash
   sudo apt-get install -f   # fix broken dependencies if needed
   sudo dpkg -i protoJS_0.1.0_amd64.deb
   ```

4. **Verify:**

   ```bash
   protojs --version
   protojs -e "console.log('OK')"
   ```

5. **Uninstall:**

   ```bash
   sudo apt remove protoJS
   ```

The .deb package installs the `protojs` binary to `/usr/bin` and runs a pre-install script that checks for the **`protocore`** package (version >= 1.0.0). You must build the .deb from the current templates (e.g. run `./packaging/build_deb.sh`) so the preinst looks for the correct package name; an older .deb may report "protoCore is not installed" even when protocore is installed.

### Option B: Install from .rpm (Fedora / RHEL / openSUSE)

1. **Install protoCore** (e.g. from .rpm or build from source).
2. **Install the protoJS .rpm package:**

   ```bash
   sudo rpm -ivh protoJS-0.1.0-1.x86_64.rpm
   # or, on Fedora/RHEL with dnf:
   sudo dnf install protoJS-0.1.0-1.x86_64.rpm
   ```

   The package will not install if protoCore is missing or too old (>= 1.0.0 required).

3. **Verify:**

   ```bash
   protojs --version
   protojs -e "console.log('OK')"
   ```

4. **Uninstall:**

   ```bash
   sudo rpm -e protoJS
   # or: sudo dnf remove protoJS
   ```

The .rpm package installs `/usr/bin/protojs`.

### Building .deb and .rpm packages

See [packaging/PROCEDURES.md](../packaging/PROCEDURES.md) for step-by-step commands to build the .deb and .rpm packages from a built `build/protojs` binary.

---

## macOS

### Option A: Install from .pkg

1. **Install protoCore** so that `libprotoCore.dylib` is available (e.g. in `/usr/local/lib` or via a protoCore .pkg). The protoJS installer script checks for protoCore via `pkgutil` or the library path.
2. **Open the .pkg** (e.g. `protoJS-0.1.0.pkg`) and follow the installer. The binary is installed to `/usr/local/bin/protojs`.
3. **Verify:**

   ```bash
   /usr/local/bin/protojs --version
   lipo -info /usr/local/bin/protojs   # optional: check architecture
   otool -L /usr/local/bin/protojs     # optional: check linked libs (e.g. libprotoCore.dylib)
   ```

4. **Uninstall:** Remove the binary and any receipt:

   ```bash
   sudo rm -f /usr/local/bin/protojs
   sudo pkgutil --forget com.protoJS.pkg
   ```

### Option B: Build from source

After building as in “Building from Source”, run:

```bash
./build/protojs -e "console.log('Hello, protoJS')"
```

To install into `/usr/local/bin` manually:

```bash
cp build/protojs /usr/local/bin/
# Ensure libprotoCore.dylib is in /usr/local/lib or set DYLD_LIBRARY_PATH
```

### Building the .pkg

See [packaging/PROCEDURES.md](../packaging/PROCEDURES.md) for `pkgbuild` and `productbuild` commands. Signing and notarization are recommended for distribution outside the Mac App Store.

---

## Windows

### Option A: Install from .msi

1. **Install protoCore** so that `protoCore.dll` is available (e.g. in `C:\Program Files\protoCore` or on `PATH`). The MSI installer can check for protoCore via registry or file search; if not found, installation will show an error and abort.
2. **Run the .msi** (e.g. `protoJS-0.1.0.msi`) as Administrator. Choose the installation directory (default: `C:\Program Files\protoJS`). The installer adds that directory to the system `PATH`.
3. **Verify:** Open a **new** Command Prompt or PowerShell:

   ```cmd
   protojs --version
   protojs -e "console.log('OK')"
   ```

4. **Uninstall:** Use “Add or remove programs” (Apps & features) and remove “protoJS”, or run:

   ```cmd
   msiexec /x {Product-GUID} /quiet
   ```

   The installer removes the binary and the PATH entry; protoCore is left installed.

### Option B: Build from source

1. Install **Visual Studio** 2019 or later with C++ desktop development workload, **CMake**, and **OpenSSL** (e.g. vcpkg or a prebuilt distribution).
2. Build **protoCore** as a shared library (e.g. `protoCore.dll`) and ensure it is on `PATH` or in the same directory as `protojs.exe`.
3. From a developer command prompt, in the repo root:

   ```cmd
   cd protoJS
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2022" -A x64
   cmake --build . --config Release
   ```

   The executable is `build\Release\protojs.exe` (or under `build` depending on generator). Run it from a shell where `protoCore.dll` is on `PATH` or next to the executable.

### Building the .msi

You need **WiX Toolset** v3.11 or later. See [packaging/PROCEDURES.md](../packaging/PROCEDURES.md) for `candle` and `light` commands. Replace placeholder GUIDs in the WiX template before building.

---

## Troubleshooting

- **“protoCore is not installed”** — Install or build protoCore first. On Linux, use the system package if available (`apt install protoCore` / `dnf install protoCore`); otherwise build from source and install the shared library to a standard path or set `LD_LIBRARY_PATH` (Linux), `DYLD_LIBRARY_PATH` (macOS), or `PATH` (Windows) so the loader can find it.
- **“Cannot find module” at runtime** — For script execution, run from the appropriate working directory so that `require()` and file paths resolve correctly.
- **Linux: linker errors when building** — Install OpenSSL development packages (e.g. `libssl-dev`, `openssl-devel`) and ensure protoCore is built and findable by CMake (e.g. in `../protoCore/build`).

For more details, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md) and [packaging/DOCUMENTATION.md](../packaging/DOCUMENTATION.md).
