# Design Specification: Test262 90% Conformance Roadmap

**Date:** 2026-04-07
**Topic:** Raising protoJS Test262 pass rate from 75.5% to ≥90%
**Status:** Approved

---

## Objective

Gain ~29,841 additional passing tests to move from 154,918 (75.5%) to ≥184,759 (90%) out of
205,288 total Test262 tests.

The work is structured as three sequential implementation phases plus a conditional fourth phase
to close any remaining gap.

---

## Baseline (2026-04-07)

| Area | Total | Passed | Pass % |
|------|------:|-------:|-------:|
| `language` | 141,435 | 126,665 | 89.6% |
| `built-ins` | 63,853 | 28,253 | 44.2% |
| `built-ins/TypedArrayConstructors` | 2,199 | 86 | 3.9% |
| `built-ins/TypedArray` | 4,742 | 45 | 0.9% |
| `built-ins/ArrayBuffer` | 509 | 353 | 69.4% |
| `built-ins/DataView` | 1,558 | 1,032 | 66.2% |
| `language/expressions` | 78,110 | 67,875 | 86.9% |
| `language/statements` | 14,622 | 13,014 | 89.0% |
| `built-ins/Object` | 7,000 | 5,958 | 85.1% |
| `built-ins/Promise` | 4,032 | 3,319 | 82.3% |
| `built-ins/String` | 3,420 | 2,776 | 81.2% |
| **Overall** | **205,288** | **154,918** | **75.5%** |

---

## Phase 8 — TypedArray + ArrayBuffer + DataView

**Target tests gained: ~6,000–7,500**

### Scope

Full implementation of the ECMAScript binary data subsystem.

| Tier | Components | Notes |
|------|-----------|-------|
| 1 — Foundation | `ArrayBuffer` completed | Fix remaining ~156 failures: `transfer()`, `resize()`, `ArrayBuffer.isView()`, constructor edge cases |
| 2 — TypedArray base | Abstract `%TypedArray%` prototype | All shared methods: `set`, `subarray`, `fill`, `copyWithin`, `every`, `find`, `findIndex`, `forEach`, `includes`, `indexOf`, `join`, `keys`, `values`, `entries`, `map`, `reduce`, `reduceRight`, `reverse`, `slice`, `some`, `sort`, `toReversed`, `toSorted`, `with`, `at`, `Symbol.iterator` |
| 3 — Concrete views | 11 typed array constructors | `Int8Array`, `Uint8Array`, `Uint8ClampedArray`, `Int16Array`, `Uint16Array`, `Int32Array`, `Uint32Array`, `Float32Array`, `Float64Array`, `BigInt64Array`, `BigUint64Array` |
| 4 — DataView completed | `DataView` prototype methods | Currently at 66.2%; many tests unblock once ArrayBuffer is solid |
| 5 — TypedArray iterators | `TypedArrayIterator` | `%TypedArray%.prototype[Symbol.iterator]`, `keys()`, `values()`, `entries()` |

### Backing Store Design

`ArrayBuffer` instances use `ProtoExternalBuffer` as the raw memory backing:

- **Allocation:** `ctx->newExternalBuffer(byteLength)` — uses `aligned_alloc`; GC shadow-frees the
  raw segment when the descriptor is collected.
- **Storage:** the resulting `ProtoObject*` is stored as attribute `__ab_data__` on the JS
  ArrayBuffer instance.
- **Access:** `abDataObj->getRawPointerIfExternalBuffer(ctx)` returns a stable `void*` pointer
  valid until the GC collects the object.
- **Lifetime:** any typed array view holds a reference to its ArrayBuffer (via `__ab_buffer__`
  attribute), keeping the `ProtoExternalBuffer` alive for the view's lifetime.
- **SharedArrayBuffer:** deferred — uses the same pattern but requires an external reference count.
  Needed for `Atomics` (Phase 11).

### Constructor Dispatch

Same marker pattern as `Array` (`__array_ctor__`) and `String` (`__string_ctor__`):

- Each typed array constructor gets `__typed_array_ctor__ = <element-type-tag>` (e.g. `"int8"`,
  `"uint8"`, `"float64"`).
- `OP_call` / `OP_call_constructor` detect the tag and dispatch to `TypedArrayPrototype::construct`.
- `TypedArray.from()` and `TypedArray.of()` are static methods on each constructor.

### Index Proxy

Numeric property access on typed array instances (`arr[0]`, `arr[0] = x`) is handled by
detecting `__ta_element_type__` in `getAttribute` / `setAttribute`:

- Read: convert raw bytes at `index * elementSize` to the appropriate JS number/BigInt.
- Write: clamp/coerce the JS value, write bytes at `index * elementSize`.
- Out-of-bounds reads return `undefined`; writes are silently ignored (per spec).

### New Files

| File | Purpose |
|------|---------|
| `src/ArrayBufferPrototype.h/.cpp` | `ArrayBuffer` constructor + prototype methods |
| `src/TypedArrayPrototype.h/.cpp` | Abstract `%TypedArray%` + all 11 concrete constructors |
| `src/DataViewPrototype.h/.cpp` | `DataView` constructor + all get/set methods |

### Test Targets

| Area | Total | Current | Target |
|------|------:|--------:|-------:|
| `built-ins/TypedArrayConstructors` | 2,199 | 86 | ~2,000 |
| `built-ins/TypedArray` | 4,742 | 45 | ~4,200 |
| `built-ins/ArrayBuffer` | 509 | 353 | ~480 |
| `built-ins/DataView` | 1,558 | 1,032 | ~1,400 |
| **Phase 8 total** | **9,008** | **1,516** | **~8,080** |

