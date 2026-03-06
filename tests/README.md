# protoJS Testing

This document describes how to run each test layer and the current baseline (last validated run).

## Test layers

| Layer | What | How to run |
|-------|------|------------|
| **C++ unit** | Catch2 tests (EventLoop, CPUThreadPool, IOThreadPool, Semver, NPMRegistry, BenchmarkRunner, NodeJSTestRunner) | `cd build && ctest` or `ctest -E "integration\|network"` to exclude optional integration/network tests |
| **Smoke (protoCore)** | 6 expressions on protoCore path | `PROTOJS_USE_PROTO_EVAL=1 node tests/test262/runner/proto_eval_smoke.js` |
| **Phase 6 directed** | Native global script | `PROTOJS_USE_PROTO_EVAL=1 ./build/protojs --proto-eval tests/test262/tests/phase6_native_global.js` |
| **Test262** | Official Test262 by pattern | `TEST262_ROOT=../test262 node tests/test262/runner/test262_runner.js` (config: `tests/test262/config/test262_paths.json`). Use `TEST262_USE_PROTO_EVAL=1` for protoCore path. Snapshots: `tests/test262/reports/` |
| **Integration** | Manual scripts (fs, stream, modules, etc.) | `./build/protojs tests/integration/<module>/<script>.js` |
| **Conformity** | Builtins/import checks | `./build/protojs tests/conformity/builtins/<script>.js` or `tests/conformity/import/<script>.js` |

## Single entry point

From the repo root:

```bash
./tests/run_all_tests.sh
```

This builds (if needed), runs C++ unit tests (excluding `[.integration]` and `[.network]`), runs the smoke test, and optionally the Phase 6 script and a small Test262 pattern when `TEST262_ROOT` is set. Exits non-zero if any step fails.

## Baseline (last validated)

**Date:** 2026-03-06

| Layer | Result |
|-------|--------|
| C++ unit (excl. integration/network) | 32/32 passed (33 total; 1 tagged integration/network excluded) |
| Smoke (protoCore) | 6/6 passed |
| Phase 6 directed | 1/1 passed (exit 0) |
| Test262 `built-ins/Array/isArray` (protoCore) | 29 passed, 0 failed, 0 timeout |
| Integration (sample) | `test_require.js`, `test_fs.js` — exit 0 |
| Conformity (sample) | `test_array_conformity.js`, `test_object_conformity.js` — exit 0 |

Integration and conformity are not run automatically by `run_all_tests.sh`; run them manually as needed.
