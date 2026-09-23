# protoJS Packaging Procedures

No prebuilt protoJS packages are published. This document describes how to build packages locally from a protoJS build. There are two independent mechanisms:

1. **CPack**, configured in `CMakeLists.txt`. It packages exactly what the CMake install rule installs.
2. **Hand-maintained templates** under `packaging/templates/` plus `packaging/build_deb.sh`. They stage a `protojs` binary manually and add dependency-check scripts.

Neither mechanism is exercised by continuous integration (the repository has none). Build and install instructions for end users are in [docs/INSTALLATION.md](../docs/INSTALLATION.md).

**Prerequisites:** a successful build (see the installation guide) and the platform packaging tools for the format you want: `rpmbuild` for RPM, `dpkg-deb` for the template-based `.deb`, `pkgbuild`/`productbuild` for the macOS template. The CMake build files support Linux; macOS builds are not verified, and Windows is not supported by the current build files.

---

## 1. CPack (recommended)

The CPack settings are in `CMakeLists.txt` under "Packaging (CPack)". The package version comes from `project(protoJS VERSION 0.1.0)`.

```bash
cmake -S . -B build
cmake --build build
cd build
cpack -G DEB          # Debian/Ubuntu package
cpack -G RPM          # RPM package (requires rpmbuild)
cpack -G TGZ          # tarball
```

Running plain `cpack` builds every generator configured for the platform.

| Platform | Generators | Output files (CPack default naming `<name>-<version>-<system>`) |
|----------|------------|------------------------------------------------------------------|
| Linux    | `DEB;RPM;TGZ` | `protojs-0.1.0-Linux.deb`, `protojs-0.1.0-Linux.rpm`, `protojs-0.1.0-Linux.tar.gz` |
| macOS    | `DragNDrop`   | `protojs-0.1.0-Darwin.dmg` |
| Windows  | `NSIS;ZIP`    | configured, but Windows builds are not supported by the current build files |

Package metadata set in `CMakeLists.txt`:

| Field | Value |
|-------|-------|
| Package name | `protojs` |
| Vendor / contact | `numaes` / `gamarino@gmail.com` |
| DEB section | `interpreters` |
| DEB dependency | `protocore (>= 2.0.0), protocore (<< 3.0.0)` |
| RPM license / group | `MIT` / `Development/Languages` |
| RPM dependency | `protoCore >= 2.0.0, protoCore < 3.0.0` |

The dependency names match the packages built by protoCore's own CPack configuration, which pins them explicitly: `protocore` for DEB, `protoCore` for RPM. The range is `[2.0.0, 3.0.0)` because protoCore's major version and its soname move together. Build and install the protoCore package first.

The DEB and RPM generators are enabled only when `dpkg` and `rpmbuild` are found: `cpack` runs every configured generator in one pass and a missing tool is fatal, so an unconditional generator list makes the whole run fail on a host that lacks one of them. Each configure prints which generators were enabled or disabled, and why.

**Inspecting and installing the results:**

```bash
dpkg -I protojs-0.1.0-Linux.deb        # metadata, including Depends
dpkg -c protojs-0.1.0-Linux.deb        # contents
sudo dpkg -i protojs-0.1.0-Linux.deb
protojs --version                       # prints: protoJS v0.1.0
sudo apt remove protojs

rpm -qpi protojs-0.1.0-Linux.rpm       # metadata
rpm -qpR protojs-0.1.0-Linux.rpm       # dependencies
sudo rpm -ivh protojs-0.1.0-Linux.rpm
sudo rpm -e protojs
```

The installed executable uses the RPATH `$ORIGIN/../<libdir>`, so it finds `libprotoCore` when protoCore is installed under the same prefix.

---

## 2. Hand-maintained templates

These files predate the CPack configuration and are kept for packagers who need installer-side dependency checks. They are not generated from `CMakeLists.txt`; keep their version fields in sync manually.

| File | Purpose |
|------|---------|
| `packaging/build_deb.sh` | Builds a `.deb` from `build_release/protojs` (override with `PROTOJS_BINARY`) using the two Linux templates below; refuses to build unless the binary is linked against `libprotoCore.so.2` |
| `packaging/templates/linux/control.template` | Debian `control` file: package `protoJS`, `Architecture: amd64`, `Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)` |
| `packaging/templates/linux/preinst.template` | Pre-install script: fails unless package `protocore` or `protoCore` is installed in `[2.0.0, 3.0.0)` **and** provides the `libprotoCore.so.2` soname |
| `packaging/templates/linux/protoJS.spec.template` | RPM spec: `Requires: protoCore >= 2.0.0, protoCore < 3.0.0`, with a `%pre` version and soname check |
| `packaging/templates/macos/preinstall.template` | macOS pre-install script: looks for the versioned `libprotoCore.2.dylib` under `/usr/local/lib`, `/opt/homebrew/lib` or `/opt/local/lib`, then for the package receipt `com.protoCore.pkg`. protoCore's macOS generator is DragNDrop (a `.dmg`), which registers no receipt, so the `pkgutil` branch only fires once protoCore ships a `productbuild` `.pkg` |
| `packaging/templates/windows/protoJS.wxs.template` | WiX source for an MSI; contains placeholder GUIDs |

