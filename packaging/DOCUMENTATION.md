### User-Facing Error Messages

When `protoCore` is missing or the version is incompatible, the following error messages MUST be displayed by the installer/package manager:

#### 1. Missing Dependency (Generic)
**Message:**
`ERROR: protoCore is not installed. Please install protoCore before installing protoJS.`

**Actionable Step:**
Download and install the latest `protoCore` package from [Official Download Page] or run `sudo apt/dnf install protoCore`.

#### 2. Version Mismatch
**Message:**
`ERROR: protoCore version <INSTALLED_VERSION> is too old. protoJS requires protoCore >= <MIN_VERSION>.`

**Actionable Step:**
Upgrade `protoCore` to the required version using your package manager (e.g., `sudo apt update && sudo apt install protoCore`) or download the newer installer.

---

### Release Checklist

#### 1. Pre-Release Verification (Clean Machine / VM)
- [ ] **Scenario: Clean install WITHOUT protoCore**
  - Attempt to install `protoJS`.
  - **Expected Result:** Installation MUST fail with the "Missing Dependency" error message.
- [ ] **Scenario: Clean install WITH protoCore**
  - Install `protoCore`.
  - Install `protoJS`.
  - **Expected Result:** Installation succeeds.
- [ ] **Scenario: Binary Execution**
  - Run `protojs --version`.
  - Run a trivial test script: `protojs -e "console.log('Hello from protoJS')"`
  - **Expected Result:** Commands execute without dynamic linker errors (e.g., missing `libprotoCore`).

#### 2. Architecture Validation
- [ ] **Linux:** Verify `.deb` and `.rpm` are `amd64` / `x86_64`.
- [ ] **macOS:** Verify `.pkg` is `universal2` (use `lipo -info /usr/local/bin/protojs`).
- [ ] **Windows:** Verify `.msi` is `x64`.

#### 3. Uninstallation
- [ ] **Scenario: Removal**
  - Uninstall `protoJS`.
  - **Expected Result:**
    - `protojs` binary is removed from `/usr/bin` (Linux), `/usr/local/bin` (macOS), or `C:\Program Files\protoJS` (Windows).
    - Windows: `PATH` entry for `protoJS` is removed.
    - `protoCore` remains installed (since it is a separate dependency).
