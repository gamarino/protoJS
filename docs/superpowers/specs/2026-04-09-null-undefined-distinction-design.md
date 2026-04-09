# Design Specification: null/undefined Distinction in protoCore

**Date:** 2026-04-09
**Topic:** Introduce a stable `null` sentinel so JS `null` and `undefined` are distinct values inside the protoCore interpreter
**Status:** Approved

---

## Objective

Fix the root cause behind ~150–200 failing `language/expressions` test262 tests where `null` and
`undefined` are treated identically. Raise `language/expressions` pass rate from **80.9% → ~83–84%**.

---

## Background

`TypeBridge::fromJSValue` currently maps both `JS_IsNull` and `JS_IsUndefined` to `PROTO_NONE`.
Once inside the interpreter there is no way to distinguish the two values, breaking:

- `typeof null` (must return `"object"`, currently returns `"undefined"`)
- `null === null` (must be `true` when compared with strict equality — currently passes vacuously, but `null === undefined` also returns `true` which is wrong)
- `null == undefined` (must be `true` in abstract equality — currently `false`)
- `??` coalescing (must trigger on `null` — currently only triggers on `PROTO_NONE`)
- Destructuring defaults (must NOT trigger for `null`, only for `undefined`)
- Optional chaining `?.` short-circuit (must treat `null` as nullish)

---

## Architecture

### Null Sentinel

Introduce a single stable `ProtoObject*` — the **null sentinel** — that represents JS `null`
inside protoCore. `PROTO_NONE` continues to represent `undefined`/absence.

```
t_nullSentinel  →  immutable ProtoObject, created once, marked with attribute "__js_null__ = true"
PROTO_NONE      →  undefined / absent (unchanged)
```

**Lifetime:** The sentinel is created during interpreter bootstrap in `ProtoInterpreter::initialize()`
and stored as `pNullSentinel` (a field on `ProtoInterpreter`). It is registered as a GC root so
it is never collected.

**Exposure:** `TypeBridge` receives `pNullSentinel` at construction time. Native methods that need
to distinguish null/undefined access it via the interpreter reference they already hold.

### Helper predicates

Three inline helpers added to `ProtoInterpreter` (or a shared header):

```cpp
inline bool isNull(const proto::ProtoObject* v) const {
    return v != nullptr && v == pNullSentinel;
}
inline bool isUndefined(const proto::ProtoObject* v) const {
    return v == nullptr || v == PROTO_NONE;
}
inline bool isNullOrUndefined(const proto::ProtoObject* v) const {
    return isNull(v) || isUndefined(v);
}
```

---

## Changes by File

### `src/runtime/ProtoInterpreter.cpp` — bootstrap

In the initializer (near global constant setup):

```cpp
// Create the JS null sentinel — a stable root object that represents null.
pNullSentinel = pContext->newObject(false);
pNullSentinel->setAttribute(pContext, "__js_null__", pContext->fromBool(true));
// Register as GC root to prevent collection.
pContext->space->addRoot(pNullSentinel);
```

### `src/TypeBridge.cpp` — fromJSValue / toJSValue

**fromJSValue (JS → protoCore):**

```cpp
// Before (broken):
if (JS_IsNull(ctx, val) || JS_IsUndefined(ctx, val)) return PROTO_NONE;

// After:
if (JS_IsNull(ctx, val))      return pInterpreter->pNullSentinel;
if (JS_IsUndefined(ctx, val)) return PROTO_NONE;
```

**toJSValue (protoCore → JS):**

```cpp
// Add before the PROTO_NONE check:
if (val == pInterpreter->pNullSentinel) return JS_NULL;
if (!val || val == PROTO_NONE)          return JS_UNDEFINED;
```

### `src/runtime/ProtoInterpreter.cpp` — opcodes

Six change points, in descending impact order:

