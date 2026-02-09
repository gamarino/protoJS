# protoJS Packaging and Release Procedures

This document describes how to build distribution packages for protoJS on Linux (.deb, .rpm), macOS (.pkg), and Windows (.msi). Use it after you have built the `protojs` binary (and, for Windows, `protojs.exe`) and have protoCore available for dependency checks.

**Prerequisites for packagers:**

- protoJS built successfully: `build/protojs` (Linux/macOS) or `build/protojs.exe` (Windows)
- protoCore installed or built so that dependency checks in installers can pass
- Platform-specific tools: `dpkg-deb` (deb), `rpmbuild` (rpm), `pkgbuild`/`productbuild` (macOS), WiX Toolset (Windows)

**Quick .deb build (Debian/Ubuntu):** From the protoJS project root, run `./packaging/build_deb.sh` to generate only the .deb for the current system. This uses the latest templates (preinst checks for the `protocore` package). Then install with `sudo dpkg -i protoJS_0.1.0_amd64.deb`.

---

## 1. Linux: Debian/Ubuntu (.deb)

**Staging layout:**

```
protoJS_staging/
├── DEBIAN/
│   ├── control      (from control.template, with VERSION and MAINTAINER set)
│   └── preinst      (from preinst.template; must be executable)
└── usr/
    └── bin/
        └── protojs
```

**Steps:**

```bash
# From protoJS project root (parent of build/)
export VERSION=0.1.0
export MAINTAINER="Your Name <email@example.com>"

# 1. Create staging directories
mkdir -p protoJS_staging/DEBIAN
mkdir -p protoJS_staging/usr/bin

# 2. Copy the binary
cp build/protojs protoJS_staging/usr/bin/protojs
chmod 755 protoJS_staging/usr/bin/protojs

# 3. Generate control from template (replace ${VERSION} and ${MAINTAINER})
sed -e "s/\${VERSION}/$VERSION/g" -e "s/\${MAINTAINER}/$MAINTAINER/g" \
    packaging/templates/linux/control.template > protoJS_staging/DEBIAN/control

# 4. Copy and set permissions for preinst
cp packaging/templates/linux/preinst.template protoJS_staging/DEBIAN/preinst
chmod 755 protoJS_staging/DEBIAN/preinst

# 5. Build the package
dpkg-deb --build protoJS_staging protoJS_${VERSION}_amd64.deb
```

**Verification:**

```bash
sudo dpkg -i protoJS_0.1.0_amd64.deb
dpkg -l protoJS
protojs --version
protojs -e "console.log('Hello from protoJS')"
sudo apt remove protoJS
```

**Note:** The `preinst` script checks that protoCore (>= 1.0.0) is installed; it looks for the package under the name **`protocore`** (lowercase, as produced by CPack) or `protoCore`. The `control` template declares `Depends: protocore (>= 1.0.0)` to match. See [DOCUMENTATION.md](DOCUMENTATION.md) for user-facing messages.

---

## 2. Linux: Fedora/RHEL/openSUSE (.rpm)

The RPM spec expects a source tarball that unpacks to a single `protojs` binary (no directory prefix). The binary is then installed into `/usr/bin`.

**Steps:**

```bash
# From protoJS project root
export VERSION=0.1.0
export RELEASE=1

# 1. Create rpmbuild directory layout
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

# 2. Create tarball with layout protoJS-<version>/protojs (required by spec %prep)
mkdir -p protoJS-${VERSION}
cp build/protojs protoJS-${VERSION}/
tar -czf ~/rpmbuild/SOURCES/protoJS-${VERSION}.tar.gz protoJS-${VERSION}
rm -rf protoJS-${VERSION}

# 3. Copy spec file and build (use --define for version and release)
cp packaging/templates/linux/protoJS.spec.template ~/rpmbuild/SPECS/protoJS.spec
rpmbuild -ba ~/rpmbuild/SPECS/protoJS.spec \
  --define "version $VERSION" \
  --define "release $RELEASE"
```

The resulting package is:

- `~/rpmbuild/RPMS/x86_64/protoJS-${VERSION}-${RELEASE}.x86_64.rpm`

**Verification:**

```bash
sudo rpm -ivh ~/rpmbuild/RPMS/x86_64/protoJS-0.1.0-1.x86_64.rpm
# or: sudo dnf install ~/rpmbuild/RPMS/x86_64/protoJS-0.1.0-1.x86_64.rpm
rpm -q protoJS
protojs --version
sudo rpm -e protoJS
```

The spec’s `%pre` script checks for protoCore (>= 1.0.0); if missing or too old, the RPM install will fail with the error messages defined in [DOCUMENTATION.md](DOCUMENTATION.md).

