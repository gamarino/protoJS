# Phase 15: Iterator Destructuring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `OP_iterator_next` and `OP_iterator_call` in the protoJS interpreter to enable array destructuring patterns, fixing ~700 test262 failures across for-of/dstr, assignment/dstr, function/dstr, and related subcategories.

**Architecture:** `OP_for_of_start` already pushes a slot-backed iterator state `[iterObj, PROTO_NONE, 0LL]` onto the stack. `OP_iterator_next` (currently a no-op returning PROTO_NONE) must read the iterator state via the slot, call `.next()` (or index the array), and push a synthetic `{value, done}` result object. `OP_iterator_call` with `flags=1` does the same but drains all remaining elements into a rest array.

**Tech Stack:** C++20, protoCore, QuickJS bytecode format, test262 JS test suite.

---

## Background

### Current failure count (Phase 14 baseline)
| Category | Failures | Root cause |
|----------|----------|------------|
| `language/statements/for-of/dstr` | 221 | `OP_iterator_next` no-op |
| `language/expressions/assignment/dstr` | 123 | `OP_iterator_next` no-op |
| `language/statements/for/dstr` | 106 | `OP_iterator_next` no-op |
| `language/expressions/arrow-function/dstr` | 80 | `OP_iterator_next` no-op |
| `language/expressions/function/dstr` | 80 | `OP_iterator_next` no-op |
| `language/statements/function/dstr` | 80 | `OP_iterator_next` no-op |
| `language/expressions/generators/dstr` | 80 | `OP_iterator_next` no-op |

**Total: ~770 tests fixable in one opcode implementation.**

### Stack protocol (established by `OP_for_of_start`)

`OP_for_of_start` pushes 3 items: `[iterObj, PROTO_NONE, 0LL]`

- `iterObj` — a lightweight ProtoObject whose `__iter_slot__` attribute holds a slot index `bs`
- `slot[bs]` — the actual iterable/iterator object
- `slot[bs+1]` — current index (≥0 for arrays) OR `-1` (sentinel for native `next()`-based iterators)

`OP_iterator_next` DEF: `(1, 4, 4, none)` — pops 4, pushes 4.

Before: `[iter, nextMethod, catch_offset, sentinel_undef]`  
After:  `[iter, nextMethod, catch_offset, result_obj]`

where `result_obj` is `{value, done}` (a ProtoObject with `value` and `done` attributes).

`OP_iterator_call` DEF: `(2, 4, 5, u8)` — pops 4, pushes 5.

Before: `[iter, nextMethod, catch_offset, sentinel_undef]`  
After (flag=1): `[iter, nextMethod, catch_offset, rest_array, false]`  
After (flag=0): `[iter, nextMethod, catch_offset, undefined, true]`

`OP_iterator_get_value_done` (already implemented) then extracts value/done:

Before: `[iter, nextMethod, catch_offset, result_obj]`  
After:  `[iter, nextMethod, new_catch_offset, value, done]`

`OP_iterator_close` (already implemented) pops `[iter, nextMethod, catch_offset]`.

---

## File Map

| File | Change |
|------|--------|
| `src/runtime/ProtoInterpreter.cpp` | Replace `OP_iterator_next` no-op (line ~4280) and `OP_iterator_call` no-op (line ~4285) with full implementations |

No other files need changes. The slot infrastructure, `OP_for_of_start`, `OP_for_of_next`, `OP_iterator_get_value_done`, `OP_iterator_close`, and `JSSymbols` are already correct.

---

## Task 1: Write and run failing manual tests

**Files:**
- Create: `tests/manual/test_destructuring.js`

- [ ] **Step 1.1: Write the test file**