| Opcode / area | Change |
|---|---|
| `typeof` handler | Add `if (isNull(val)) { push("object"); break; }` before the `PROTO_NONE` branch |
| `OP_strict_eq` / `OP_strict_neq` | Use `isNull` / `isUndefined` for sentinel-aware comparison; `null !== undefined` |
| `OP_eq` / `OP_neq` (abstract equality) | `null == undefined → true`; `null == 0/false/"" → false` (per spec Table 11) |
| `OP_is_null` | Change guard from `val == PROTO_NONE` to `isNull(val)` |
| `OP_is_undefined` | Change guard from `val == PROTO_NONE` to `isUndefined(val)` |
| `OP_coalesce` (`??`) | Change trigger from `val == PROTO_NONE` to `isNullOrUndefined(val)` |
| Destructuring default check | Change `val == PROTO_NONE` to `isUndefined(val)` — null must NOT trigger default |
| Optional chaining `?.` | Change short-circuit from `val == PROTO_NONE` to `isNullOrUndefined(val)` |

### Native methods audit

Any C++ native method that uses `== PROTO_NONE` to mean "this arg was not provided / is empty"
must be reviewed. The audit uses:

```bash
grep -n "== PROTO_NONE\|PROTO_NONE ==" src/*.cpp src/runtime/*.cpp
```

For each match, classify as:
- **"undefined only"** → keep as `isUndefined(v)` — e.g. destructuring defaults, `arguments` slots
- **"null or undefined"** → change to `isNullOrUndefined(v)` — e.g. `Array.prototype.join` separator arg
- **"internal absence"** → keep as `== PROTO_NONE` — e.g. internal slot checks that never receive user values

---

## GC Safety

The null sentinel is stored in `ProtoInterpreter::pNullSentinel`. It must be reachable from the
GC mark phase. Implementation: call `pContext->space->addRoot(pNullSentinel)` immediately after
creation. Verify by running a GC-stress test after the change (or checking that the sentinel
survives a forced GC cycle via the memory analyzer).

---

## Verification Plan

### Step 1 — Manual smoke test (6 cases)

```js
console.log(typeof null);                    // "object"
console.log(null === null);                  // true
console.log(null === undefined);             // false
console.log(null == undefined);              // true
console.log(null ?? "fallback");             // "fallback"
const {a = 1} = {a: null};
console.log(a);                              // null  (default NOT applied)
const {b = 1} = {b: undefined};
console.log(b);                              // 1     (default applied)
```

### Step 2 — Partial test262 run

```bash
TEST262_PATTERNS="language/expressions/strict-equals language/expressions/typeof \
  language/expressions/nullish language/expressions/optional-chaining" \
  TEST262_USE_PROTO_EVAL=1 TEST262_ROOT=../test262 \
  node tests/test262/runner/test262_runner.js 2>&1 | tail -10
```

### Step 3 — Full language/expressions run + snapshot

```bash
TEST262_PATTERNS=language/expressions TEST262_USE_PROTO_EVAL=1 \
  TEST262_ROOT=../test262 node tests/test262/runner/test262_runner.js
```

Compare against 80.9% baseline (snapshot `snapshot-language-expressions-*.json`).
Target: ≥83% (≥9,160/11,036).

---

## Risks

| Risk | Mitigation |
|------|-----------|
| Native methods receive null sentinel and misinterpret it as a valid object | Audit via grep; classify each site before committing |
| GC collects the sentinel | Register as space root immediately after creation |
| Regressions in tests that passed vacuously | Full expressions run post-fix required before updating TEST262_STATUS.md |
| `OP_strict_eq` performance regression | Sentinel check is a single pointer comparison — negligible |

---

## Commit Plan

One commit per logical unit:

1. `feat(interpreter): introduce null sentinel — JS null distinct from undefined`
   — bootstrap + TypeBridge + `typeof` + `OP_strict_eq`
2. `fix(interpreter): abstract equality null==undefined, OP_is_null, OP_coalesce`
   — remaining opcodes
3. `fix(interpreter): destructuring defaults use isUndefined, not isNullOrUndefined`
   — destructuring + optional chaining
4. `docs(test262): update language/expressions status — null/undefined distinction`
   — snapshot + TEST262_STATUS.md

---

## Expected Outcome

| Metric | Before | After |
|--------|--------|-------|
| `language/expressions` pass rate | 80.9% (~8,928/11,036) | ~83–84% (~9,160–9,270/11,036) |
| `typeof null` | `"undefined"` ❌ | `"object"` ✅ |
| `null === undefined` | `true` ❌ | `false` ✅ |
| `null == undefined` | `false` ❌ | `true` ✅ |
| `null ?? "x"` | `null` ❌ | `"x"` ✅ |
| Destructuring `{a=1}={a:null}` | `1` ❌ | `null` ✅ |