**Estimated gain: ~6,564 tests.**

### Process

Diagnosis-first per tier:

1. Run `built-ins/ArrayBuffer` subset → fix remaining failures → verify no regressions.
2. Implement `TypedArrayPrototype` base → run `built-ins/TypedArray` → iterate.
3. Add all 11 concrete constructors → run `built-ins/TypedArrayConstructors` → iterate.
4. Complete `DataView` → run `built-ins/DataView` → iterate.
5. Update `docs/TEST262_STATUS.md` after each tier.

---

## Phase 9 — language/expressions + language/statements

**Target tests gained: ~6,000–8,000**

### Scope

Root-cause analysis and targeted fixes for the ~11,800 failing tests in `language/expressions`
(86.9%) and `language/statements` (89%).

### Probable Root Causes — `language/expressions`

| Feature | Estimated tests | Notes |
|---------|---------------:|-------|
| Destructuring edge cases (default values, rest, nested) | ~1,500–2,000 | `{a=1}`, `[...rest]` in params and assignments |
| `for...of` / iterators in expressions, `yield*`, spread | ~800–1,200 | |
| Optional chaining `?.` / nullish coalescing `??` edge cases | ~500–800 | Assignment forms, short-circuit |
| Logical assignment (`&&=`, `\|\|=`, `??=`) | ~300–500 | ES2021 |
| `async`/`await` edge cases | ~500–800 | Error propagation, unhandled rejections |
| `class` expressions — private fields/methods, static blocks | ~400–600 | ES2022 |
| `in` operator with private fields (`#x in obj`) | ~200–300 | ES2022 |
| Temporal Dead Zone (TDZ) correctness | ~200–400 | `let`/`const` before initialization |

### Probable Root Causes — `language/statements`

| Feature | Estimated tests |
|---------|---------------:|
| `for...of` / `for...in` with destructuring | ~400–600 |
| `class` statements — static blocks, private fields | ~300–500 |
| `try/catch` with destructuring in catch binding | ~100–200 |
| `with` statement (legacy) | ~50–100 |

### Process

1. Run `language/expressions` subset → group failures by error message → rank by frequency.
2. Fix root causes in descending impact order (destructuring first, then class private, then async).
3. Re-run after each fix; confirm no regressions.
4. Repeat for `language/statements`.
5. Update `docs/TEST262_STATUS.md`.

---

## Phase 10 — Built-ins Sweep

**Target tests gained: ~2,500–3,500**

### Scope

Diagnosis-first pass on all built-ins in the 65–85% range.

| Area | Total | Failures | Target recovery |
|------|------:|---------:|----------------:|
| `built-ins/Object` | 7,000 | ~1,042 | ~900 |
| `built-ins/Promise` | 4,032 | ~712 | ~500 |
| `built-ins/String` | 3,420 | ~645 | ~500 |
| `built-ins/Function` | 1,139 | ~366 | ~280 |
| `built-ins/Iterator` | 1,384 | ~296 | ~200 |
| `built-ins/Set` | 1,099 | ~202 | ~160 |
| `built-ins/Map` | 530 | ~165 | ~140 |
| `built-ins/Symbol` | 216 | ~132 | ~100 |
| `built-ins/NativeErrors` | 214 | ~128 | ~100 |
| `built-ins/WeakMap` + `WeakSet` | ~450 | ~100 | ~80 |
| **Phase 10 total** | | | **~2,960** |

### Process

Order: Object → Promise → String → Function → remaining by failure count.
For each: run subset → group by root cause → fix in descending impact order → verify.

---

## Phase 11 — Gap Closer (Conditional)

Activated if Phases 8–10 leave the pass rate below 90%. Candidates:

| Area | Failures | Notes |
|------|----------:|-------|
| `built-ins/Atomics` | ~749 | Requires `SharedArrayBuffer` — unblocked by Phase 8 design |
| `language/function-code` | ~260 | Currently 68.9%; root causes TBD |
| Second pass `language/expressions` | Variable | Remaining high-frequency root causes from Phase 9 |
| `built-ins/BigInt` | ~56 | 63.6% → should be straightforward |
| `built-ins/JSON` | ~68 | 79% → edge cases |

---

## Overall Projection

| Phase | Tests gained (conservative) | Tests gained (optimistic) |
|-------|--------------------------:|-------------------------:|
| Phase 8 | 6,000 | 7,500 |
| Phase 9 | 6,000 | 8,000 |
| Phase 10 | 2,500 | 3,500 |
| **Phases 8–10 total** | **14,500** | **19,000** |

- Conservative: 154,918 + 14,500 = **169,418 → ~82.5%**
- Optimistic: 154,918 + 19,000 = **173,918 → ~84.7%**

Phase 11 adds ~1,000–3,000 more tests. Combined with upside variance in Phase 9 (a single
high-frequency destructuring root cause could recover 3,000+ tests alone), the 90% target
is achievable within these four phases.

---

## Commit Discipline

- One commit per feature or fix, prefixed `feat(typed-array):`, `fix(expressions):`, etc.
- `docs/TEST262_STATUS.md` updated after each phase completes.
- Each phase gets its own spec and implementation plan document.

---

## Deferred

| Feature | Reason |
|---------|--------|
| `Temporal` (16,745 tests) | Massive spec surface; separate multi-month effort |
| `ShadowRealm` | Requires deep sandboxing |
| `DisposableStack` / `AsyncDisposableStack` | ES2025 using/await using syntax |
| `SharedArrayBuffer` (full) | Phase 11 partial via Atomics only |
