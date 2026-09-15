# protoJS Testing

How to run each test layer. Commands are run from the repository root and assume the build directory `build/`.

## Test layers

| Layer | What | How to run |
|-------|------|------------|
| **C++ unit** | Catch2 tests in `tests/unit/`: event loop, CPU and I/O thread pools, Semver, NPMRegistry, BenchmarkRunner, NodeJSTestRunner | `ctest --test-dir build --output-on-failure` |
| **Smoke** | Six short expressions evaluated by `protojs` | `node tests/test262/runner/proto_eval_smoke.js` |
| **Directed global-object script** | Global `var` writes and reads on the protoCore-native global | `./build/protojs tests/test262/tests/phase6_native_global.js` |
| **Test262** | Official Test262 suite, selected by path pattern | `node tests/test262/runner/test262_runner.js` (see below) |
| **Integration** | Scripts per module area under `tests/integration/` | `./build/protojs tests/integration/<area>/<script>.js` |
| **Conformity** | Built-in and module-identity checks | `node tests/conformity/run_conformity.js`; see [conformity/README.md](conformity/README.md) |
| **Benchmarks** | Standard benchmark suite and comparison runners | See [benchmarks/standard/README.md](benchmarks/standard/README.md) |

### C++ unit tests

The test executable is `build/tests/protojs_tests`. CMake registers its Catch2 test cases with CTest through `catch_discover_tests`. Four test cases are hidden with Catch2 tags and are not registered: two tagged `[.integration]`, one `[.network]` (needs network access) and one `[.bench]`. Run them explicitly by tag:

```bash
./build/tests/protojs_tests "[.integration]"
```

### Test262

The runner reads `tests/test262/config/test262_paths.json`. By default it expects a Test262 checkout at `../test262` (next to the protoJS repository) and runs the patterns `language` and `built-ins`, which is the full suite used in [TEST262_STATUS.md](../docs/TEST262_STATUS.md). Environment variables override the configuration:

| Variable | Effect |
|----------|--------|
| `PROTOJS` | Path of the `protojs` binary (default: `build/protojs`, then `./protojs`) |
| `TEST262_ROOT` | Test262 checkout (relative paths are resolved from the repository root) |
| `TEST262_PATTERNS` | Comma-separated patterns, for example `built-ins/Array/isArray` |
| `TEST262_CONCURRENCY` | Number of tests run in parallel |
| `TEST262_VERBOSE`, `TEST262_PROGRESS_EVERY` | Progress output |

```bash
TEST262_PATTERNS=built-ins/Array/isArray node tests/test262/runner/test262_runner.js
```

The runner skips the tests listed in `tests/test262/config/skip_proto_eval.json` and writes a JSON snapshot for each run to `tests/test262/reports/`, which is not tracked in git. Its classification rules (including parse-negative leniency) are described in [CONFORMANCE_JS.md](../CONFORMANCE_JS.md#1-scope-and-methodology).

The runner, the smoke script and `run_all_tests.sh` set `PROTOJS_USE_PROTO_EVAL=1`; `protojs` does not read that variable, and the protoCore interpreter is always used. For non-module tests the Test262 runner also sets `PROTOJS_NO_FALLBACK=1`, so a protoCore compile failure is reported instead of being retried with QuickJS.

### Native addons

The build produces two test addons: `build/tests/native_addons/simple/simple.so` and `tests/integration/native_addons/fixture.so`. The scripts in `tests/integration/native_addons/` load them.

## Single entry point

```bash
./tests/run_all_tests.sh
```

The script builds `build/` (override with `BUILD_DIR`; the binary with `PROTOJS`), runs the C++ unit tests with `ctest -E "integration|network"`, runs the smoke test and the directed global-object script, and, when `TEST262_ROOT` is set, runs the Test262 runner with the configured patterns. With the default configuration that is the full `language` and `built-ins` suite, which is a long run. The script exits non-zero if any step fails.

Integration, conformity and benchmark scripts are not run by `run_all_tests.sh`.

## Results

- Latest full Test262 run: [docs/TEST262_STATUS.md](../docs/TEST262_STATUS.md).
- Test262 subset measurements: [CONFORMANCE_JS.md](../CONFORMANCE_JS.md).
- Dated benchmark reports: [benchmarks/results/](benchmarks/results/).
