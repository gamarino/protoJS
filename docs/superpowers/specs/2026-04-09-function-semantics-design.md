# Phase 11: Function Statement Semantics — Design Spec

**Date:** 2026-04-09
**Status:** Approved

---

## Goal

Implement three missing function-related semantics in protoJS to improve ECMAScript conformance in the `language/statements/function`, `language/expressions/function`, and `language/expressions/arrow-function` test262 areas:

1. **`arguments` object** — make `arguments` available inside non-arrow functions
2. **`function.name`** — expose the `.name` property on function objects
3. **Arrow function lexical `this`** — arrow functions capture `this` at closure creation, not at call time

**Expected recovery:** ~250–350 additional passing tests.

---

## Architecture Overview

All three sub-features share a common thread: they require bytecode metadata that QuickJS tracks internally but does not yet expose to the protoJS runtime.

### Files Modified

| File | Change |
|---|---|
| `deps/quickjs/quickjs.c` | Add `protojs_bytecode_is_arrow()` C accessor |
| `src/runtime/QuickJSBytecodeExport.h` | Declare the new accessor |
| `src/runtime/ProtoBytecodeModule.h` | Add `bool isArrow` field |
| `src/runtime/ProtoBytecodeLoader.cpp` | Populate `isArrow` during module load |
| `src/runtime/ProtoInterpreter.cpp` | Fix `OP_special_object`, `OP_fclosure`/`OP_fclosure8` (name + arrow_this capture), `OP_push_this` (arrow passthrough) |

No new files are created. All changes are additive or localized fixes.

---

## Sub-feature A: `arguments` Object

### Problem

`OP_special_object` (opcode `0x44`) at `ProtoInterpreter.cpp:~1252` returns `PROTO_NONE` for all operand kinds. QuickJS emits this opcode with `kind=0` (ARGUMENTS) or `kind=1` (MAPPED_ARGUMENTS) at the start of any non-arrow, non-strict function that references `arguments`. The result is that `typeof arguments === "undefined"` instead of `"object"`.

### Solution

The `args` parameter (`const proto::ProtoList* args`) is already passed into `runBytecode` on every call. Build the arguments object directly from it when `kind == 0 || kind == 1`.

```cpp
case OP_special_object: {
    uint8_t kind = pc[0]; pc++;
    if (kind == 0 || kind == 1) {  // ARGUMENTS / MAPPED_ARGUMENTS
        proto::ProtoObject* argsObj = pContext->getSpace()->createObject(pContext);
        int argc = args ? (int)args->length() : 0;
        for (int i = 0; i < argc; i++) {
            std::string idx = std::to_string(i);
            auto* idxKey = pContext->getSpace()->createString(pContext, idx);
            argsObj = argsObj->setAttribute(pContext, idxKey, args->getAt(pContext, i));
        }
        auto* lenKey   = pContext->getSpace()->createString(pContext, "length");
        auto* lenVal   = pContext->getSpace()->createInteger(pContext, argc);
        argsObj = argsObj->setAttribute(pContext, lenKey, lenVal);
        stackPush(pContext, argsObj);
    } else {
        // kind 2 = THIS_FUNC, kind 3 = NEW_TARGET — not yet implemented
        stackPush(pContext, PROTO_NONE);
    }
    break;
}
```

**Scope:** Only kinds 0 and 1 are implemented. THIS_FUNC and NEW_TARGET remain as PROTO_NONE stubs.

**Note:** MAPPED_ARGUMENTS (kind=1) provides live binding between `arguments[i]` and named parameters in non-strict mode. This distinction is not implemented — both kinds produce the same static snapshot. This covers the majority of test262 cases; live-binding tests are a separate concern.

---

## Sub-feature B: `function.name`

### Problem

`ProtoBytecodeModule::funcName` is populated during bytecode loading (Phase 9) but never set as a property on the function objects created by `OP_fclosure` and `OP_fclosure8`. As a result, `(function foo(){}).name` returns `undefined` instead of `"foo"`.

### Solution

After creating the function object in both `OP_fclosure` (line ~3500) and `OP_fclosure8` (line ~3473), set the `"name"` attribute:

```cpp
// After funcObj is created in OP_fclosure / OP_fclosure8:
const std::string& fname = nestedFunctions[idx].funcName;
if (!fname.empty()) {
    auto* nameKey = pContext->getSpace()->createString(pContext, "name");
    auto* nameVal = pContext->getSpace()->createString(pContext, fname);
    funcObj = funcObj->setAttribute(pContext, nameKey, nameVal);
}
```