```bash
cat > /home/gamarino/Documentos/proyectos/protoJS/tests/manual/test_destructuring.js << 'EOF'
// Array destructuring — basic
const [a, b, c] = [10, 20, 30];
if (a !== 10) throw new Error("a should be 10, got: " + a);
if (b !== 20) throw new Error("b should be 20, got: " + b);
if (c !== 30) throw new Error("c should be 30, got: " + c);
console.log("PASS: basic array destructuring");

// Array destructuring with rest
const [first, ...rest] = [1, 2, 3, 4];
if (first !== 1) throw new Error("first should be 1, got: " + first);
if (rest.length !== 3) throw new Error("rest.length should be 3, got: " + rest.length);
if (rest[0] !== 2) throw new Error("rest[0] should be 2, got: " + rest[0]);
console.log("PASS: rest element destructuring");

// Array destructuring with skip (hole)
const [x, , z] = [7, 8, 9];
if (x !== 7) throw new Error("x should be 7, got: " + x);
if (z !== 9) throw new Error("z should be 9, got: " + z);
console.log("PASS: skip element destructuring");

// for-of with destructuring
const pairs = [[1, 'a'], [2, 'b'], [3, 'c']];
const results = [];
for (const [num, letter] of pairs) {
    results.push(num + letter);
}
const expected = ['1a', '2b', '3c'].join(',');
const actual = results.join(',');
if (actual !== expected) throw new Error("for-of dstr: expected " + expected + ", got: " + actual);
console.log("PASS: for-of with destructuring");

// Assignment destructuring
let p, q;
[p, q] = [100, 200];
if (p !== 100) throw new Error("p should be 100, got: " + p);
if (q !== 200) throw new Error("q should be 200, got: " + q);
console.log("PASS: assignment destructuring");

// Function parameter destructuring
function sum([x, y, z]) { return x + y + z; }
const s = sum([1, 2, 3]);
if (s !== 6) throw new Error("sum should be 6, got: " + s);
console.log("PASS: function parameter destructuring");

console.log("ALL TESTS PASSED");
EOF
```

- [ ] **Step 1.2: Verify the tests currently fail**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
./build/protojs tests/manual/test_destructuring.js 2>&1 | head -20
```

Expected: fails or crashes silently (returns empty output, exit code 0 — due to current `OP_iterator_next` → `return PROTO_NONE` behaviour). The `PASS` lines should NOT appear.

---

## Task 2: Implement `OP_iterator_next`

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` lines ~4277–4281

- [ ] **Step 2.1: Replace the `OP_iterator_next` no-op**

Find this block (around line 4277):

```cpp
            // OP_iterator_next: DEF(iterator_next, 1, 4, 4, none)
            // Used in destructuring. Unsupported — preserve vacuous-pass behaviour for
            // tests that use iterator destructuring (they previously exited here via default:).
            case OP_iterator_next:
                return PROTO_NONE;
```

Replace it with:

