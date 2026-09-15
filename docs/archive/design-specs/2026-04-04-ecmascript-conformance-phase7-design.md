# Design Specification: ECMAScript Conformance Phase 7 — Quick Wins

**Date:** 2026-04-04  
**Topic:** Raising Test262 conformance in RegExp, Date, and Promise built-ins  
**Status:** Approved  

---

## Objective

Raise Test262 pass rate from 75.5% toward a higher baseline by targeting the three built-in categories
with the best effort-to-impact ratio. Approach is diagnosis-first: run the test suite per category,
group failures by root cause, and implement fixes in descending order of test gain.

| Area | Baseline | Target | Approx. tests to gain |
|------|----------|--------|-----------------------|
| RegExp | 59.3% (2,592 / 4,374) | 90% (~3,937) | ~1,345 |
| Date | 62.3% (966 / 1,551) | 90% (~1,396) | ~430 |
| Promise | 47.9% (653 / 1,364) | 90% (~1,228) | ~575 |
| **Total** | | | **~2,350** |

---

## 1. Process (Diagnosis-First Loop)

For each area, the cycle is:

1. **Diagnose** — run the Test262 subset for that category, capture failures, group by error message / missing feature.
2. **Prioritize** — rank groups by frequency; identify the top 5 root causes.
3. **Implement** — fix root causes in descending impact order, one at a time.
4. **Verify** — re-run tests after each fix; confirm no regressions in previously-passing tests.
5. **Repeat** — until 90%+ is reached or remaining failures require major redesign.

Each area is an independent cycle. RegExp completes before Date begins; Date before Promise.

**Commit discipline:** one commit per feature or fix, with a descriptive message following the
`feat(regexp):`, `fix(date):`, `fix(promise):` convention. This makes bisecting and reviewing
incremental progress straightforward.

---

## 2. RegExp (59.3% → 90%+)

The previous spec (`2026-03-20-regexp-builtins-design.md`) implemented the core RegExp API using
`libregexp`. The remaining ~1,800 failures target features not covered by that spec.

**Likely root causes (to confirm via diagnosis):**

| Feature | Estimated impact | Notes |
|---------|-----------------|-------|
| `RegExpStringIterator` + `String.prototype.matchAll` | +300–500 tests | Iterator protocol for global/sticky RegExp |
| `d` flag (`hasIndices`) — ES2022 | +150–250 tests | Adds `indices` array to `exec()` results |
| Named capture groups — edge cases | +100–200 tests | `(?<name>...)`, `\k<name>`, groups in `replace` |
| `RegExp.prototype[Symbol.matchAll]` | +50–100 tests | Delegates to `RegExpStringIterator` |
| `lastIndex` coercion and step semantics | +50–100 tests | Spec-compliant updates in `exec()` loop |
| `RegExp.prototype.dotAll` / `s` flag edge cases | +30–60 tests | |

**Files expected:** `src/RegExpPrototype.cpp`, new `src/RegExpStringIterator.cpp` (and `.h`).

**Risk:** If `d` flag requires plumbing through `libregexp`, the cost may be higher than estimated.
Diagnosis will reveal whether `libregexp` exposes indices natively.

---

## 3. Date (62.3% → 90%+)

**Likely root causes (to confirm via diagnosis):**

| Feature | Estimated impact | Notes |
|---------|-----------------|-------|
| ISO 8601 parsing edge cases (timezone offsets, `Z`, sub-ms) | +100–150 tests | |
| `Date[Symbol.toPrimitive]` with `"default"` hint | +50–80 tests | Must behave as `"number"` |
| `Date.prototype.toJSON` edge cases | +30–50 tests | |
| `Date.UTC()` with missing/NaN arguments | +30–50 tests | |
| Legacy `getYear` / `setYear` behavior | +20–40 tests | |
| Extreme timestamps (pre-epoch, year > 9999) | +20–40 tests | |
| `toLocaleDateString` / `toLocaleTimeString` (Intl-dependent) | variable | If failures depend on `Intl`, scope is limited to non-Intl tests |

**Files expected:** `src/DatePrototype.cpp` (or equivalent).

**Scope note:** If a significant portion of Date failures depend on `Intl` (internationalization),
those will be deferred — `Intl` is a separate large effort. Diagnosis determines this early.

---

## 4. Promise (47.9% → 90%+)

**Likely root causes (to confirm via diagnosis):**

| Feature | Estimated impact | Notes |
|---------|-----------------|-------|
| `Symbol.species` propagation in subclasses | +200–300 tests | Affects `then`, `catch`, `finally` return type |
| `Promise.any` + `AggregateError` completeness | +80–120 tests | |
| `Promise.allSettled` edge cases | +40–60 tests | |
| Unhandled rejection events (`unhandledrejection`, `rejectionhandled`) | +40–80 tests | |
| `Promise.prototype.finally` return value semantics | +30–50 tests | |
| Async iteration (`for await...of`) integration | +20–40 tests | May touch `ProtoInterpreter` |

**Files expected:** `src/modules/PromiseModule.cpp`, possibly `src/runtime/ProtoInterpreter.cpp`
for async/await edge cases.

---

## 5. Verification and Done Criteria

A category is **done** when:
- The Test262 subset for that category passes at 90%+.
- No previously-passing tests in that category now fail (zero regressions).
- `TEST262_STATUS.md` is updated with the new snapshot.

Between categories, run the full language suite (`language/`) to catch cross-cutting regressions
introduced by prototype changes.

---

## 6. Workflow Notes

- **GitNexus:** run `gitnexus_impact` before editing any symbol; run `gitnexus_detect_changes()`
  before each commit.
- **One commit per fix:** `feat(regexp): add RegExpStringIterator for matchAll`,
  `fix(date): correct Symbol.toPrimitive default hint`, etc.
- **No unrelated changes:** stay strictly within the target category per cycle.
- **Document blockers:** if a root cause requires redesign beyond the estimated scope, document it
  and move to the next item rather than stalling.
