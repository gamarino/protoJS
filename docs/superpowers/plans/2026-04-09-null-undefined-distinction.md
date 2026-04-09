# null/undefined Distinction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce a stable `t_nullSentinel` ProtoObject so that JS `null` and `undefined` are distinct values inside the protoCore interpreter, fixing ~150–200 failing `language/expressions` test262 tests.

**Architecture:** A `thread_local const proto::ProtoObject* t_nullSentinel` is initialized in `runBytecode` bootstrap and stored as attribute `__js_null_sentinel__` on the global root (making it GC-visible). `OP_null` pushes the sentinel instead of `PROTO_NONE`. All comparison/type opcodes and TypeBridge are updated to distinguish the two values.

**Tech Stack:** C++20, protoCore ProtoObject/ProtoContext API, QuickJS bytecode opcodes, CMake build, test262 runner (Node.js).

---

## File Map

| File | Action | What changes |
|------|--------|-------------|
| `src/runtime/ProtoInterpreter.cpp` | Modify | `t_nullSentinel` thread-local + bootstrap + `getNullSentinel()` + 9 opcode sites + `isTruthy` + `toPrimIfObject` + `jsAbstractEquals` |
| `src/runtime/ProtoInterpreter.h` | Modify | Declare `getNullSentinel()` |
| `src/TypeBridge.cpp` | Modify | `fromJS`: split null/undefined; `toJS`: map sentinel to `JS_NULL` |
| `docs/TEST262_STATUS.md` | Modify | Snapshot + changelog after verification |

---

## Task 1: Null Sentinel Bootstrap

**Files:**
- Modify: `src/runtime/ProtoInterpreter.h`
- Modify: `src/runtime/ProtoInterpreter.cpp` (lines ~37–44 for thread-locals; ~745–773 for bootstrap)

This task introduces the sentinel object and its GC-safe lifecycle. No opcode changes yet.

- [ ] **Step 1: Add `getNullSentinel()` declaration to the header**

  In `src/runtime/ProtoInterpreter.h`, add before the closing `}` of `namespace protojs`:

  ```cpp
  /**
   * Returns the thread-local null sentinel — the ProtoObject that represents JS null.
   * Returns nullptr if called before runBytecode has been entered on this thread.
   */
  const proto::ProtoObject* getNullSentinel();
  ```

- [ ] **Step 2: Add `t_nullSentinel` thread-local to `ProtoInterpreter.cpp`**

  In `src/runtime/ProtoInterpreter.cpp`, after the existing thread-locals at line ~44:

  ```cpp
  // The JS null sentinel: a stable ProtoObject* representing null.
  // PROTO_NONE continues to represent undefined/absence.
  thread_local const proto::ProtoObject* t_nullSentinel = nullptr;
  ```

  And add the implementation of `getNullSentinel()` right after the `runBytecode` function, near `callJSFunction`:

  ```cpp
  const proto::ProtoObject* getNullSentinel() {
      return t_nullSentinel;
  }
  ```

- [ ] **Step 3: Initialize the sentinel in `runBytecode` bootstrap**

  In `src/runtime/ProtoInterpreter.cpp`, just after the `tdzSentinel` line (~line 773) and before `int pc = 0;`:

  ```cpp
  // Bootstrap the null sentinel. Stored as __js_null_sentinel__ on the global root
  // so the GC can trace it. Cached in t_nullSentinel for O(1) access during execution.
  if (!t_nullSentinel && pGlobalRoot && *pGlobalRoot) {
      const proto::ProtoString* sentinelKey =
          (pContext->fromUTF8String("__js_null_sentinel__")
              ? pContext->fromUTF8String("__js_null_sentinel__")->asString(pContext)
              : nullptr);
      if (sentinelKey) {
          const proto::ProtoObject* existing =
              (*pGlobalRoot)->getAttribute(pContext, sentinelKey, false);
          if (existing && existing != PROTO_NONE) {
              t_nullSentinel = existing;
          } else {
              const proto::ProtoObject* sentinel = pContext->newObject(false);
              *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, sentinelKey, sentinel);
              t_nullSentinel = sentinel;
          }
      }
  }
  ```