```cpp
            // OP_iterator_next: DEF(iterator_next, 1, 4, 4, none)
            // Stack before: [iter, nextMethod, catch_offset, sentinel]  (4 items consumed)
            // Stack after:  [iter, nextMethod, catch_offset, result_obj] (4 items produced)
            // result_obj is a ProtoObject with "value" and "done" attributes.
            case OP_iterator_next: {
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // Pop all 4, saving iter / nextMethod / catch for re-push.
                stackPop(pContext); // sentinel (undefined or previous value — discarded)
                const proto::ProtoObject* catchOffIN = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* nextMethodIN = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* iterObjIN = stackTop(pContext); stackPop(pContext);

                const proto::ProtoString* slotKeyIN = JSSymbols::iterSlot(pContext);
                const proto::ProtoObject* slotValIN = (slotKeyIN && iterObjIN && iterObjIN != PROTO_NONE)
                    ? iterObjIN->getAttribute(pContext, slotKeyIN, false) : PROTO_NONE;

                const proto::ProtoObject* resultObjIN = PROTO_NONE;
                const proto::ProtoString* valueKeyIN = JSSymbols::value(pContext);
                const proto::ProtoString* doneKeyIN  = JSSymbols::done(pContext);

                if (slotValIN && slotValIN != PROTO_NONE && slotValIN->isInteger(pContext)) {
                    uint32_t bsIN = static_cast<uint32_t>(slotValIN->asLong(pContext));
                    const proto::ProtoObject* actualIter = getSlot(pContext, bsIN);
                    const proto::ProtoObject* idxObjIN   = getSlot(pContext, bsIN + 1);
                    long long idxIN = (idxObjIN && idxObjIN->isInteger(pContext))
                                      ? idxObjIN->asLong(pContext) : 0LL;

                    if (idxIN == -1LL) {
                        // Native iterator: call iter.next() and use the returned result object directly.
                        const proto::ProtoString* nextKeyIN = JSSymbols::next(pContext);
                        const proto::ProtoObject* nextFnIN =
                            (nextKeyIN && actualIter && actualIter != PROTO_NONE)
                            ? actualIter->getAttribute(pContext, nextKeyIN, false) : PROTO_NONE;
                        if (nextFnIN && nextFnIN != PROTO_NONE) {
                            if (nextFnIN->isMethod(pContext)) {
                                resultObjIN = nextFnIN->asMethod(pContext)(
                                    pContext, actualIter, nullptr, nullptr, nullptr);
                            } else {
                                const proto::ProtoList* emptyArgsIN = pContext->newList();
                                resultObjIN = callJSFunction(pContext, nextFnIN, actualIter, emptyArgsIN);
                            }
                        }
                    } else {
                        // Array/TypedArray: build synthetic {value, done} result object.
                        const proto::ProtoString* lenKeyIN = JSSymbols::length(pContext);
                        const proto::ProtoObject* lenValIN = lenKeyIN
                            ? actualIter->getAttribute(pContext, lenKeyIN, false) : PROTO_NONE;
                        long long arrLenIN = (lenValIN && lenValIN->isInteger(pContext))
                                             ? lenValIN->asLong(pContext) : 0LL;

                        const proto::ProtoObject* synResult = pContext->newObject(false);
                        if (idxIN >= arrLenIN) {
                            if (valueKeyIN) synResult = synResult->setAttribute(pContext, valueKeyIN, PROTO_NONE);
                            if (doneKeyIN)  synResult = synResult->setAttribute(pContext, doneKeyIN,  PROTO_TRUE);
                        } else {
                            // Fetch element (TypedArray aware).
                            const proto::ProtoObject* elemVal = PROTO_NONE;
                            uint8_t taTypeIN = getTypedArrayElementType(pContext, actualIter);
                            if (taTypeIN != 0xFF) {
                                elemVal = typedArrayGetElement(pContext, actualIter,
                                    static_cast<uint32_t>(idxIN), taTypeIN);
                            } else {
                                std::string eidxStr = std::to_string(idxIN);
                                const proto::ProtoObject* eidxObj =
                                    pContext->fromUTF8String(eidxStr.c_str());
                                const proto::ProtoString* eidxKey =
                                    eidxObj ? eidxObj->asString(pContext) : nullptr;
                                elemVal = eidxKey
                                    ? actualIter->getAttribute(pContext, eidxKey, false) : PROTO_NONE;
                            }
                            setSlot(pContext, bsIN + 1, pContext->fromInteger(idxIN + 1LL));
                            if (valueKeyIN)
                                synResult = synResult->setAttribute(pContext, valueKeyIN,
                                    elemVal ? elemVal : PROTO_NONE);
                            if (doneKeyIN)
                                synResult = synResult->setAttribute(pContext, doneKeyIN, PROTO_FALSE);
                        }
                        resultObjIN = synResult;
                    }
                }

                // Push back [iter, nextMethod, catch_offset, result_obj].
                stackPush(pContext, iterObjIN);
                stackPush(pContext, nextMethodIN);
                stackPush(pContext, catchOffIN ? catchOffIN : pContext->fromInteger(0LL));
                stackPush(pContext, resultObjIN ? resultObjIN : PROTO_NONE);
                break;
            }
```

