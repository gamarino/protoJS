# Changelog

All notable changes to protoJS are documented in this file.

## [Unreleased]

### Fixed

- **Packaging** (2026-02-08): Added `packaging/build_deb.sh` to build the protoJS .deb from current templates on Debian/Ubuntu. INSTALLATION and PROCEDURES updated: users must rebuild the .deb (e.g. run `./packaging/build_deb.sh`) after the protocore dependency fix—otherwise an old .deb still reports "protoCore is not installed" when the `protocore` package is installed.

- **Debian package dependency check** (2026-02-08): protoJS .deb preinst now looks for the protoCore package under the name **`protocore`** (lowercase), which is how CPack installs the protoCore .deb. Also added fallback check for `protoCore`. The control template `Depends` was updated to `protocore (>= 1.0.0)` so installation succeeds when protoCore is installed from its CPack-generated .deb. Docs (INSTALLATION.md, PROCEDURES.md) updated accordingly.

- **protoCore getImportModule API** (2026-02-08): CommonJSLoader now passes `ProtoContext*` as the first argument to `ProtoSpace::getImportModule(context, logicalPath, attrName)` to match the current protoCore API (fixes build error when building against updated protoCore).

- **-Wformat-security warnings** (2026-02-08): All `JS_ThrowTypeError(ctx, dynamic_string.c_str())` calls replaced with `JS_ThrowTypeError(ctx, "%s", dynamic_string.c_str())` in CommonJSLoader, ESModuleLoader, IOModule, FSModule, and DNSModule so the format string is a literal and the compiler no longer reports format-security warnings.

- **require() built-in module resolution** (2026-02-08): `require('fs')`, `require('path')`, `require('stream')`, `require('crypto')`, `require('buffer')`, and other core modules now resolve from the global object so integration tests (fs, stream, crypto, buffer) pass. See CommonJSLoader built-in resolution and docs (README, NATIVE_MODULES).

- **GCBridge null-pointer handling** (2026-02-07): Fixed `-Wnonnull` compiler warnings and potential undefined behavior in `GCBridge::detectLeaks()` and `GCBridge::getMemoryStats()` when `ProtoContext` is null. Both functions now return early with null/empty values instead of dereferencing a null pointer. Added null checks in `reportLeaks()` and `getMemoryStats()` for defensive handling of empty reports.

### Build & Test

- Full project recompilation: protoCore + protoJS clean build
- All 33 unit tests passing (ctest)
- Integration tests verified (hello_world, arithmetic, modules)

### Performance (2026-02-07)

- Performance suite executed successfully: `run_nodejs_comparison.js` (5/5 benchmarks)
- **Array operations:** 34–45x faster than Node.js (immutable structural sharing)
- **Overall speedup:** ~10–45x depending on workload
- Added [docs/PERFORMANCE_RUN_2026-02-07.md](docs/PERFORMANCE_RUN_2026-02-07.md) with run report and analysis
