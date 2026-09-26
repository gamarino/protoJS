# protoJS Testing Strategy

**Last reviewed:** 2026-09-15

---

## Testing Goals

1. **Validate functionality:** Check C++ runtime components and JavaScript-level behaviour.
2. **Track conformance:** Measure ECMAScript conformance with Test262 on the protoCore execution path.
3. **Prevent regressions:** Detect interpreter and runtime breakage early with fast smoke and directed tests.
4. **Measure performance:** Compare protoJS with Node.js and QuickJS on a standard benchmark suite.
5. **Document behaviour:** Keep scripts that double as executable examples.

---

## Test Layout

```
tests/
├── CMakeLists.txt          # Catch2 unit test target (protojs_tests)
├── README.md               # How to run each layer; last validated baseline
├── run_all_tests.sh        # Single entry point (see "Success Criteria")
├── unit/                   # C++ unit tests (Catch2)
│   ├── test_main.cpp
│   ├── test_event_loop.cpp
│   ├── test_thread_pools.cpp
│   ├── test_io_thread_pool.cpp
│   ├── test_semver.cpp
│   ├── test_npm_registry.cpp
│   ├── test_benchmark_runner.cpp
│   ├── test_nodejs_test_runner.cpp
│   └── test_array_storage_microbench.cpp
├── integration/            # JavaScript scripts, one directory per area
│   ├── basic/  buffer/  collections/  crypto/  deferred/  dgram/
│   ├── fs/  http/  modules/  native_addons/  net/  profiling/  stream/
│   └── test_deferred_basic.js
├── conformity/             # Built-in and module-identity checks
│   ├── builtins/  import/  bootstrap/
│   └── run_conformity.js
├── test262/                # Test262 tooling (the suite itself is not vendored)
│   ├── runner/             # test262_runner.js, proto_eval_smoke.js, batch scripts
│   ├── config/             # test262_paths.json, skip lists
│   ├── harness/            # assert.js, sta.js
│   └── tests/              # Directed tests (phase6_native_global.js, language/, built-ins/)
├── benchmarks/             # Standard suite, comparison runners, dated results
│   ├── standard/
│   └── results/
├── demos/                  # Demonstration scripts
├── native_addons/          # Sources of the test addons built by CMakeLists.txt
├── manual/  scripts/       # Ad-hoc scripts
└── test_*.js, test_getfirstparent.cpp, run_gdb.sh, run_gdb_batch.sh
                            # Ad-hoc debugging helpers, not registered with CTest
```

---

## C++ Unit Tests

### Framework: Catch2 with CTest

`BUILD_TESTING` is `ON` by default. `CMakeLists.txt` uses an installed Catch2 package or fetches it with `FetchContent`. `tests/CMakeLists.txt` then builds `protojs_tests` from `tests/unit/*.cpp`, links it against `protojs_core`, and registers every test case with CTest through `catch_discover_tests`.

**Coverage by file:**

| File | Component | Test cases |
|------|-----------|-----------|
| `test_event_loop.cpp` | `EventLoop` | 3 |
| `test_thread_pools.cpp` | `ThreadPoolExecutor`, `CPUThreadPool`, `IOThreadPool` | 3 |
| `test_io_thread_pool.cpp` | `IOThreadPool` | 2 |
| `test_semver.cpp` | `Semver` (npm version resolution) | 5 |
| `test_npm_registry.cpp` | `NPMRegistry` | 3 |
| `test_benchmark_runner.cpp` | `BenchmarkRunner` | 9 |
| `test_nodejs_test_runner.cpp` | `NodeJSTestRunner` | 11 |
| `test_array_storage_microbench.cpp` | Array element storage micro-benchmark | 1 |

Four of the 38 test cases carry Catch2 hidden tags: two `[.integration]`, one `[.network]` and one `[.bench]`. Hidden test cases are not discovered by default, so CTest registers 34 tests. Hidden cases can be run explicitly by tag, for example `./build/tests/protojs_tests "[.integration]"`.

**Example: `tests/unit/test_semver.cpp`**

```cpp
#include <catch2/catch_all.hpp>
#include "../../src/npm/Semver.h"

using namespace protojs;

TEST_CASE("Semver::parse", "[Semver][Phase6]") {
    int major, minor, patch;
    std::string prerelease, build;

    SECTION("valid full version") {
        REQUIRE(Semver::parse("1.2.3", major, minor, patch, prerelease, build));
        REQUIRE(major == 1);
        REQUIRE(minor == 2);
        REQUIRE(patch == 3);
    }
}
```