- [ ] **Step 2.2: Build**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -5
```

Expected: build succeeds, `[100%] Linking CXX executable protojs` in last lines.

- [ ] **Step 2.3: Run manual tests**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
./build/protojs tests/manual/test_destructuring.js 2>&1
```

Expected output (rest will fail until Task 3):
```
PASS: basic array destructuring
PASS: skip element destructuring
...
```

Note: rest element test may still fail until `OP_iterator_call` is implemented (Task 3).

- [ ] **Step 2.4: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
git add src/runtime/ProtoInterpreter.cpp
git commit -m "feat(phase15): implement OP_iterator_next — enable array destructuring patterns"
```

---

## Task 3: Implement `OP_iterator_call` (rest elements)

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` lines ~4283–4287

- [ ] **Step 3.1: Replace the `OP_iterator_call` no-op**

Find this block (around line 4283):

```cpp
            // OP_iterator_call: DEF(iterator_call, 2, 4, 5, u8)
            // Used in advanced iterator protocol. Unsupported — preserve vacuous-pass.
            case OP_iterator_call:
                if (pc + 1 <= len) pc += 1; // skip u8 flags byte before returning
                return PROTO_NONE;
```

Replace it with:

```cpp
            // OP_iterator_call: DEF(iterator_call, 2, 4, 5, u8)
            // Stack before: [iter, nextMethod, catch_offset, sentinel]  (4 items consumed)
            // Stack after:  [iter, nextMethod, catch_offset, value, done] (5 items produced)
            // flags=1: collect all remaining iterator values into a rest array, done=false.
            // flags=0: iterator.return() cleanup path, value=undefined, done=true.
            case OP_iterator_call: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t icFlags = buf[pc++];
                if (stackSize(pContext) < 4) {
                    stackPush(pContext, PROTO_NONE);
                    return PROTO_NONE;
                }
                // Pop all 4, saving iter / nextMethod / catch for re-push.
                stackPop(pContext); // sentinel
                const proto::ProtoObject* catchOffIC  = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* nextMethodIC = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* iterObjIC   = stackTop(pContext); stackPop(pContext);

                const proto::ProtoObject* resultValIC  = PROTO_NONE;
                const proto::ProtoObject* resultDoneIC = PROTO_TRUE;

                if (icFlags == 1) {
                    // Collect remaining iterator values into a rest array.
                    const proto::ProtoString* slotKeyIC = JSSymbols::iterSlot(pContext);
                    const proto::ProtoObject* slotValIC =
                        (slotKeyIC && iterObjIC && iterObjIC != PROTO_NONE)
                        ? iterObjIC->getAttribute(pContext, slotKeyIC, false) : PROTO_NONE;

                    const proto::ProtoObject* restArr = pContext->newObject(false);
                    const proto::ProtoString* lenKeyIC = JSSymbols::length(pContext);
                    long long restIdx = 0;

                    if (slotValIC && slotValIC != PROTO_NONE && slotValIC->isInteger(pContext)) {
                        uint32_t bsIC = static_cast<uint32_t>(slotValIC->asLong(pContext));
                        const proto::ProtoObject* actualIterIC = getSlot(pContext, bsIC);
                        const proto::ProtoObject* idxObjIC = getSlot(pContext, bsIC + 1);
                        long long idxIC = (idxObjIC && idxObjIC->isInteger(pContext))
                                          ? idxObjIC->asLong(pContext) : 0LL;

                        if (idxIC == -1LL) {
                            // Native iterator: drain via repeated next() calls.
                            const proto::ProtoString* nextKeyIC = JSSymbols::next(pContext);
                            const proto::ProtoString* doneKeyIC2 = JSSymbols::done(pContext);
                            const proto::ProtoString* valueKeyIC2 = JSSymbols::value(pContext);
                            const proto::ProtoObject* nextFnIC =
                                (nextKeyIC && actualIterIC && actualIterIC != PROTO_NONE)
                                ? actualIterIC->getAttribute(pContext, nextKeyIC, false) : PROTO_NONE;
                            while (nextFnIC && nextFnIC != PROTO_NONE) {
                                const proto::ProtoObject* resIC = PROTO_NONE;
                                if (nextFnIC->isMethod(pContext)) {
                                    resIC = nextFnIC->asMethod(pContext)(
                                        pContext, actualIterIC, nullptr, nullptr, nullptr);
                                } else {
                                    const proto::ProtoList* argsIC = pContext->newList();
                                    resIC = callJSFunction(pContext, nextFnIC, actualIterIC, argsIC);
                                }
                                const proto::ProtoObject* doneIC =
                                    (resIC && resIC != PROTO_NONE && doneKeyIC2)
                                    ? resIC->getAttribute(pContext, doneKeyIC2, false) : PROTO_TRUE;
                                bool isDoneIC = (!doneIC || doneIC == PROTO_NONE || doneIC == PROTO_TRUE);
                                if (isDoneIC) break;
                                const proto::ProtoObject* valIC =
                                    (resIC && resIC != PROTO_NONE && valueKeyIC2)
                                    ? resIC->getAttribute(pContext, valueKeyIC2, false) : PROTO_NONE;
                                std::string ridxStr = std::to_string(restIdx++);
                                const proto::ProtoObject* ridxObj =
                                    pContext->fromUTF8String(ridxStr.c_str());
                                const proto::ProtoString* ridxKey =
                                    ridxObj ? ridxObj->asString(pContext) : nullptr;
                                if (ridxKey)
                                    restArr = restArr->setAttribute(pContext, ridxKey,
                                        valIC ? valIC : PROTO_NONE);
                            }
                        } else {
                            // Array-based: slice from current index to end.
                            const proto::ProtoString* lenKeyIC2 = JSSymbols::length(pContext);
                            const proto::ProtoObject* lenValIC = lenKeyIC2
                                ? actualIterIC->getAttribute(pContext, lenKeyIC2, false) : PROTO_NONE;
                            long long arrLenIC = (lenValIC && lenValIC->isInteger(pContext))
                                                 ? lenValIC->asLong(pContext) : 0LL;
                            for (long long i = idxIC; i < arrLenIC; ++i) {
                                const proto::ProtoObject* evIC = PROTO_NONE;
                                uint8_t taTIC = getTypedArrayElementType(pContext, actualIterIC);
                                if (taTIC != 0xFF) {
                                    evIC = typedArrayGetElement(pContext, actualIterIC,
                                        static_cast<uint32_t>(i), taTIC);
                                } else {
                                    std::string eiStr = std::to_string(i);
                                    const proto::ProtoObject* eiObj =
                                        pContext->fromUTF8String(eiStr.c_str());
                                    const proto::ProtoString* eiKey =
                                        eiObj ? eiObj->asString(pContext) : nullptr;
                                    evIC = eiKey
                                        ? actualIterIC->getAttribute(pContext, eiKey, false) : PROTO_NONE;
                                }
                                std::string riStr = std::to_string(restIdx++);
                                const proto::ProtoObject* riObj =
                                    pContext->fromUTF8String(riStr.c_str());
                                const proto::ProtoString* riKey =
                                    riObj ? riObj->asString(pContext) : nullptr;
                                if (riKey)
                                    restArr = restArr->setAttribute(pContext, riKey,
                                        evIC ? evIC : PROTO_NONE);
                            }
                            setSlot(pContext, bsIC + 1, pContext->fromInteger(arrLenIC));
                        }
                    }

                    if (lenKeyIC)
                        restArr = restArr->setAttribute(pContext, lenKeyIC,
                            pContext->fromInteger(restIdx));
                    resultValIC  = restArr;
                    resultDoneIC = PROTO_FALSE;
                }
                // flags=0 and others: cleanup path, value=undefined, done=true (defaults above).

                // Push back [iter, nextMethod, catch_offset, value, done].
                stackPush(pContext, iterObjIC);
                stackPush(pContext, nextMethodIC);
                stackPush(pContext, catchOffIC ? catchOffIC : pContext->fromInteger(0LL));
                stackPush(pContext, resultValIC  ? resultValIC  : PROTO_NONE);
                stackPush(pContext, resultDoneIC ? resultDoneIC : PROTO_TRUE);
                break;
            }
```