- [ ] **Step 4: Build**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS/build && cmake --build . 2>&1 | tail -5
  ```
  Expected: build succeeds with no errors.

- [ ] **Step 5: Smoke test — sentinel is created**

  ```bash
  cat > /tmp/test_sentinel.js << 'EOF'
  // Basic sanity: script runs without crashing
  var x = null;
  var y = undefined;
  console.log("ok");
  EOF
  ./build/protojs /tmp/test_sentinel.js 2>&1 | grep -v '^\[protojs\]\|^\[RegExp\]\|^\[CommonJSLoader\]'
  ```
  Expected: `ok`

- [ ] **Step 6: Commit**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  git add src/runtime/ProtoInterpreter.h src/runtime/ProtoInterpreter.cpp
  git commit -m "feat(interpreter): introduce null sentinel — JS null distinct from undefined

  Add t_nullSentinel thread-local and getNullSentinel() accessor. The sentinel
  is initialized on the first runBytecode call and stored as __js_null_sentinel__
  on the global root to prevent GC collection. PROTO_NONE continues to represent
  undefined/absence.

  No opcode changes in this commit — sentinel is created but not yet used.

  Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
  ```

---

## Task 2: OP_null + isTruthy + toPrimIfObject

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp`
  - `OP_null` at line ~2113
  - `isTruthy` anonymous function at line ~350
  - `toPrimIfObject` lambda at line ~958

These are the three sites that produce and consume `null` as a value.

- [ ] **Step 1: Fix `OP_null` to push the sentinel**

  Locate `case OP_null:` at line ~2113. Change:

  ```cpp
  // Before:
  case OP_null:
      stackPush(pContext, PROTO_NONE);
      break;
  ```

  To:

  ```cpp
  case OP_null:
      // JS null is the null sentinel, not PROTO_NONE (which is undefined).
      stackPush(pContext, t_nullSentinel ? t_nullSentinel : PROTO_NONE);
      break;
  ```

- [ ] **Step 2: Fix `isTruthy` — null is falsy**

  Locate the `isTruthy` function at line ~350. The first line is:
  ```cpp
  if (!value || value == PROTO_NONE || value->isNone(context)) return false;
  ```

  Change to:
  ```cpp
  if (!value || value == PROTO_NONE || value->isNone(context)) return false;
  if (value == t_nullSentinel) return false;  // JS null is falsy
  ```

- [ ] **Step 3: Fix `toPrimIfObject` — null is not coercible to primitive**

  Locate the `toPrimIfObject` lambda at line ~958. Its first check is:
  ```cpp
  if (!obj || obj == PROTO_NONE || obj->isNone(pContext)) return obj;
  ```

  Change to:
  ```cpp
  if (!obj || obj == PROTO_NONE || obj->isNone(pContext)) return obj;
  if (obj == t_nullSentinel) return obj;  // null does not coerce to primitive
  ```

- [ ] **Step 4: Build**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS/build && cmake --build . 2>&1 | tail -5
  ```
  Expected: build succeeds.

- [ ] **Step 5: Verify `OP_null` and `isTruthy`**

  ```bash
  cat > /tmp/test_null_basic.js << 'EOF'
  // null is falsy
  if (null) { console.log("FAIL: null is truthy"); } else { console.log("ok: null is falsy"); }
  // undefined is falsy
  if (undefined) { console.log("FAIL: undefined is truthy"); } else { console.log("ok: undefined is falsy"); }
  // 0 is falsy
  if (0) { console.log("FAIL: 0 is truthy"); } else { console.log("ok: 0 is falsy"); }
  // "x" is truthy
  if ("x") { console.log("ok: string is truthy"); } else { console.log("FAIL"); }
  EOF
  ./build/protojs /tmp/test_null_basic.js 2>&1 | grep -v '^\[protojs\]\|^\[RegExp\]\|^\[CommonJSLoader\]'
  ```
  Expected:
  ```
  ok: null is falsy
  ok: undefined is falsy
  ok: 0 is falsy
  ok: string is truthy
  ```

- [ ] **Step 6: Commit**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  git add src/runtime/ProtoInterpreter.cpp
  git commit -m "fix(interpreter): OP_null pushes null sentinel; null is falsy and non-coercible

  OP_null now pushes t_nullSentinel instead of PROTO_NONE, making null
  distinct from undefined on the value stack.

  isTruthy: null sentinel returns false (null is falsy per spec).
  toPrimIfObject: null sentinel returns itself (null does not coerce via
  valueOf/toString — coercing null throws TypeError per spec 7.1.1).

  Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
  ```

---

## Task 3: TypeBridge — fromJS / toJS

**Files:**
- Modify: `src/TypeBridge.cpp` (lines ~12–15 for `fromJS`, lines ~307–310 for `toJS`)

TypeBridge is used at boundaries (global object setup, result extraction). Fixing it ensures that
any JS null value entering via QuickJS also becomes the sentinel.

- [ ] **Step 1: Fix `TypeBridge::fromJS`**

  In `src/TypeBridge.cpp`, at the top of `fromJS` (line ~13):

  ```cpp
  // Before (line 13):
  if (JS_IsNull(val) || JS_IsUndefined(val)) {
      return PROTO_NONE;
  }
  ```

  Change to:

  ```cpp
  if (JS_IsNull(val)) {
      // Return the null sentinel if the interpreter is active; fall back to PROTO_NONE
      // (should not happen in practice — TypeBridge is always called within runBytecode).
      const proto::ProtoObject* s = protojs::getNullSentinel();
      return s ? s : PROTO_NONE;
  }
  if (JS_IsUndefined(val)) {
      return PROTO_NONE;
  }
  ```

  Add `#include "runtime/ProtoInterpreter.h"` to `src/TypeBridge.cpp` (after the existing includes).

