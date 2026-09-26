# protoJS Testing

How to run each test layer. Commands are run from the repository root and assume the build directory `build/`.

## Test layers

| Layer | What | How to run |
|-------|------|------------|
| **C++ unit** | Catch2 tests in `tests/unit/`: event loop, CPU and I/O thread pools, Semver, NPMRegistry, BenchmarkRunner, NodeJSTestRunner, native addon ABI check | `ctest --test-dir build --output-on-failure` |
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

The runner reads `tests/test262/config/test262_paths.json`. By default it expects a Test262 checkout at `../test262` (next to the protoJS repository) and runs the patterns `language` and `built-ins`. That default is a **partial** run: protoJS's authoritative figure covers the whole corpus and is documented, with its exclusion policy, in [TEST262_STATUS.md](../docs/TEST262_STATUS.md). Environment variables override the configuration:

| Variable | Effect |
|----------|--------|
| `PROTOJS` | Path of the `protojs` binary (default: `build/protojs`, then `./protojs`) |
| `TEST262_ROOT` | Test262 checkout (relative paths are resolved from the repository root) |
| `TEST262_PATTERNS` | Comma-separated patterns, for example `built-ins/Array/isArray` |
| `TEST262_CONCURRENCY` | Number of tests run in parallel. Keep it at `1`: parallel Test262 runs hang the development machine |
| `TEST262_LENIENT` | `1` restores the pre-2026-09 classification (parse-negatives always pass, async tests judged by exit code). For reproducing historical figures only |
| `TEST262_VERBOSE`, `TEST262_PROGRESS_EVERY` | Progress output |

```bash
TEST262_PATTERNS=built-ins/Array/isArray node tests/test262/runner/test262_runner.js
```

The whole corpus, which is what the authoritative figure measures (about 1 h 21 min):

```bash
TEST262_ROOT=../test262 \
TEST262_CONCURRENCY=1 \
TEST262_PATTERNS=annexB,built-ins,harness,intl402,language,staging \
PROTOJS=./build_release/protojs \
node tests/test262/runner/test262_runner.js
```

The runner skips the tests listed in `tests/test262/config/skip_proto_eval.json` and writes a JSON snapshot for each run to `tests/test262/reports/`, which is not tracked in git. Every run ends by printing its patterns, corpus commit, denominator, per-category failure counts, pass rate and wall clock, and the snapshot carries the same fields — so a figure can always be traced back to what produced it. Results are classified `passed`, `failed_syntax`, `failed_semantics`, `failed_negative` (the engine accepted source Test262 requires it to reject), `failed_async` (no `Test262:AsyncTestComplete` marker), `timeout` or `skipped`; the rules are in [TEST262_STATUS.md](../docs/TEST262_STATUS.md#exclusion-policy).

The Test262 runner still sets `PROTOJS_USE_PROTO_EVAL=1`; `protojs` does not read that variable, and the protoCore interpreter is always used. The smoke script and `run_all_tests.sh` no longer set it, and no longer pass the deprecated `--proto-eval` flag. For non-module tests the Test262 runner also sets `PROTOJS_NO_FALLBACK=1`, so a protoCore compile failure is reported instead of being retried with QuickJS.

### Native addons

The build produces two test addons: `<build>/tests/native_addons/simple/simple.so` (the scripts look under `build_release/` and `build/`) and `tests/integration/native_addons/fixture.so`. Both are built against ABI v2, so they use protoCore objects only and include no QuickJS header. The scripts in `tests/integration/native_addons/` load them and assert: `test_native_require.js` checks a callable export and a catchable addon exception, `test_resolution.js` checks that a native addon wins over a sibling `.js`.

## Single entry point

```bash
./tests/run_all_tests.sh
```

The script builds `build/` (override with `BUILD_DIR`; the binary with `PROTOJS`), runs the C++ unit tests with `ctest -E "integration|network"`, runs the smoke test, the directed global-object script and the command-line tests (`tests/integration/cli/test_cli_flags.js`), and, when `TEST262_ROOT` is set, runs the Test262 runner with the configured patterns. With the default configuration that is the full `language` and `built-ins` suite, which is a long run. The script exits non-zero if any step fails.

Integration, conformity and benchmark scripts are not run by `run_all_tests.sh`.

## Results

- Authoritative Test262 figure and exclusion policy: [docs/TEST262_STATUS.md](../docs/TEST262_STATUS.md).
- Test262 subset measurements, all subordinate to that figure: [CONFORMANCE_JS.md](../CONFORMANCE_JS.md).
- Dated benchmark reports: [benchmarks/results/](benchmarks/results/).