- [ ] **Step 3.2: Build**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
cmake --build build --target protojs -j$(nproc) 2>&1 | tail -5
```

Expected: clean build.

- [ ] **Step 3.3: Run full manual test suite**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
./build/protojs tests/manual/test_destructuring.js 2>&1
```

Expected full output:
```
PASS: basic array destructuring
PASS: rest element destructuring
PASS: skip element destructuring
PASS: for-of with destructuring
PASS: assignment destructuring
PASS: function parameter destructuring
ALL TESTS PASSED
```

If any test fails, check the opcode trace by adding `console.log` before the failing case and verifying stack state.

- [ ] **Step 3.4: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
git add src/runtime/ProtoInterpreter.cpp tests/manual/test_destructuring.js
git commit -m "feat(phase15): implement OP_iterator_call — rest element destructuring"
```

---

## Task 4: Run test262 snapshot for language/statements and language/expressions

**Files:** no source changes — run tests only.

- [ ] **Step 4.1: Run both category snapshots in parallel**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS=language/statements TEST262_USE_PROTO_EVAL=1 \
  TEST262_ROOT=../test262 node tests/test262/runner/test262_runner.js &
PID_STMT=$!
TEST262_PATTERNS=language/expressions TEST262_USE_PROTO_EVAL=1 \
  TEST262_ROOT=../test262 node tests/test262/runner/test262_runner.js &
PID_EXPR=$!
wait $PID_STMT $PID_EXPR
echo "Both suites complete"
```