- [ ] **Step 2: Fix `TypeBridge::toJS`**

  In `src/TypeBridge.cpp`, in `toJS` at line ~308:

  ```cpp
  // Before:
  if (obj == PROTO_NONE || obj == nullptr) {
      return JS_NULL;
  }
  ```

  Change to:

  ```cpp
  if (obj == protojs::getNullSentinel() && protojs::getNullSentinel() != nullptr) {
      return JS_NULL;
  }
  if (obj == PROTO_NONE || obj == nullptr) {
      return JS_UNDEFINED;
  }
  ```

  Note: previously PROTO_NONE mapped to `JS_NULL` — this was wrong. It now maps to `JS_UNDEFINED`,
  which is correct. The null sentinel is the only path to `JS_NULL`.

- [ ] **Step 3: Build**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS/build && cmake --build . 2>&1 | tail -5
  ```
  Expected: build succeeds.

- [ ] **Step 4: Verify TypeBridge round-trip**

  This is implicitly verified by the subsequent opcode tests. Skip dedicated TypeBridge test for now
  — the boundary is exercised by the full expressions run.

- [ ] **Step 5: Commit**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  git add src/TypeBridge.cpp
  git commit -m "fix(TypeBridge): map JS null to null sentinel, JS undefined to PROTO_NONE

  fromJS: JS_IsNull → t_nullSentinel; JS_IsUndefined → PROTO_NONE.
  toJS: null sentinel → JS_NULL; PROTO_NONE → JS_UNDEFINED (previously
  PROTO_NONE incorrectly mapped to JS_NULL).

  Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
  ```

---