**Running:**

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure -E "integration|network"
```

---

## Integration Tests (JavaScript)

### Framework: Run scripts and inspect their output

Integration scripts are run directly with the `protojs` binary. Most of them print results rather than assert, so a run is judged by its exit status and output. `run_all_tests.sh` does not run them.

```bash
./build/protojs tests/integration/fs/test_fs.js
./build/protojs tests/integration/modules/test_require.js
```

**Example: `tests/integration/basic/hello_world.js`**

```javascript
// Basic hello world test

console.log("Hello, World!");
console.log("protoJS is running!");

const x = 10;
const y = 20;
const sum = x + y;
console.log(`${x} + ${y} = ${sum}`);
```

The native addon tests in `tests/integration/native_addons/` load `simple_addon` and `fixture_addon`. Both are shared libraries that `CMakeLists.txt` builds from `tests/native_addons/`, against ABI v2: they use protoCore objects only and include no QuickJS header. `test_native_require.js` checks that an exported function is callable and that an exception raised by the addon is catchable; `test_resolution.js` checks that a native addon is preferred over a sibling `.js`. `tests/unit/test_dynamic_library_loader.cpp` covers the ABI check itself, including the rejection of a v1 addon.

---

## Conformance Tests

### Smoke test

`tests/test262/runner/proto_eval_smoke.js` runs `protojs -e <code>` on six short cases:
- arithmetic
- `typeof` on a number and on a function
- comparison
- `Array.isArray`
- a top-level `var` on the native global

It fails if any case exits with a non-zero status. Run it after every interpreter change:

```bash
node tests/test262/runner/proto_eval_smoke.js
```

The `protojs` binary accepts `--proto-eval` as a deprecated no-op and does not read `PROTOJS_USE_PROTO_EVAL`. The smoke script, the directed Phase 6 test and `run_all_tests.sh` no longer pass either of them; the protoCore path is always active. `tests/integration/cli/test_cli_flags.js` checks that the flag is still accepted, that `-c` / `--check` parses without executing, and that the usage text matches the implemented I/O thread default.

### Directed tests

`tests/test262/tests/` holds small Test262-style tests: `phase6_native_global.js`, plus cases under `language/` and `built-ins/`.

### Test262

`tests/test262/runner/test262_runner.js` performs these steps:

1. Reads `tests/test262/config/test262_paths.json`. By default this sets the Test262 root to `../test262`, the patterns to `language` and `built-ins`, a 5000 ms timeout, and `use_proto_eval: true`.
2. Discovers tests and parses their front matter.
3. Prepends `assert.js`, `sta.js` and any required includes.
4. Runs each test with the `protojs` binary. It sets `PROTOJS_NO_FALLBACK=1`, so a compile failure is reported instead of being hidden by the QuickJS fallback.
5. Writes a JSON snapshot to `tests/test262/reports/`.

The Test262 suite is not part of this repository; clone it next to protoJS or set `TEST262_ROOT`.

**Environment overrides:**

| Variable | Effect |
|----------|--------|
| `TEST262_ROOT` | Location of the Test262 checkout |
| `TEST262_PATTERNS` | Comma-separated test patterns |
| `PROTOJS` | Path to the `protojs` binary |
| `TEST262_USE_PROTO_EVAL` | Forces the protoCore path (already the configured default) |
| `TEST262_CONCURRENCY` | Number of tests run in parallel (default 1) |
| `TEST262_VERBOSE=1` | Prints one line per test |

```bash
TEST262_ROOT=../test262 TEST262_PATTERNS=built-ins/Array/isArray \
  node tests/test262/runner/test262_runner.js
```

Results and methodology are recorded in [CONFORMANCE_JS.md](CONFORMANCE_JS.md) and [docs/TEST262_STATUS.md](docs/TEST262_STATUS.md). A full-suite run starts one `protojs` process per test and can take a long time; use patterns for day-to-day work.

### Conformity suite

`tests/conformity/` holds these checks:
- `builtins/`: Number, String, Array and Object conformity scripts.
- `import/`: module identity and repeated `require`.
- `bootstrap/`: a manifest of a minimal Test262 subset.

`node tests/conformity/run_conformity.js` runs them; set `PROTOJS` to choose the binary.

---

## Benchmarks

### Framework: Compare with Node.js and QuickJS

The standard suite in `tests/benchmarks/standard/` consists of self-contained scripts that run unchanged under `node` and `protojs`. Each script prints a final `__BENCH_RESULT__<json>` line with an in-process `time_ms` (median of several runs). The runners compare that time rather than wall-clock time.

```bash
# protoJS vs Node.js; writes tests/benchmarks/results/standard_comparison.json
node tests/benchmarks/run_standard_comparison.js