### 2.1 Debian/Ubuntu (.deb)

From the repository root, after building `build_release/protojs`:

```bash
VERSION=0.1.0 MAINTAINER="Your Name <you@example.com>" ./packaging/build_deb.sh
```

The script checks that the binary is linked against `libprotoCore.so.2`, stages `build_release/protoJS_staging/usr/bin/protojs`, generates `DEBIAN/control` from the template (substituting `${VERSION}` and `${MAINTAINER}`), copies `preinst`, and runs `dpkg-deb --build`. The output is `build_release/protoJS_<version>_amd64.deb`. Staging and output live inside the build directory so the script no longer writes into the repository root next to the committed artefacts of the same name. Override any of `VERSION`, `MAINTAINER`, `PROTOJS_BINARY`, `PROTOCORE_SONAME` and `OUTDIR`.

```bash
sudo dpkg -i protoJS_0.1.0_amd64.deb
protojs --version
sudo apt remove protojs     # dpkg stores package names in lowercase
```

### 2.2 RPM (Fedora/RHEL/openSUSE)

The spec expects a source tarball that unpacks to `protoJS-<version>/protojs`:

```bash
export VERSION=0.1.0 RELEASE=1
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
mkdir -p protoJS-${VERSION}
cp build/protojs protoJS-${VERSION}/
tar -czf ~/rpmbuild/SOURCES/protoJS-${VERSION}.tar.gz protoJS-${VERSION}
rm -rf protoJS-${VERSION}
cp packaging/templates/linux/protoJS.spec.template ~/rpmbuild/SPECS/protoJS.spec
rpmbuild -ba ~/rpmbuild/SPECS/protoJS.spec \
  --define "version $VERSION" \
  --define "release $RELEASE"
```

Because the spec sets `Release: %{release}%{?dist}`, the package file is `~/rpmbuild/RPMS/x86_64/protoJS-<version>-<release><dist>.x86_64.rpm`, where `<dist>` is the distribution tag (for example `.fc40`) or empty.

### 2.3 macOS (.pkg)

```bash
export VERSION=0.1.0
mkdir -p staging/usr/local/bin
cp build/protojs staging/usr/local/bin/protojs
chmod 755 staging/usr/local/bin/protojs

pkgbuild --root staging \
         --identifier com.protoJS.pkg \
         --version "$VERSION" \
         --install-location / \
         --scripts packaging/templates/macos \
         protoJS-core.pkg

productbuild --package protoJS-core.pkg \
             --identifier com.protoJS.installer \
             protoJS-${VERSION}.pkg
```

`pkgbuild --scripts` only picks up a script named `preinstall`; copy `preinstall.template` to a scripts directory under that name (and make it executable) before running `pkgbuild`. The binary is built for the host architecture; the CMake files do not configure universal binaries. Signing (`productsign`) and notarization (`xcrun notarytool`) are required for distribution outside a development machine.

### 2.4 Windows (.msi)

`protoJS.wxs.template` is a WiX v3 source for an x64 MSI that installs `protojs.exe` under `Program Files\protoJS`, adds that directory to `PATH`, and refuses to install unless the registry key `HKLM\SOFTWARE\protoCore` or the file `[ProgramFiles64Folder]protoCore\protoCore.dll` exists. Its GUID placeholders (`PUT-GUID-HERE-1` to `PUT-GUID-HERE-3`) must be replaced before use. The template cannot be used until protoJS builds on Windows, which the current build files do not support.

---

## Summary

| Mechanism | Format | Output file | Binary location | Dependency handling |
|-----------|--------|-------------|-----------------|---------------------|
| CPack | DEB | `protojs-0.1.0-Linux.deb` | `bin/protojs` under the package install prefix | `Depends: protocore (>= 2.0.0), protocore (<< 3.0.0)` |
| CPack | RPM | `protojs-0.1.0-Linux.rpm` | `bin/protojs` under the package install prefix | `Requires: protoCore >= 2.0.0, protoCore < 3.0.0` |
| CPack | TGZ | `protojs-0.1.0-Linux.tar.gz` | `bin/protojs` inside the archive | none |
| Template | DEB | `build_release/protoJS_0.1.0_amd64.deb` | `/usr/bin/protojs` | `Depends` + `preinst` version and soname check (`[2.0.0, 3.0.0)`, `libprotoCore.so.2`) |
| Template | RPM | `protoJS-0.1.0-1<dist>.x86_64.rpm` | `/usr/bin/protojs` | `Requires` + `%pre` version and soname check (`[2.0.0, 3.0.0)`, `libprotoCore.so.2`) |
| Template | PKG | `protoJS-0.1.0.pkg` | `/usr/local/bin/protojs` | `preinstall` check |

Use `dpkg -c` or `rpm -qpl` to see the exact install path inside a CPack package. User-facing dependency error messages and a release checklist are in [DOCUMENTATION.md](DOCUMENTATION.md).