## Task 4: typeof + OP_is_null + OP_is_undefined_or_null

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp`
  - `OP_typeof` at line ~2707
  - `OP_is_null` at line ~3342
  - `OP_is_undefined_or_null` at line ~2600

- [ ] **Step 1: Fix `OP_typeof` — `typeof null === "object"`**

  Locate `case OP_typeof:` at line ~2707. Currently:
  ```cpp
  case OP_typeof: {
      if (stackEmpty(pContext)) return PROTO_NONE;
      const proto::ProtoObject* v = stackTop(pContext);
      stackPop(pContext);
      const char* typeStr = "undefined";
      if (v && v != PROTO_NONE && !v->isNone(pContext)) {
  ```

  Change the body to:
  ```cpp
  case OP_typeof: {
      if (stackEmpty(pContext)) return PROTO_NONE;
      const proto::ProtoObject* v = stackTop(pContext);
      stackPop(pContext);
      const char* typeStr = "undefined";
      if (v == t_nullSentinel) {
          typeStr = "object";  // typeof null === "object" per spec
      } else if (v && v != PROTO_NONE && !v->isNone(pContext)) {
  ```

  (Only the `if` condition changes — add the `null sentinel` early check and make the existing block an `else if`.)

- [ ] **Step 2: Fix `OP_is_null` — detect only the null sentinel**

  Locate `case OP_is_null:` at line ~3342. Currently:
  ```cpp
  case OP_is_null: {
      // Pops one value; pushes true if it is null. protoCore maps both null and
      // undefined to PROTO_NONE, so we treat PROTO_NONE as null here.
      if (stackEmpty(pContext)) return PROTO_NONE;
      const proto::ProtoObject* val = stackTop(pContext);
      stackPop(pContext);
      stackPush(pContext, (!val || val == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE);
      break;
  }
  ```

  Replace with:
  ```cpp
  case OP_is_null: {
      // Pops one value; pushes true if it is null (the null sentinel).
      if (stackEmpty(pContext)) return PROTO_NONE;
      const proto::ProtoObject* val = stackTop(pContext);
      stackPop(pContext);
      stackPush(pContext, (val == t_nullSentinel) ? PROTO_TRUE : PROTO_FALSE);
      break;
  }
  ```

- [ ] **Step 3: Fix `OP_is_undefined_or_null` — detect both sentinel and PROTO_NONE**

  Locate `case OP_is_undefined_or_null:` at line ~2600. Currently:
  ```cpp
  stackPush(pContext, (!val || val == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE);
  ```

  Change to:
  ```cpp
  stackPush(pContext, (!val || val == PROTO_NONE || val == t_nullSentinel) ? PROTO_TRUE : PROTO_FALSE);
  ```

  Also update the comment above it:
  ```cpp
  // Pops one value; pushes true if it is undefined (PROTO_NONE) or null (t_nullSentinel).
  // Used by the ?? operator and ?. optional chaining.
  ```

- [ ] **Step 4: Build**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS/build && cmake --build . 2>&1 | tail -5
  ```
  Expected: build succeeds.

- [ ] **Step 5: Verify typeof + is_null**

  ```bash
  cat > /tmp/test_typeof_null.js << 'EOF'
  console.log(typeof null);           // "object"
  console.log(typeof undefined);      // "undefined"
  console.log(typeof 0);             // "number"
  console.log(typeof "");            // "string"
  console.log(typeof true);          // "boolean"
  console.log(typeof {});            // "object"
  // is_null via == null pattern (uses OP_is_undefined_or_null internally)
  var x = null;
  console.log(x == null);            // true
  var y;
  console.log(y == null);            // true (undefined == null)
  var z = 0;
  console.log(z == null);            // false
  EOF
  ./build/protojs /tmp/test_typeof_null.js 2>&1 | grep -v '^\[protojs\]\|^\[RegExp\]\|^\[CommonJSLoader\]'
  ```
  Expected:
  ```
  object
  undefined
  number
  string
  boolean
  object
  true
  true
  false
  ```

- [ ] **Step 6: Run partial test262 — typeof category**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  TEST262_PATTERNS=language/expressions/typeof \
    TEST262_USE_PROTO_EVAL=1 TEST262_ROOT=../test262 \
    node tests/test262/runner/test262_runner.js 2>&1 | tail -5
  ```
  Expected: improvement vs previous run.

- [ ] **Step 7: Commit**

  ```bash
  git add src/runtime/ProtoInterpreter.cpp
  git commit -m "fix(interpreter): typeof null === 'object'; OP_is_null uses sentinel; OP_is_undefined_or_null covers both

  typeof null now correctly returns 'object' (null sentinel early-exit in
  OP_typeof before the generic object branch).

  OP_is_null now checks val == t_nullSentinel (not PROTO_NONE), so null and
  undefined are correctly distinguished in null-check expressions.

  OP_is_undefined_or_null now covers both PROTO_NONE and t_nullSentinel,
  fixing ?? operator and ?. optional chaining short-circuit on null.

  Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
  ```

---

## Task 5: Strict Equality — null === null / null !== undefined

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (lines ~2312–2330 for `OP_strict_eq` / `OP_strict_neq`)

After Task 2, `OP_null` pushes `t_nullSentinel` (a unique object) and `OP_undefined` pushes `PROTO_NONE`. The `a->compare(pContext, b)` call in `OP_strict_eq` compares by identity for objects, so `null === null` (sentinel == sentinel → 0) and `null !== undefined` (sentinel ≠ PROTO_NONE) should work automatically.

This task verifies correctness and adds guards for the case where `t_nullSentinel` is null (startup race).

- [ ] **Step 1: Verify without code change**

  ```bash
  cat > /tmp/test_strict_eq.js << 'EOF'
  console.log(null === null);       // true
  console.log(null === undefined);  // false
  console.log(undefined === undefined); // true
  console.log(null !== undefined);  // true
  console.log(null === 0);          // false
  console.log(null === "");         // false
  console.log(null === false);      // false
  EOF
  ./build/protojs /tmp/test_strict_eq.js 2>&1 | grep -v '^\[protojs\]\|^\[RegExp\]\|^\[CommonJSLoader\]'
  ```
  Expected:
  ```
  true
  false
  true
  true
  false
  false
  false
  ```

  If all correct: no code change needed — proceed to commit (or skip commit since no code change).
  If any wrong: see step 2.

- [ ] **Step 2: Fix `OP_strict_eq` if needed**

  Only needed if step 1 reveals failures. In that case, add explicit null-sentinel handling at
  the top of the `OP_strict_eq` block (line ~2313):

  ```cpp
  case OP_strict_eq: {
      if (stackSize(pContext) < 2) return PROTO_NONE;
      const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext);
      const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
      // Strict equality: null === null (true), null === undefined (false).
      // t_nullSentinel and PROTO_NONE are distinct pointers, so compare() handles this.
      // Guard against PROTO_NONE->compare() crash: PROTO_NONE is a valid ProtoObject.
      bool eq;
      if (a == b) {
          eq = true;  // same pointer: also handles null===null, undefined===undefined
      } else if (!a || !b) {
          eq = (a == b);  // one is true nullptr
      } else {
          eq = (a->compare(pContext, b) == 0);
      }
      stackPush(pContext, eq ? PROTO_TRUE : PROTO_FALSE);
      break;
  }
  ```

  Apply the same pattern to `OP_strict_neq`.

- [ ] **Step 3: Run strict-equals test262 category**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  TEST262_PATTERNS=language/expressions/strict-equals \
    TEST262_USE_PROTO_EVAL=1 TEST262_ROOT=../test262 \
    node tests/test262/runner/test262_runner.js 2>&1 | tail -5
  ```

- [ ] **Step 4: Commit (only if code changed in step 2)**

  ```bash
  git add src/runtime/ProtoInterpreter.cpp
  git commit -m "fix(interpreter): OP_strict_eq explicit null/undefined identity guard

  Add same-pointer fast path before compare() call to ensure null===null
  and undefined===undefined work correctly without relying on compare()
  behavior for sentinel objects.

  Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
  ```

---

## Task 6: Abstract Equality — null == undefined / null == other

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (lines ~507–571 for `jsAbstractEquals`)

Per ECMAScript §7.2.13: `null == undefined → true`; `null == <anything-else> → false`.
Currently `jsAbstractEquals` uses `xNone = !x || x == PROTO_NONE` — after Task 2, null is the
sentinel, so it falls through to the `Object vs primitive` branch, which is wrong.

- [ ] **Step 1: Fix `jsAbstractEquals` — null-specific handling**

  In `src/runtime/ProtoInterpreter.cpp`, find `jsAbstractEquals` at line ~507. The current
  null/undefined section is:

  ```cpp
  // null/undefined both map to PROTO_NONE; they are equal to each other.
  bool xNone = !x || x == PROTO_NONE || x->isNone(ctx);
  bool yNone = !y || y == PROTO_NONE || y->isNone(ctx);
  if (xNone && yNone) return true;
  if (xNone || yNone) return false;
  ```

  Replace with:

  ```cpp
  // null and undefined are distinct but equal to each other under abstract equality.
  // Per spec §7.2.13 step 2–3: null == undefined → true; null/undefined == other → false.
  bool xNull = (x == t_nullSentinel);
  bool yNull = (y == t_nullSentinel);
  bool xUndef = !x || x == PROTO_NONE || x->isNone(ctx);
  bool yUndef = !y || y == PROTO_NONE || y->isNone(ctx);
  bool xNullish = xNull || xUndef;
  bool yNullish = yNull || yUndef;
  if (xNullish && yNullish) return true;   // null == null, null == undefined, undefined == null
  if (xNullish || yNullish) return false;  // null/undefined == number/string/bool → false
  ```

- [ ] **Step 2: Also fix `OP_eq` — skip toPrimIfObject for null**

  In the `OP_eq` handler at line ~2297, currently:
  ```cpp
  const proto::ProtoObject* pa = toPrimIfObject(a);
  if (has_pending_exception) break;
  const proto::ProtoObject* pb = toPrimIfObject(b);
  ```

  `toPrimIfObject` was already fixed in Task 2 to return null sentinel unchanged, so no code
  change is needed here. Verify this with the test below.

- [ ] **Step 3: Build**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS/build && cmake --build . 2>&1 | tail -5
  ```
  Expected: build succeeds.

- [ ] **Step 4: Verify abstract equality**

  ```bash
  cat > /tmp/test_abstract_eq.js << 'EOF'
  console.log(null == null);         // true
  console.log(null == undefined);    // true
  console.log(undefined == null);    // true
  console.log(null == 0);            // false
  console.log(null == "");           // false
  console.log(null == false);        // false
  console.log(undefined == 0);      // false
  console.log(undefined == "");     // false
  console.log(null != undefined);    // false
  console.log(null != 0);           // true
  EOF
  ./build/protojs /tmp/test_abstract_eq.js 2>&1 | grep -v '^\[protojs\]\|^\[RegExp\]\|^\[CommonJSLoader\]'
  ```
  Expected:
  ```
  true
  true
  true
  false
  false
  false
  false
  false
  false
  true
  ```

- [ ] **Step 5: Commit**

  ```bash
  git add src/runtime/ProtoInterpreter.cpp
  git commit -m "fix(interpreter): abstract equality null==undefined (true), null==other (false)

  jsAbstractEquals now distinguishes null (t_nullSentinel) from undefined
  (PROTO_NONE). Per spec §7.2.13: null and undefined are abstractly equal
  to each other and to nothing else (not 0, '', or false).

  Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
  ```

---

## Task 7: Destructuring Defaults + ?? Coalescing Verification

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` only if any test in Step 1 fails

The `??` operator uses `OP_is_undefined_or_null` (fixed in Task 4). Destructuring defaults
use `OP_is_undefined` to decide whether to apply a default — already correct (`OP_is_undefined`
checks only `PROTO_NONE`, not the null sentinel, so `{a = 1} = {a: null}` correctly keeps `null`).

- [ ] **Step 1: Verify destructuring and ?? in isolation**

  ```bash
  cat > /tmp/test_dstr_coalesce.js << 'EOF'
  // Destructuring: null does NOT apply default
  var {a = 1} = {a: null};
  console.log(a);                      // null
  // Destructuring: undefined DOES apply default
  var {b = 1} = {b: undefined};
  console.log(b);                      // 1
  // Destructuring: absent property applies default
  var {c = 1} = {};
  console.log(c);                      // 1
  // ?? coalescing: null triggers fallback
  var d = null ?? "fallback";
  console.log(d);                      // "fallback"
  // ?? coalescing: undefined triggers fallback
  var e = undefined ?? "fallback";
  console.log(e);                      // "fallback"
  // ?? coalescing: 0 does NOT trigger fallback
  var f = 0 ?? "fallback";
  console.log(f);                      // 0
  // ?? coalescing: "" does NOT trigger fallback
  var g = "" ?? "fallback";
  console.log(g);                      // ""
  // ?? coalescing: false does NOT trigger fallback
  var h = false ?? "fallback";
  console.log(h);                      // false
  EOF
  ./build/protojs /tmp/test_dstr_coalesce.js 2>&1 | grep -v '^\[protojs\]\|^\[RegExp\]\|^\[CommonJSLoader\]'
  ```
  Expected:
  ```
  null
  1
  1
  fallback
  fallback
  0

  false
  ```

- [ ] **Step 2: Run test262 nullish + optional chaining categories**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  TEST262_PATTERNS="language/expressions/nullish language/expressions/optional-chaining" \
    TEST262_USE_PROTO_EVAL=1 TEST262_ROOT=../test262 \
    node tests/test262/runner/test262_runner.js 2>&1 | tail -5
  ```

- [ ] **Step 3: Fix any remaining failures**

  If destructuring defaults still apply defaults on `null`, locate the destructuring default check.
  Search for `OP_is_undefined` usage in parameter/binding initialization context and verify it
  doesn't check for the null sentinel:

  ```bash
  grep -n "OP_is_undefined\|is_undefined\|PROTO_NONE.*default" \
    src/runtime/ProtoInterpreter.cpp | head -20
  ```

  `OP_is_undefined` at line ~3334 checks `!val || val == PROTO_NONE` — this is correct.
  The null sentinel will NOT match, so no default is applied for null. No change needed.

  If `??` doesn't trigger on null: verify `OP_is_undefined_or_null` includes `t_nullSentinel`
  (done in Task 4 Step 3). If still broken, trace which opcode QuickJS uses for `??` by
  running with a debugger or adding a temporary log:
  ```bash
  grep -n "OP_is_undefined_or_null\|OP_nip\|coalesce" \
    src/runtime/ProtoInterpreter.cpp | head -10
  ```

  No commit in this task unless fixes were needed.

---

## Task 8: Native Methods Audit

**Files:**
- Modify: various `src/*.cpp` files (only where `== PROTO_NONE` needs updating)

Find all sites where `== PROTO_NONE` is used to mean "is null or undefined" and update to
`isNullOrUndefined`. Add a helper macro/inline to avoid scatter.

- [ ] **Step 1: Add `isNullish` helper near the top of `ProtoInterpreter.cpp`**

  After the `t_nullSentinel` declaration (~line 46), add:

  ```cpp
  // Inline helper: true if v is null (t_nullSentinel) or undefined (PROTO_NONE/nullptr).
  static inline bool isNullish(const proto::ProtoObject* v) {
      return !v || v == PROTO_NONE || v == t_nullSentinel;
  }
  ```

- [ ] **Step 2: Grep for sites that need updating**

  ```bash
  grep -n "== PROTO_NONE\|PROTO_NONE ==" src/*.cpp src/runtime/*.cpp | \
    grep -v "test\|//\|setBool\|setSlot\|pending_exception" | head -60
  ```

  For each match, classify:
  - **"only undefined"** — keep as `== PROTO_NONE` (e.g. checking if a JS variable is unset)
  - **"null or undefined"** — change to `isNullish(v)` or `!v || v == PROTO_NONE || v == t_nullSentinel`
  - **"internal sentinel"** — keep as `== PROTO_NONE` (interpreter internal use)

  Key sites to verify:
  - `Array.prototype` methods (`src/ArrayPrototype.cpp`): separator in `join`, callback check in `forEach`
  - `Object.prototype` methods (`src/ObjectPrototype.cpp`)
  - `String.prototype` methods (`src/StringPrototype.cpp`)

- [ ] **Step 3: Fix `Array.prototype.join` separator check (if needed)**

  In `src/ArrayPrototype.cpp`, `join` uses a separator that defaults to `,` when undefined:
  ```cpp
  // Check: if sep is PROTO_NONE (undefined), use ","
  // null should produce empty string "" per spec (not ",")
  ```
  Search for the separator handling:
  ```bash
  grep -n "join\|separator\|sep.*PROTO_NONE" src/ArrayPrototype.cpp | head -10
  ```

  If the separator check uses `== PROTO_NONE`, change it:
  - `sep == PROTO_NONE` → use default `","` (undefined)
  - `sep == t_nullSentinel` → use `""` (null → empty separator per spec)

- [ ] **Step 4: Build after any changes**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS/build && cmake --build . 2>&1 | tail -5
  ```

- [ ] **Step 5: Quick regression check**

  ```bash
  cat > /tmp/test_regression.js << 'EOF'
  // Array methods should still work
  console.log([1,2,3].join(","));      // "1,2,3"
  console.log([1,2,3].join(null));     // "1null2null3" — null converts to "null" string per spec
  console.log([1,2,3].join());         // "1,2,3"
  // Object methods
  var o = {a: null, b: undefined, c: 1};
  console.log(JSON.stringify(o));      // {"a":null,"c":1}
  // null propagation
  var arr = [null, undefined, 1];
  console.log(arr.indexOf(null));      // 0
  console.log(arr.indexOf(undefined)); // 1
  EOF
  ./build/protojs /tmp/test_regression.js 2>&1 | grep -v '^\[protojs\]\|^\[RegExp\]\|^\[CommonJSLoader\]'
  ```

- [ ] **Step 6: Commit if any native methods were changed**

  ```bash
  git add src/*.cpp
  git commit -m "fix(builtins): audit PROTO_NONE usages — distinguish null vs undefined in native methods

  After introducing t_nullSentinel, native methods that used == PROTO_NONE
  to mean 'null or undefined' now use isNullish(). Sites that correctly
  meant 'only undefined' are unchanged.

  Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
  ```

---

## Task 9: Smoke Test + Partial test262 Run

**Files:** None modified — verification only.

- [ ] **Step 1: Full smoke test — all 8 cases from spec**

  ```bash
  cat > /tmp/test_null_full.js << 'EOF'
  // typeof null
  console.log(typeof null === "object" ? "PASS" : "FAIL: typeof null");
  // null === null
  console.log(null === null ? "PASS" : "FAIL: null===null");
  // null === undefined
  console.log(null === undefined ? "FAIL: null===undefined" : "PASS");
  // null == undefined
  console.log(null == undefined ? "PASS" : "FAIL: null==undefined");
  // ?? coalescing
  console.log((null ?? "fallback") === "fallback" ? "PASS" : "FAIL: ??");
  // destructuring: null does not trigger default
  var {a = 1} = {a: null};
  console.log(a === null ? "PASS" : "FAIL: dstr null default");
  // destructuring: undefined triggers default
  var {b = 1} = {b: undefined};
  console.log(b === 1 ? "PASS" : "FAIL: dstr undef default");
  // optional chaining on null
  var obj = null;
  console.log(obj?.x === undefined ? "PASS" : "FAIL: ?. on null");
  EOF
  ./build/protojs /tmp/test_null_full.js 2>&1 | grep -v '^\[protojs\]\|^\[RegExp\]\|^\[CommonJSLoader\]'
  ```
  Expected: 8 lines of `PASS`.

- [ ] **Step 2: Partial test262 — key categories**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  TEST262_PATTERNS="language/expressions/strict-equals language/expressions/typeof \
    language/expressions/nullish language/expressions/optional-chaining \
    language/expressions/abstract-equality" \
    TEST262_USE_PROTO_EVAL=1 TEST262_ROOT=../test262 \
    node tests/test262/runner/test262_runner.js 2>&1 | tail -15
  ```
  Expected: significant improvement in all five categories vs previous runs.

---

## Task 10: Full language/expressions Run + TEST262_STATUS.md Update

**Files:**
- Modify: `docs/TEST262_STATUS.md`
- New snapshot in: `tests/test262/reports/`

- [ ] **Step 1: Run full `language/expressions` suite**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  TEST262_PATTERNS=language/expressions TEST262_USE_PROTO_EVAL=1 \
    TEST262_ROOT=../test262 node tests/test262/runner/test262_runner.js
  ```
  This takes ~5–10 minutes.

- [ ] **Step 2: Extract results from snapshot**

  ```bash
  node -e "
  const fs = require('fs');
  const files = fs.readdirSync('tests/test262/reports/')
    .filter(f => f.includes('language-expressions') || f.includes('language_expressions'))
    .sort();
  const latest = files[files.length - 1];
  const snap = JSON.parse(fs.readFileSync('tests/test262/reports/' + latest, 'utf8'));
  const pass = snap.results.filter(r => r.result === 'pass').length;
  const total = snap.results.length;
  const sem  = snap.results.filter(r => r.result === 'failed_semantics').length;
  const syn  = snap.results.filter(r => r.result === 'failed_syntax').length;
  const to   = snap.results.filter(r => r.result === 'timeout').length;
  console.log('Snapshot:', latest);
  console.log('Pass:', pass, '/', total, '=', (100*pass/total).toFixed(1) + '%');
  console.log('Semantics failures:', sem, '| Syntax failures:', syn, '| Timeouts:', to);
  "
  ```

- [ ] **Step 3: Update `docs/TEST262_STATUS.md`**

  In `docs/TEST262_STATUS.md`, update the changelog section to add a new row:

  ```markdown
  | 2026-04-09 | `language/expressions` post-null/undefined fix: **XX.X%** (XXXX/11,036) | +XXX passes vs 80.9%. Fixes: typeof null, null===null, null!==undefined, null==undefined, ?? coalescing, ?. on null, destructuring defaults. |
  ```

  Also update the "Recommended Next Steps" section — if the target ~83% was reached, mark item 3
  (`null vs undefined distinction`) as RESOLVED and add the next candidate.

- [ ] **Step 4: Commit snapshot + status update**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  # Replace XXXXXXXXXX with the actual snapshot timestamp
  SNAP=$(ls tests/test262/reports/snapshot-language-expressions-*.json | sort | tail -1)
  git add docs/TEST262_STATUS.md "$SNAP"
  git commit -m "docs(test262): language/expressions post-null/undefined distinction — XX.X%

  New pass rate: XXXX/11036 (XX.X%)
  Previous: ~8,928/11036 (80.9%)
  Gain: +XXX passes

  Root cause fixed: null and undefined are now distinct values. Changes:
  - OP_null pushes t_nullSentinel (not PROTO_NONE)
  - TypeBridge.fromJS maps JS_IsNull → sentinel, JS_IsUndefined → PROTO_NONE
  - typeof null → 'object'
  - OP_is_null checks sentinel only
  - OP_is_undefined_or_null checks both
  - jsAbstractEquals: null == undefined (true), null == other (false)
  - isTruthy: null is falsy
  - toPrimIfObject: null is not coercible

  Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
  ```

---

## Self-Review Checklist

**Spec coverage:**
- [x] Null sentinel creation + GC safety → Task 1
- [x] TypeBridge fromJS/toJS → Task 3
- [x] typeof null → Task 4
- [x] OP_strict_eq null===null / null!==undefined → Task 5
- [x] Abstract equality null==undefined → Task 6
- [x] OP_is_null → Task 4
- [x] OP_is_undefined → correct by construction (no change needed)
- [x] OP_coalesce (??) → Task 4 (OP_is_undefined_or_null)
- [x] Destructuring defaults → Task 7
- [x] Optional chaining ?. → Task 7 (OP_is_undefined_or_null)
- [x] isTruthy → Task 2
- [x] toPrimIfObject → Task 2
- [x] Native methods audit → Task 8
- [x] TEST262_STATUS.md update → Task 10

**Type consistency:** `t_nullSentinel` (thread_local ProtoObject*) is used consistently throughout.
`isNullish()` helper introduced in Task 8 for native method audit.
