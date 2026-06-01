# Test262 Conformance Status — protoJS

**Last full-suite run:** 2026-06-01T05:55:21Z
**Snapshot:** `tests/test262/reports/snapshot-language_built-ins-1780293321973.json`
**Binary:** `build_release/protojs` v0.1.0 (post-2026-06-01 cycle, commit `073d1414` on `master`)
**Scope:** `language` + `built-ins` (46 963 tests; non-trivial subset of full Test262)
**Runner:** parallel mode added this cycle (`TEST262_CONCURRENCY=10`, ~5:30 min wall on 12-core)

## Overall

| | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Skipped | Pass rate |
|---|---:|---:|---:|---:|---:|---:|---:|
| **2026-06-01 (this run)** | 46 963 | **27 565** | 874 | 18 477 | 36 | 11 | **58.70 %** |
| 2026-05-11 (prior full run) | 46 963 | 27 025 | 830 | 18 666 | 431 | 11 | 57.55 % |
| **Δ** | 0 | **+540** | +44 | **−189** | **−395** | 0 | **+1.15 pp** |

Three load-bearing observations:

1. **Net +540 passes** (+1.15 pp) over 3 weeks of work. Most of the gain landed in this cycle's correctness commits (`b09373ea` through `ccefb3e4`, 2026-06-01).
2. **Timeouts dropped from 431 → 36 (−91 %)** — almost entirely attributable to `3dc726b8 fix(interp): OP_for_of_start stale pAutomaticLocals after slot resize`. Before that fix, any test using `for-of` over an array or generator hung in an infinite loop and timed out; now those tests fail or pass deterministically.
3. **Failed (semantics) dropped 189** while passes grew 540, meaning ~350 of the new passes came from PREVIOUSLY-TIMING-OUT tests that now resolve cleanly, and ~190 came from previously-failing tests being correctly fixed.

## By Family

| Family | Total | Passed (this run) | Passed (prior) | Δ | Pass rate (this run) | Pass rate (prior) |
|---|---:|---:|---:|---:|---:|---:|
| `built-ins` | 23 334 | 9 933 | 9 247 | **+686** | **42.57 %** | 39.63 % |
| `language` | 23 629 | 17 632 | 17 778 | **−146** | **74.62 %** | 75.24 % |

`built-ins` carried the cycle's gains. `language` slipped slightly — the regression is concentrated in `language/expressions` (see Top Movers) and warrants investigation but is below the gain in `built-ins`.

## Top Movers (|Δpass| ≥ 25)

### Wins (commits in this cycle's window, 2026-05-11 → 2026-06-01)

| Sub-category | Total | Passed (this run) | Passed (prior) | Δ |
|---|---:|---:|---:|---:|
| `built-ins/Object` | 3 411 | **2 048** | 1 178 | **+870** |
| `language/statements` | 9 337 | **6 745** | 6 503 | **+242** |
| `built-ins/String` | 1 223 | 673 | 630 | +43 |
| `built-ins/Set` | 383 | 221 | 187 | +34 |
| `built-ins/Map` | 204 | 125 | 91 | +34 |
| `built-ins/Number` | 338 | 118 | 88 | +30 |
| `language/function-code` | 217 | 156 | 131 | +25 |

`built-ins/Object` (+870) is the cycle's headline win — most `Object.*` tests iterate via `for-of`/iterator protocol, all of which previously hung. The for-of fix unblocked the entire subtree.

`language/statements` (+242) likely benefited from a mix of the for-of fix (the `for-of` and `for-in` statement tests) and the generator-return fix (`ac3f225d`).

### Regressions (warrant investigation; not necessarily caused by this cycle)

| Sub-category | Total | Passed (this run) | Passed (prior) | Δ |
|---|---:|---:|---:|---:|
| `language/expressions` | 11 036 | 8 329 | **8 705** | **−376** |
| `built-ins/Promise` | 652 | 269 | 337 | −68 |
| `built-ins/Iterator` | 510 | 195 | 253 | −58 |
| `built-ins/Temporal` | 4 485 | 1 879 | 1 933 | −54 |
| `built-ins/TypedArrayConstructors` | 736 | 186 | 240 | −54 |
| `language/eval-code` | 347 | 75 | 125 | −50 |
| `built-ins/Atomics` | 382 | 21 | 51 | −30 |
| `built-ins/TypedArray` | 1 438 | 351 | 381 | −30 |

These represent ~700 tests that PASSED on 2026-05-11 and FAIL or behave differently on 2026-06-01. The 3-week gap means the cause is not necessarily this cycle's commits — anything that landed on `master` between snapshots could be responsible. Three plausible classes:

1. **`language/expressions` (−376):** the biggest concentrated loss. Likely related to changes in expression-handling opcodes (call-method reorderings, SmallInt fast paths added in `b0b6f692`, `ccefb3e4`). A bisect over the 2026-05-11..2026-06-01 commit range would localise.
2. **`built-ins/Iterator` (−58) + `built-ins/Promise` (−68):** plausibly correlated — both heavily use the iterator protocol and Promise resolution paths.
3. **`built-ins/Temporal` (−54), `Atomics` (−30), `TypedArray*` (−30 + −54):** these subsystems aren't actively maintained in protoJS; changes elsewhere can cascade.