Expected runtime: ~3–5 minutes each. Expect significant improvement vs Phase 14:
- `language/statements`: was 8167/9337 (87.5%), target ≥ 8700/9337 (93%+)
- `language/expressions`: was 9356/11036 (84.8%), target ≥ 9700/11036 (88%+)

- [ ] **Step 4.2: Find the new snapshot files**

```bash
ls -lt /home/gamarino/Documentos/proyectos/protoJS/tests/test262/reports/snapshot-language-*.json | head -4
```

Note the two most recent filenames.

---

## Task 5: Update TEST262_STATUS.md and commit

**Files:**
- Modify: `tests/test262/STATUS.md`

- [ ] **Step 5.1: Parse snapshot results**

```bash
python3 - << 'EOF'
import json, sys
files = [
    "tests/test262/reports/snapshot-language-expressions-XXXXXXXXXX.json",
    "tests/test262/reports/snapshot-language-statements-XXXXXXXXXX.json",
]
# Replace filenames with actual ones from Step 4.2
import glob, os
os.chdir("/home/gamarino/Documentos/proyectos/protoJS")
snapshots = sorted(glob.glob("tests/test262/reports/snapshot-language-*.json"),
                   key=os.path.getmtime, reverse=True)[:2]
for f in snapshots:
    with open(f) as fp: d = json.load(fp)
    s = d["summary"]
    pct = s["passed"] / d["total"] * 100
    print(f"{f.split('/')[-1]}")
    print(f"  {d['total']} total, {s['passed']} passed ({pct:.1f}%), "
          f"failSyntax={s['failed_syntax']}, failSem={s['failed_semantics']}, "
          f"timeout={s['timeout']}, date={d['generatedAt'][:10]}")
EOF
```

- [ ] **Step 5.2: Add Phase 15 rows to the `language/statements` table in STATUS.md**

In `tests/test262/STATUS.md`, in the `### language/statements` section, add a new row after the Phase 14 row:

```markdown
| **Phase 15: destructuring — OP_iterator_next + OP_iterator_call (2026-04-10)** | 9337 | **NNNN (XX.X%)** | 176 | NNN | 0 | +NN vs Phase 14; array/object/rest destructuring for for-of, assignments, params |
```

