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
| **CLI fixtures** | Shell cases registered directly with CTest (`cli/...`), for things no unit test can reach | `ctest --test-dir build -R "^cli/"` |
| **Test262 regression gate** | Expected-failures diff over `built-ins/{Object,Reflect,Proxy}` — the per-commit conformance gate | `python3 tests/test262/runner/regression_gate.py` |

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

## Continuous integration

`.github/workflows/ci.yml`. Two jobs, and which one a check sits in is the whole
design:

| Job | Gates? | What it runs |
|---|---|---|
| `test` | **yes** | protoCore and protoJS built with **no `-j`**, an `ldd` assertion that `libprotoCore.so.3` came from the workspace, `ctest -E "integration|network" < /dev/null`, and the Test262 regression gate |
| `informational` | no | the static conformance ratchet (which exits 1 by design), the mutable-cycle census, the case the gate excludes by name, and — on a tag, the nightly schedule or on demand — the whole 1 h 21 min Test262 corpus |

Nothing timing-sensitive gates: a runner is a shared machine, and in the other
five repositories of this family a test failed its own timing precondition on the
first real run with no defect present. protoJS's 34 registered Catch2 cases were
checked and contain no wall-clock assertion, so if one is ever added it belongs in
`informational`.

**No parallelism anywhere.** Every build omits `-j` and every Test262 invocation
sets `TEST262_CONCURRENCY=1`. A runner is not assumed to be exempt from the
constraint that hangs the development machine.

**Three pinned inputs.** protoCore is checked out as a sibling `../protoCore` at a
pinned SHA and built into `../protoCore/build_release` (the first path protoJS's
discovery searches); no `cmake --install` is involved. `tc39/test262` is a sibling
`../test262` at a pinned commit — not vendored, not a submodule. The corpus pin is
load-bearing: without it the denominator moves with upstream every week and a
failure diff becomes unattributable.

### The Test262 regression gate

`tests/test262/runner/regression_gate.py` compares the *set* of non-passing test
paths against `tests/test262/config/regression_gate_baseline.json` and fails on
any difference in either direction. It is deliberately **not** a percentage gate: a
commit that repairs one test and breaks another leaves 3,619 of 3,875 untouched,
so a percentage would report it green. A test that starts passing is also a
failure, because a baseline allowed to drift stops detecting the reverse; bank it
with `--update` in the same commit that earned it.

The gate also checks its own premise — the corpus must be at the pin and the
denominator unchanged — and reports that with exit code 2 rather than 1, because
it needs a different fix. `cli/test262-regression-gate-self-test` feeds the gate
synthetic snapshots and asserts all of that, including the rate-preserving swap.

Do not read the gate's 93.4 % as a conformance figure. Those three directories are
among protoJS's strongest; the conformance figure is the whole corpus, in
[docs/TEST262_STATUS.md](../docs/TEST262_STATUS.md).

## Results

- Authoritative Test262 figure and exclusion policy: [docs/TEST262_STATUS.md](../docs/TEST262_STATUS.md).
- Test262 subset measurements, all subordinate to that figure: [CONFORMANCE_JS.md](../CONFORMANCE_JS.md).
- Dated benchmark reports: [benchmarks/results/](benchmarks/results/).