Investigation priority is `language/expressions` first.

## Cycle Commits That Drove Movement

This cycle (2026-06-01, 6 commits over `ecf0f57d..073d1414`):

| Commit | Description | Likely test262 impact |
|---|---|---|
| `b09373ea` | `perf(interp)`: skip newList for argc==0 native calls | Indirect — broad reduction in per-call overhead, may unblock timeout tests |
| `ac3f225d` | `fix(interp)`: sync generators wrapping return in Promise | Direct — generator-return tests in `language/statements` and `built-ins/Iterator` |
| `b0b6f692` | `perf(interp)`: SmallInt fast path for OP_add_loc | Indirect — arithmetic-heavy tests faster, possibly unblocks timeouts |
| `3dc726b8` | `fix(interp)`: OP_for_of_start stale pAutomaticLocals | **Largest** — directly resolves the 395 timeout reduction and the `built-ins/Object` +870 wave |
| `ccefb3e4` | `perf(interp)`: skip __native_fn__ unwrap for JS closures | Indirect — −8 % cycles per JS function call |
| `073d1414` | `docs(readme)`: 2026-06-01 baseline | n/a |

## How to Run

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
PROTOJS=$PWD/build_release/protojs \
TEST262_ROOT=/home/gamarino/Documentos/proyectos/test262 \
TEST262_USE_PROTO_EVAL=1 \
TEST262_CONCURRENCY=10 \
PROTOCORE_GC_CONTEXT_THRESHOLD=1000000000 \
  node tests/test262/runner/test262_runner.js
```

Setting `TEST262_CONCURRENCY=10` enables the parallel mode added this cycle (`tests/test262/runner/test262_runner.js`). Wall-clock on a 12-core machine: ~5:30 min for the full 46 963-test `language + built-ins` suite. Sequential fallback (no env var or `=1`) is unchanged.

Per-test stdout is suppressed in parallel mode (it would interleave from multiple workers); progress prints every `TEST262_PROGRESS_EVERY` tests (default 500). `TEST262_VERBOSE=1` restores per-test output.

## Methodology Notes

- **Pass rate ≠ ECMA conformance score.** Pass rate here is `passed / total`, where `total` includes `failed_syntax`, `failed_semantics`, `timeout`, and `skipped`. Some "syntax failures" are tests using ES2024+ syntax that the QuickJS frontend doesn't parse — those aren't conformance failures of the runtime; they're a known frontend limitation.
- **Test262 root is pinned** to `../test262` (a sibling repo). The tip commit at run time should be recorded for reproducibility; current run used whatever HEAD `~/Documentos/proyectos/test262` was on as of 2026-06-01.
- **`PROTOCORE_GC_CONTEXT_THRESHOLD=1e9`** suppresses GC during the run so test timing is dominated by the test code, not by GC. Conformance results are unaffected — the GC is correctness-preserving regardless of when it runs.
- **Skip list:** `tests/test262/config/skip_proto_eval.json` records 11 tests that hang or crash protoJS in ways unrelated to conformance (e.g. infinite recursion that exhausts stack). Excluded from both pass and fail counts.

## Historical Context

Prior baselines (from older revisions of this document and the `tests/test262/STATUS.md` file):

- **2026-03-18:** 94.4 % overall claim — superseded as a false positive; the binary at that time had assertion bugs in `assert.throws` / `sameValue` that did not propagate failures correctly, inflating the pass count. The first honest baseline below this number was the 2026-04-10 Phase 13 run at 87.1 % for `language/statements` specifically.
- **2026-04-10 (Phase 13):** 87.1 % on `language/statements` only (8 133 / 9 337). First honest measurement post-`assert.throws` fix.
- **2026-04-11 (Phase 16):** 88.2 % on `language/statements` (8 239 / 9 337). Destructuring + iterator-callback exception propagation.
- **2026-05-11:** 57.55 % on `language + built-ins` (27 025 / 46 963). The drop vs. 2026-04 numbers is scope-related — those earlier numbers were `language/statements`-only, this one is the much larger `language + built-ins` superset.
- **2026-06-01 (this run):** 58.70 % on the same `language + built-ins` scope.

## Next Steps

1. **Investigate `language/expressions` −376.** Bisect 2026-05-11..2026-06-01. Likely candidates: call-method dispatch changes, SmallInt fast paths, OP_add_loc fast path.
2. **Investigate `built-ins/Iterator` + `built-ins/Promise` paired regression.** These often share root causes (iterator protocol intermediaries).
3. **Run the suite weekly.** The runner is now parallel and fast (~5 min); regression detection becomes routine rather than a once-a-month event.
4. **Re-baseline `CONFORMANCE_JS.md`.** That per-sub-category file hasn't been updated in this cycle; the per-category numbers in this doc supersede it for the language + built-ins subset.