# protoJS vs QuickJS
node tests/benchmarks/run_standard_comparison_quickjs.js
```

The runners look for `build_release/protojs` first, then `build/protojs`. Workloads and details are listed in [tests/benchmarks/standard/README.md](tests/benchmarks/standard/README.md). Dated result files are kept in `tests/benchmarks/results/`. The parallel CPU benchmark (`parallel_cpu.js`) uses `protoCore.runInThread` when available. It runs the same workload (2e6 iterations per task) in every runtime and prints the work performed and the executor that ran it, but it compares protoCore's native multithreaded worker with a sequential JavaScript loop, so it is not a like-for-like engine comparison. `tests/integration/benchmarks/test_parallel_cpu_workload.js` asserts that the protojs and Node.js arms run the same work.

---

## Demonstration Scripts

`tests/demos/` contains short scripts that print what they do:

| Script | Shows |
|--------|-------|
| `deferred_demo.js` | Creating a `Deferred` |
| `immutable_arrays.js` | `concat` returning a new array while the original is unchanged |
| `protoCore_collections.js` | `protoCore.Set` and `protoCore.Multiset` |
| `test_virtual_threads.js` | Availability of `Deferred`, the `io` module and thread-pool options |

The scripts use the APIs directly. `protoCore.Set`, `protoCore.Multiset` and the other collections are installed on the protoCore-native global by `src/ProtoCoreNativeBindings.cpp`; `tests/integration/collections/protoCore_collections.js` asserts their behaviour.

```bash
./build/protojs tests/demos/immutable_arrays.js
```

---

## Success Criteria

`tests/run_all_tests.sh` is the pass/fail gate. It stops at the first failing step:

1. `cmake --build build`
2. `ctest --output-on-failure -E "integration|network"`
3. The smoke test (`proto_eval_smoke.js`)
4. The directed test `tests/test262/tests/phase6_native_global.js`
5. The Test262 patterns from the configuration, only when `TEST262_ROOT` points to a Test262 checkout

The binary and build directory can be overridden with `PROTOJS` and `BUILD_DIR`. The last validated baseline is recorded in [tests/README.md](tests/README.md).

---

## Testing Tools

### C++ tests

- **Framework:** Catch2, registered with CTest.
- **Coverage:** configure with `-DENABLE_COVERAGE=ON` to add `--coverage` instrumentation. No report target is defined, so reports are produced with external tools that read gcov data.
- **Debugging:** `tests/run_gdb.sh` and `tests/run_gdb_batch.sh`.
- **Sanitizers:** no dedicated CMake option; pass the flags through `CMAKE_CXX_FLAGS` when needed.

### JavaScript tests

- **Runner:** the `protojs` binary for individual scripts. Node.js scripts drive the smoke test, Test262, the conformity suite and the benchmarks.
- **Assertions:** Test262 tests use the harness in `tests/test262/harness/`; most integration scripts print results.

### Benchmarks

- **Framework:** the standard suite and its comparison runners.
- **Comparison engines:** Node.js and QuickJS.
- **Metric:** in-process `time_ms` per benchmark.

---

## Testing Process

### Development

1. Add or update a test that covers the change: a unit test for C++ components, a directed or Test262 pattern for language semantics.
2. Implement the change.
3. Run the smoke test and the relevant Test262 patterns.
4. Iterate until they pass.

### Before submitting changes

1. Run `tests/run_all_tests.sh`.
2. For interpreter or built-in changes, rerun the affected Test262 patterns and compare with the previous snapshot.
3. For performance-sensitive changes, run the standard benchmark comparison.

### Continuous integration

No CI workflow is configured in this repository; the steps above are run locally.

---

## Metrics to Track

1. **Unit tests:** CTest pass count.
2. **Conformance:** one authoritative Test262 pass rate over the whole corpus, with its denominator, corpus commit and exclusion policy (`docs/TEST262_STATUS.md`). Per-pattern rates (JSON snapshots, `CONFORMANCE_JS.md`) are subset diagnostics and are never quoted as the conformance figure.
3. **Performance:** per-benchmark `time_ms` and the ratios reported by the comparison runners.
4. **Coverage:** line coverage when built with `ENABLE_COVERAGE`.
5. **Stability:** crashes and timeouts reported by the Test262 runner.

---

## Known Gaps

1. There are no unit tests for `TypeBridge`, `GCBridge`, `Deferred` / `ProtoDeferred` or `ProtoInterpreter`; interpreter correctness is covered by the smoke, directed and Test262 tests.
2. Integration, conformity and demonstration scripts are not run by `run_all_tests.sh`, and most integration scripts do not assert.
3. There is no coverage report target and no CI workflow.