---

## 3. macOS: .pkg installer

**Staging layout:** The root of the package is the install hierarchy. To install into `/usr/local/bin`, the staging directory must contain `usr/local/bin/protojs`.

**Steps:**

```bash
# From protoJS project root
export VERSION=0.1.0

# 1. Create staging with same layout as target
mkdir -p staging/usr/local/bin
cp build/protojs staging/usr/local/bin/protojs
chmod 755 staging/usr/local/bin/protojs

# 2. Build component package (preinstall script in packaging/templates/macos/)
pkgbuild --root staging \
         --identifier com.protoJS.pkg \
         --version "$VERSION" \
         --install-location / \
         --scripts packaging/templates/macos \
         protoJS-core.pkg

# 3. Build product/installer package
productbuild --package protoJS-core.pkg \
             --identifier com.protoJS.installer \
             protoJS-${VERSION}.pkg
```

**Verification:**

```bash
sudo installer -pkg protoJS-0.1.0.pkg -target /
/usr/local/bin/protojs --version
lipo -info /usr/local/bin/protojs
otool -L /usr/local/bin/protojs
```

**Signing and notarization (recommended for distribution):**

```bash
# Sign
productsign --sign "Developer ID Installer: Your Team (ID)" protoJS-0.1.0.pkg protoJS-0.1.0-signed.pkg

# Notarize (requires Apple ID and app-specific password)
xcrun notarytool submit protoJS-0.1.0-signed.pkg --keychain-profile "AC_PASSWORD" --wait

# Staple ticket
xcrun stapler staple protoJS-0.1.0-signed.pkg
```

---

## 4. Windows: .msi installer (WiX)

**Requirements:** WiX Toolset v3.11 or later (e.g. from [wixtoolset.org](https://wixtoolset.org/) or Visual Studio extension).

Before building, **replace placeholder GUIDs** in `packaging/templates/windows/protoJS.wxs.template`:

- `PUT-GUID-HERE-1` — Product `Id="*"` generates a new GUID per build; for upgrades use a fixed `UpgradeCode` (already in the template). You can leave `Id="*"` or set a fixed Product GUID.
- `PUT-GUID-HERE-2` — Component GUID for the `protojs.exe` file (generate with `uuidgen` or similar).
- `PUT-GUID-HERE-3` — Component GUID for the PATH environment (generate a different one).

Ensure the binary is built as **protojs.exe** and is in the current directory or adjust the `Source` path in the template.

**Steps (from Command Prompt or PowerShell):**

```cmd
REM From protoJS project root; ensure build\protojs.exe exists (rename if your build outputs protojs.exe)
set VERSION=0.1.0

REM 1. Copy binary to a known location for WiX (e.g. packaging folder or current dir)
copy build\protojs.exe protojs.exe

REM 2. Compile WiX source (output .wixobj)
candle -arch x64 packaging/templates/windows/protoJS.wxs.template -o protoJS.wixobj

REM 3. Link (output .msi)
light protoJS.wixobj -o protoJS-%VERSION%.msi
```

If the template is not in the current directory, use full paths. The `Source` attribute in the template must point to the location of `protojs.exe` (e.g. `Source="protojs.exe"` if it is in the current directory when running `candle`/`light`).

**Verification:**

- Install: double-click the .msi or run `msiexec /i protoJS-0.1.0.msi`.
- Open a **new** command prompt and run: `protojs --version`, `protojs -e "console.log('OK')"`.
- Uninstall via “Add or remove programs” or `msiexec /x {Product-GUID}`.

**Silent install/uninstall:**

```cmd
msiexec /i protoJS-0.1.0.msi /quiet /qn /norestart
msiexec /x {Product-GUID} /quiet /qn /norestart
```

The MSI adds the install directory to the system PATH so that `protojs` is available in new shells. The installer condition checks for protoCore (registry or file); see the template and [DOCUMENTATION.md](DOCUMENTATION.md).

---

## Summary

| Platform | Package  | Binary location after install   | Dependency check        |
|----------|----------|----------------------------------|-------------------------|
| Linux    | .deb     | `/usr/bin/protojs`              | preinst: dpkg protoCore |
| Linux    | .rpm     | `/usr/bin/protojs`              | %pre: rpm protoCore     |
| macOS    | .pkg     | `/usr/local/bin/protojs`        | preinstall: pkgutil/lib |
| Windows  | .msi     | `C:\Program Files\protoJS\` + PATH | WiX condition: registry/file |

For user-facing error messages and release checklist, see [DOCUMENTATION.md](DOCUMENTATION.md).