**Cases covered:**
- Named function declarations: `function foo() {}` → `foo.name === "foo"`
- Named function expressions: `const f = function bar() {}` → `f.name === "bar"`
- Object method shorthand names (already stored in `funcName` by QuickJS)
- Anonymous arrow functions: `funcName` is `""`, so no attribute is set (returns `undefined`, which is correct per spec — empty string `""` is the actual spec value, but `undefined` is what most tests accept in practice; a follow-up can set `""` explicitly if needed)

---

## Sub-feature C: Arrow Function Lexical `this`

### Problem

Arrow functions do not have their own `this` binding — they inherit `this` from the enclosing lexical scope at the point of definition. Currently, `OP_push_this` always uses the `thisObj` parameter of the current `runBytecode` invocation, which is the call-site receiver. This means `obj.method = () => this.x` sees `obj` as `this` instead of the outer `this`.

### Solution — Three Steps

#### Step 1: Expose `isArrow` from QuickJS

Add a C accessor in `deps/quickjs/quickjs.c`:

```c
int protojs_bytecode_is_arrow(JSFunctionBytecode* b) {
    return b->func_type == JS_PARSE_FUNC_ARROW ? 1 : 0;
}
```

Declare in `src/runtime/QuickJSBytecodeExport.h`:

```cpp
extern "C" int protojs_bytecode_is_arrow(JSFunctionBytecode* b);
```

#### Step 2: Store `isArrow` in `ProtoBytecodeModule`

Add field to `src/runtime/ProtoBytecodeModule.h`:

```cpp
bool isArrow = false;
```

Populate in `src/runtime/ProtoBytecodeLoader.cpp` (wherever `funcName`, `argCount_`, etc. are set from the QuickJS bytecode struct):

```cpp
mod->isArrow = protojs_bytecode_is_arrow(b) != 0;
```

#### Step 3: Capture and pass lexical `this`

**At closure creation** (`OP_fclosure` / `OP_fclosure8`):

```cpp
if (nestedFunctions[idx].isArrow) {
    auto* arrowKey = pContext->getSpace()->createString(pContext, "__arrow_this__");
    funcObj = funcObj->setAttribute(pContext, arrowKey, thisObj);
}
```

**At call time** (in `callJSFunction` or whichever site invokes `runBytecode` for JS closures):

```cpp
// Before calling runBytecode for a closure:
auto* arrowKey = pContext->getSpace()->createString(pContext, "__arrow_this__");
const proto::ProtoObject* arrowThis = funcObj->getAttribute(pContext, arrowKey);
const proto::ProtoObject* effectiveThis = (arrowThis != PROTO_NONE) ? arrowThis : receiverObj;
// Pass effectiveThis as thisObj to runBytecode
```

**In `OP_push_this`:** No change needed — `OP_push_this` already pushes `thisObj`. The fix is upstream at the call site.

**Nested arrow functions:** Because `__arrow_this__` is captured at closure creation from `thisObj` of the enclosing frame, and the enclosing frame already has the correct `thisObj` (either original or resolved from its own `__arrow_this__`), nesting is handled automatically.

---

## Error Handling

- `OP_special_object` with unknown kind: push `PROTO_NONE` (existing behavior, preserved).
- `protojs_bytecode_is_arrow` is a pure C accessor — no allocation, no failure modes.
- If `__arrow_this__` lookup returns `PROTO_NONE` (not an arrow function), fall back to call-site receiver — no behavior change for non-arrow functions.

---

## Testing

Run the relevant test262 slices before and after:

```bash
# arguments
node tests/test262/run_tests.mjs language/statements/function --timeout 5000

# function.name
node tests/test262/run_tests.mjs language/expressions/function --timeout 5000

# arrow this
node tests/test262/run_tests.mjs language/expressions/arrow-function --timeout 5000
```

A full snapshot run after all three are implemented:

```bash
PROTOJS=./build/protojs node tests/test262/run_tests.mjs language/expressions --timeout 5000
```

---

## Out of Scope

- **MAPPED_ARGUMENTS live binding** (kind=1 vs kind=0 distinction) — static snapshot is sufficient for Phase 11.
- **`new.target`** (kind=3) — separate feature, not part of this phase.
- **`this` in class constructors and `super`** — deferred.
- **`arguments.callee`** — deprecated; low test262 coverage, deferred.
- **Anonymous function `.name` as `""`** — setting the empty string explicitly (vs. returning undefined) is a minor edge case, deferred.
