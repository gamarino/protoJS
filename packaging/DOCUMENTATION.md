# protoJS Packaging Notes

For end-user build and installation instructions, see [docs/INSTALLATION.md](../docs/INSTALLATION.md). For the package build steps, see [PROCEDURES.md](PROCEDURES.md). No prebuilt protoJS or protoCore packages are published; both are built from source.

---

## Dependency error messages

**protoCore** is the shared library that protoJS links against (`libprotoCore.so.5` on Linux, `libprotoCore.5.dylib` on macOS). protoJS requires protoCore 3.4.0 or later within the 3.x series: protoCore's major version and its soname move together, and 3.4.0 moved the soname to SOVERSION 5 (`ProtoContext::pendingRoot` removed), so a protoCore older than 3.4.0, or a 4.x, is ABI-incompatible with this protoJS.

- **CPack packages** (`protojs-<version>-Linux.deb` / `.rpm`) declare the dependency in package metadata only (`Depends: protocore (>= 3.4.0), protocore (<< 4.0.0)`, `Requires: protoCore >= 3.4.0, protoCore < 4.0.0`). The package manager reports a missing or out-of-range dependency with its own message.
- **Template-based installers** (`packaging/templates/`) run a pre-install script that prints the messages below and aborts.

### Missing dependency

Printed by `preinst.template`, the `%pre` section of `protoJS.spec.template`, `preinstall.template` (macOS) and the WiX condition in `protoJS.wxs.template`:

```
ERROR: protoCore is not installed.
protoJS requires protoCore >= 3.4.0 and < 4.0.0.
```

Remedy: build protoCore from <https://github.com/numaes/protoCore>, create its package with CPack, and install that package first.

### Version too old

Printed by `preinst.template` and the RPM `%pre` script, which require protoCore in `[3.4.0, 4.0.0)` and, separately, the `libprotoCore.so.5` soname:

```
ERROR: protoCore version <installed version> is too old.
protoJS requires protoCore >= 3.4.0.

or, for a protoCore from a later major series:

ERROR: protoCore version <installed version> is too new.
protoJS is built against the 3.4.0 series and needs < 4.0.0.

or, when the package version is right but the library is not:

ERROR: protocore <installed version> does not provide libprotoCore.so.5.
```

Remedy: rebuild and reinstall protoCore from a current checkout.

---

## Release checklist

### 1. Verification on a clean machine or VM

- [ ] **Install without protoCore** — installing protoJS must fail: the package manager reports the unmet dependency (CPack packages), or the pre-install script prints the "Missing dependency" message (template packages).
- [ ] **Install with protoCore** — install the protoCore package, then protoJS; installation succeeds.
- [ ] **Execution** — `protojs --version` prints the configured version, and `protojs -e "console.log('Hello from protoJS')"` runs without dynamic-linker errors (for example a missing `libprotoCore`).

### 2. Architecture

- [ ] **Linux:** the template `.deb` declares `amd64`; the template `.rpm` declares `x86_64`. CPack packages take the architecture of the build host.
- [ ] **macOS:** `lipo -info /usr/local/bin/protojs` matches the build host; the CMake files do not configure universal binaries.

### 3. Removal

- [ ] Uninstalling protoJS removes the `protojs` binary (`/usr/bin/protojs` for the template `.deb`/`.rpm`, `/usr/local/bin/protojs` for the macOS template, and the path listed by `dpkg -c` / `rpm -qpl` for CPack packages).
- [ ] protoCore remains installed, since it is a separate package.
