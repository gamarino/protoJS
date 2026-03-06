# Changelog

All notable changes to protoJS are documented in this file.

## [Unreleased]

### Added

- **Phase 6: ProtoCore-native global object** (2026-03-03): The global scope is now a ProtoObject built at first eval from the QuickJS global via `JS_GetOwnPropertyNames` and `TypeBridge::fromJS`. No QuickJS heap is used for the global container; conversion only at host boundaries. `runBytecode` accepts `pGlobalRoot` and updates it on `put_field`/`define_field` so top-level `var` assignments persist and subsequent reads see the new object. Directed tests: `proto_eval_smoke.js` (6 cases, including Phase 6 global var) and `tests/test262/tests/phase6_native_global.js`. Docs: ARCHITECTURE.md § 1.4, CONFORMANCE_JS.md Phase 6 table, src/runtime/README.md, TECHNICAL_AUDIT.md.

### Fixed

- **Packaging** (2026-02-08): Added `packaging/build_deb.sh` to build the protoJS .deb from current templates on Debian/Ubuntu. INSTALLATION and PROCEDURES updated: users must rebuild the .deb (e.g. run `./packaging/build_deb.sh`) after the protocore dependency fix—otherwise an old .deb still reports "protoCore is not installed" when the `protocore` package is installed.

- **Debian package dependency check** (2026-02-08): protoJS .deb preinst now looks for the protoCore package under the name **`protocore`** (lowercase), which is how CPack installs the protoCore .deb. Also added fallback check for `protoCore`. The control template `Depends` was updated to `protocore (>= 1.0.0)` so installation succeeds when protoCore is installed from its CPack-generated .deb. Docs (INSTALLATION.md, PROCEDURES.md) updated accordingly.

- **protoCore getImportModule API** (2026-02-08): CommonJSLoader now passes `ProtoContext*` as the first argument to `ProtoSpace::getImportModule(context, logicalPath, attrName)` to match the current protoCore API (fixes build error when building against updated protoCore).

- **-Wformat-security warnings** (2026-02-08): All `JS_ThrowTypeError(ctx, dynamic_string.c_str())` calls replaced with `JS_ThrowTypeError(ctx, "%s", dynamic_string.c_str())` in CommonJSLoader, ESModuleLoader, IOModule, FSModule, and DNSModule so the format string is a literal and the compiler no longer reports format-security warnings.

- **require() built-in module resolution** (2026-02-08): `require('fs')`, `require('path')`, `require('stream')`, `require('crypto')`, `require('buffer')`, and other core modules now resolve from the global object so integration tests (fs, stream, crypto, buffer) pass. See CommonJSLoader built-in resolution and docs (README, NATIVE_MODULES).

- **GCBridge null-pointer handling** (2026-02-07): Fixed `-Wnonnull` compiler warnings and potential undefined behavior in `GCBridge::detectLeaks()` and `GCBridge::getMemoryStats()` when `ProtoContext` is null. Both functions now return early with null/empty values instead of dereferencing a null pointer. Added null checks in `reportLeaks()` and `getMemoryStats()` for defensive handling of empty reports.

- **Exception logging in eval** (2026-03-06): When compile or run failed, the code called `JS_GetException(ctx)` a second time to stringify the exception; the first call had already consumed it, so the second returned an uninitialized value that stringified as "[unsupported type]". Fixed by (1) capturing the exception inside `compileToBytecode` via an optional out-parameter as soon as `JS_Eval` returns an exception, and (2) using that value (or the one already in `val`) for logging in `eval`. Real exceptions from the compiler are now reported correctly when QuickJS sets them; when the compiler fails without setting an exception (e.g. some `__JS_EvalInternal` fail paths), the message may still be "undefined". Standard suite `__BENCH_RESULT__` remains unavailable until both compile success and `console.log` output are fixed in the protoCore path.

### Build & Test

- Full project recompilation: protoCore + protoJS clean build
- All 33 unit tests passing (ctest)
- Integration tests verified (hello_world, arithmetic, modules)

### Performance (2026-02-07)

- Performance suite executed successfully: `run_nodejs_comparison.js` (5/5 benchmarks)
- **Array operations:** 34–45x faster than Node.js (immutable structural sharing)
- **Overall speedup:** ~10–45x depending on workload
- Added [docs/PERFORMANCE_RUN_2026-02-07.md](docs/PERFORMANCE_RUN_2026-02-07.md) with run report and analysis

### Performance (2026-03-06)

- Re-ran Node.js comparison suite: 5/5 benchmarks passed; protoJS wins all 5.
- **Latest results:** array_operations 45x faster; overall speedup 10.81x (protoJS avg 42.6 ms vs Node 460.4 ms).
- Full combined suite (41 tests) run with Node.js; report and JSON written to `tests/benchmarks/results/report_2026-03-06_00-07-34.html` and `results_2026-03-06_00-07-34.json`.
- Updated [docs/PERFORMANCE_RUN_2026-02-07.md](docs/PERFORMANCE_RUN_2026-02-07.md) with latest run table; [docs/PERFORMANCE_REPORT.md](docs/PERFORMANCE_REPORT.md) with "Latest Node.js comparison", report paths, and new "Results analysis" section (Node comparison table + full-suite summary).