Replace `NNNN`, `XX.X%`, `NNN`, `+NN` with actual numbers from Step 5.1.

Also update `**Most recent snapshot:**` to point to the new snapshot file.

- [ ] **Step 5.3: Add Phase 15 row to the `language/expressions` table**

Same pattern in the `### language/expressions` section.

- [ ] **Step 5.4: Add Phase 15 notes block after the Phase 14 notes block**

```markdown
> **Phase 15: destructuring — OP_iterator_next + OP_iterator_call (2026-04-10):**
> 1. *`OP_iterator_next` implemented* — Stack before: `[iter, nextMethod, catch, sentinel]`; pops all 4, calls `iter.next()` (native path: sentinel=-1) or reads `arr[idx]` (array path: idx≥0) and builds a synthetic `{value, done}` result object; pushes back `[iter, nextMethod, catch, result_obj]`. The existing `OP_iterator_get_value_done` then extracts `value`/`done` from the result object.
> 2. *`OP_iterator_call flag=1` implemented* — Drains all remaining iterator values into a rest array. For array-based iterators slices from current slot index to end; for native iterators drains via repeated `next()` calls. Pushes `[iter, nextMethod, catch, rest_array, false]` (5 items).
> 3. *Destructuring categories fixed* — Array destructuring patterns now work in: for-of/dstr, for/dstr, assignment/dstr, function/dstr, arrow-function/dstr, generator/dstr.
```

- [ ] **Step 5.5: Update Phase History table**

Add a row to the Phase History table (bottom of STATUS.md):

```markdown
| 2026-04-10 | *(per-category; full-suite pending)* | — | Phase 15: +NNN statements, +NNN expressions vs Phase 14. Destructuring patterns working (for-of/dstr, assignment/dstr, function/dstr). Full-suite run needed. |
```

- [ ] **Step 5.6: Commit all changes**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
git add src/runtime/ProtoInterpreter.cpp tests/test262/STATUS.md tests/manual/test_destructuring.js
git commit -m "feat(phase15): destructuring (+NNN statements, +NNN expressions) — OP_iterator_next + OP_iterator_call

- OP_iterator_next: pop [iter, nextMethod, catch, sentinel], call iter.next() or
  read arr[idx], build {value,done} result object, push [iter, nextMethod, catch, result].
  Handles both native iterator (slot sentinel=-1) and array-index paths.
- OP_iterator_call flags=1: drain remaining iterator values into rest array.
  Array path slices slot[bs+1]..length; native path drains via next() loop.
- Fixes ~770 test262 failures across for-of/dstr, assignment/dstr, function/dstr,
  arrow-function/dstr, generator/dstr categories.
- STATUS.md: document Phase 15 results.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

Replace `+NNN` with actual deltas from Step 5.1.

---

## Self-Review

**Spec coverage:**
- ✅ `OP_iterator_next` with array-based iterator (array destructuring)
- ✅ `OP_iterator_next` with native iterator (destructuring of generator/iterator objects)
- ✅ `OP_iterator_call flags=1` for rest patterns
- ✅ `OP_iterator_call flags=0` for cleanup (no-value path)
- ✅ TypedArray destructuring (via `getTypedArrayElementType` / `typedArrayGetElement`)
- ✅ Manual tests covering basic, rest, skip, for-of, assignment, function-param patterns
- ✅ test262 snapshot before commit

**Placeholder scan:** No TBDs — all code is complete.

**Type consistency:** `valueKeyIN/doneKeyIN` in Task 2 match `JSSymbols::value`/`JSSymbols::done` used in `OP_for_of_next` and `OP_iterator_get_value_done`. All `stackPush`/`stackPop`/`stackAt`/`getSlot`/`setSlot` calls match existing interpreter API.

**Known limitation:** Object destructuring (`const {x, y} = obj`) does NOT use `OP_iterator_next` — it uses `OP_get_field` which is already implemented. Object pattern failures in test262 are a separate category.
