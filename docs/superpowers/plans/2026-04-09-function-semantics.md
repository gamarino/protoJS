# Phase 11: Function Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the `arguments` object and arrow-function lexical `this` in protoJS to improve ECMAScript conformance.

**Architecture:** `OP_special_object` (kind=0/1) builds an array-like object from the `args` ProtoList already passed to `runBytecode`. Arrow `this` is captured as `__arrow_this__` on the function object at `OP_fclosure` time using a new `isArrow` flag loaded from QuickJS bytecode metadata. `function.name` is already implemented (lines 3485–3492, 3513–3520 of ProtoInterpreter.cpp).

**Tech Stack:** C++20, CMake, QuickJS (modified), protoCore, Node.js (test runner)

---

## File Map

| File | Change |
|---|---|
| `deps/quickjs/quickjs.c` | Add `is_arrow` bit to `JSFunctionBytecode`, set it during finalization, add `protojs_bytecode_is_arrow()` accessor |
| `src/runtime/QuickJSBytecodeExport.h` | Declare `protojs_bytecode_is_arrow()` |
| `src/runtime/ProtoBytecodeModule.h` | Add `bool isArrow{false}` field |
| `src/runtime/ProtoBytecodeLoader.cpp` | Load `isArrow` from bytecode |
| `src/runtime/ProtoInterpreter.cpp` | Fix `OP_special_object` (arguments), add `__arrow_this__` capture in `OP_fclosure`/`OP_fclosure8`, use `__arrow_this__` in `callJSFunction` |

---

## Task 1: Add `is_arrow` to QuickJS bytecode + accessor

**Files:**
- Modify: `deps/quickjs/quickjs.c:623–639` (struct), `deps/quickjs/quickjs.c:35762` (finalization), `deps/quickjs/quickjs.c:5560` (accessor)
- Modify: `src/runtime/QuickJSBytecodeExport.h:54` (declaration)

- [ ] **Step 1: Write a minimal test script to verify arrow detection (run after build)**

  Save as `/tmp/test_arrow.js`:
  ```js
  const obj = { x: 42, getX() { const a = () => this.x; return a(); } };
  console.log(obj.getX()); // must print 42, not undefined
  const arr = () => {};
  console.log(typeof arr); // function
  ```

- [ ] **Step 2: Add `is_arrow` bit to `JSFunctionBytecode` struct**

  In `deps/quickjs/quickjs.c`, find the struct definition at line ~623. The block with bitfields ends around line 638 with the comment `/* XXX: 10 bits available */`. Add `is_arrow` BEFORE that comment:

  ```c
  // Replace this:
      uint8_t is_direct_or_indirect_eval : 1; /* used by JS_GetScriptOrModuleName() */
      /* XXX: 10 bits available */

  // With this:
      uint8_t is_direct_or_indirect_eval : 1; /* used by JS_GetScriptOrModuleName() */
      uint8_t is_arrow : 1; /* true for arrow functions (no own this/arguments binding) */
      /* XXX: 9 bits available */
  ```

- [ ] **Step 3: Set `is_arrow` during bytecode finalization**

  In `deps/quickjs/quickjs.c`, find line ~35763 (after `b->arguments_allowed = fd->arguments_allowed;`). Add one line:

  ```c
  // After:
      b->arguments_allowed = fd->arguments_allowed;
      b->is_direct_or_indirect_eval = (fd->eval_type == JS_EVAL_TYPE_DIRECT ||
                                       fd->eval_type == JS_EVAL_TYPE_INDIRECT);

  // Insert between them:
      b->arguments_allowed = fd->arguments_allowed;
      b->is_arrow = (fd->func_type == JS_PARSE_FUNC_ARROW) ? 1 : 0;
      b->is_direct_or_indirect_eval = (fd->eval_type == JS_EVAL_TYPE_DIRECT ||
                                       fd->eval_type == JS_EVAL_TYPE_INDIRECT);
  ```

- [ ] **Step 4: Add `protojs_bytecode_is_arrow()` accessor in `quickjs.c`**

  Find the block of `protojs_*` accessors ending at line ~5560 (after `protojs_bytecode_func_name`). Append:

  ```c
  /* Return 1 if the function is an arrow function (no own this/arguments binding), 0 otherwise. */
  int protojs_bytecode_is_arrow(void *bytecode)
  {
      JSFunctionBytecode *b = (JSFunctionBytecode *)bytecode;
      if (!b) return 0;
      return b->is_arrow ? 1 : 0;
  }
  ```

- [ ] **Step 5: Declare `protojs_bytecode_is_arrow` in the export header**

  In `src/runtime/QuickJSBytecodeExport.h`, add after the existing `protojs_bytecode_func_name` declaration (line ~58):

  ```cpp
  /** Return 1 if the function is an arrow function (no own this/new.target binding), 0 otherwise. */
  int protojs_bytecode_is_arrow(void* bytecode);
  ```

- [ ] **Step 6: Commit**

  ```bash
  git add deps/quickjs/quickjs.c src/runtime/QuickJSBytecodeExport.h
  git commit -m "feat: add protojs_bytecode_is_arrow() accessor to QuickJS export layer"
  ```

---

## Task 2: Wire `isArrow` into `ProtoBytecodeModule`

**Files:**
- Modify: `src/runtime/ProtoBytecodeModule.h:47` (field)
- Modify: `src/runtime/ProtoBytecodeLoader.cpp:103–106` (loading)

- [ ] **Step 1: Add `isArrow` field to `ProtoBytecodeModule`**

  In `src/runtime/ProtoBytecodeModule.h`, find the `funcName` field (line ~47). Add `isArrow` directly after it:

  ```cpp
  // Replace:
      /** The function's declared name (empty for anonymous functions). */
      std::string funcName;

  // With:
      /** The function's declared name (empty for anonymous functions). */
      std::string funcName;
      /** True when this function is an arrow function (no own this/arguments). */
      bool isArrow{false};
  ```

- [ ] **Step 2: Load `isArrow` in `ProtoBytecodeLoader.cpp`**

  In `src/runtime/ProtoBytecodeLoader.cpp`, find line ~103–106 where `isStrict` and `funcName` are loaded:

  ```cpp
  // Current code (lines ~103–106):
      out->isStrict = protojs_bytecode_is_strict(quickjsBytecode) != 0;
      const char* funcNameCstr = protojs_bytecode_func_name(ctx, quickjsBytecode);
      out->funcName = funcNameCstr ? funcNameCstr : "";
      if (funcNameCstr) JS_FreeCString(ctx, funcNameCstr);

  // Replace with:
      out->isStrict = protojs_bytecode_is_strict(quickjsBytecode) != 0;
      out->isArrow  = protojs_bytecode_is_arrow(quickjsBytecode) != 0;
      const char* funcNameCstr = protojs_bytecode_func_name(ctx, quickjsBytecode);
      out->funcName = funcNameCstr ? funcNameCstr : "";
      if (funcNameCstr) JS_FreeCString(ctx, funcNameCstr);
  ```

- [ ] **Step 3: Build and verify compilation**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -5
  cmake --build . -j$(nproc) 2>&1 | tail -20
  ```

  Expected: build succeeds with 0 errors.

- [ ] **Step 4: Commit**

  ```bash
  git add src/runtime/ProtoBytecodeModule.h src/runtime/ProtoBytecodeLoader.cpp
  git commit -m "feat: add isArrow field to ProtoBytecodeModule, loaded from QuickJS bytecode"
  ```

---

## Task 3: Fix `OP_special_object` — implement `arguments` object

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp:1251–1257`

- [ ] **Step 1: Write a failing test script**

  Save as `/tmp/test_arguments.js`:
  ```js
  function sum() {
    var result = 0;
    for (var i = 0; i < arguments.length; i++) result += arguments[i];
    return result;
  }
  console.log(sum(1, 2, 3)); // expected: 6
  console.log(typeof arguments); // expected: undefined (top-level)
  function first(a, b) { return arguments[0]; }
  console.log(first(10, 20)); // expected: 10
  ```

- [ ] **Step 2: Run test script to verify current failure**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  ./build/protojs /tmp/test_arguments.js
  ```

  Expected (before fix): prints `NaN` or `0` for the first call, not `6`.

- [ ] **Step 3: Implement `arguments` object in `OP_special_object`**

  In `src/runtime/ProtoInterpreter.cpp`, find `case OP_special_object:` at line ~1251:

  ```cpp
  // Replace this entire case:
  case OP_special_object: {
      // TODO: Implement arguments/new.target/etc. wiring.
      if (pc + 1 > len) return PROTO_NONE;
      pc += 1; // skip kind
      stackPush(pContext,PROTO_NONE);
      break;
  }

  // With this:
  case OP_special_object: {
      if (pc + 1 > len) return PROTO_NONE;
      uint8_t soKind = buf[pc++];
      if (soKind == 0 || soKind == 1) {
          // ARGUMENTS / MAPPED_ARGUMENTS: build array-like object from args.
          // QuickJS only emits this in functions with has_arguments_binding, never in arrow fns.
          const proto::ProtoObject* argsObj = pContext->newObject(true);
          int argc2 = args ? static_cast<int>(args->getSize(pContext)) : 0;
          for (int ai = 0; ai < argc2; ai++) {
              std::string idxStr = std::to_string(ai);
              const proto::ProtoString* idxKey = pContext->fromUTF8String(idxStr.c_str());
              const proto::ProtoObject* argVal = args->getAt(pContext, ai);
              if (idxKey && argsObj)
                  argsObj = argsObj->setAttribute(pContext, idxKey, argVal ? argVal : PROTO_NONE);
          }
          const proto::ProtoString* lenKey2 = pContext->fromUTF8String("length");
          if (lenKey2 && argsObj)
              argsObj = argsObj->setAttribute(pContext, lenKey2, pContext->fromInteger(static_cast<long long>(argc2)));
          stackPush(pContext, argsObj ? argsObj : PROTO_NONE);
      } else {
          // kind 2 = THIS_FUNC, kind 3 = NEW_TARGET — not yet implemented.
          stackPush(pContext, PROTO_NONE);
      }
      break;
  }
  ```

- [ ] **Step 4: Build**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS/build
  cmake --build . -j$(nproc) 2>&1 | tail -10
  ```

  Expected: 0 errors.

- [ ] **Step 5: Run test script to verify fix**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  ./build/protojs /tmp/test_arguments.js
  ```

  Expected output:
  ```
  6
  undefined
  10
  ```

- [ ] **Step 6: Run test262 slice for arguments coverage**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  node tests/test262/run_tests.mjs language/statements/function --timeout 5000 2>&1 | tail -5
  ```

  Expected: more passes than before the fix.

- [ ] **Step 7: Commit**

  ```bash
  git add src/runtime/ProtoInterpreter.cpp
  git commit -m "feat: implement arguments object in OP_special_object (kinds 0 and 1)"
  ```

---

## Task 4: Implement arrow function lexical `this`

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp:3473–3527` (OP_fclosure8 + OP_fclosure)
- Modify: `src/runtime/ProtoInterpreter.cpp:4161–4208` (callJSFunction)

- [ ] **Step 1: Write a failing test script**

  Save as `/tmp/test_arrow_this.js`:
  ```js
  const obj = {
    x: 42,
    getX: function() {
      const arrow = () => this.x;
      return arrow();
    }
  };
  console.log(obj.getX()); // expected: 42

  function Counter(n) {
    this.count = n;
    this.inc = () => { this.count++; return this.count; };
  }
  const c = new Counter(0);
  console.log(c.inc()); // expected: 1
  console.log(c.inc()); // expected: 2
  ```

- [ ] **Step 2: Run test script to verify current failure**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  ./build/protojs /tmp/test_arrow_this.js
  ```

  Expected (before fix): `undefined` for `obj.getX()`, not `42`.

- [ ] **Step 3: Add `__arrow_this__` capture in `OP_fclosure8`**

  In `src/runtime/ProtoInterpreter.cpp`, find `case OP_fclosure8:` at line ~3473. Locate the block after setting the `.name` attribute (lines ~3485–3493). Add the arrow capture AFTER the name block, before `stackPush`:

  ```cpp
  // Current code after name block:
                      stackPush(pContext, fnInst);
                  } else {
                      stackPush(pContext, rawFn ? rawFn : PROTO_NONE);
                  }
                  break;
              }

  // Replace the `stackPush(pContext, fnInst);` line only (inside the if fnBcId8 >= 0 block):
  // Add after the name block ends (after the closing `}` of the name block):
                      // Capture lexical this for arrow functions.
                      if (static_cast<size_t>(fnBcId8) < nested.size() &&
                          nested[static_cast<size_t>(fnBcId8)].isArrow) {
                          const proto::ProtoString* arrowThisKey =
                              pContext->fromUTF8String("__arrow_this__");
                          if (arrowThisKey)
                              fnInst = fnInst->setAttribute(pContext, arrowThisKey,
                                  thisObj ? thisObj : PROTO_NONE);
                      }
                      stackPush(pContext, fnInst);
  ```

  The full resulting block for `OP_fclosure8` (replacing lines 3478–3497) should look like:

  ```cpp
              case OP_fclosure8: {
                  if (pc + 1 > len) return PROTO_NONE;
                  uint8_t idx = buf[pc++];
                  const proto::ProtoObject* rawFn = (idx < cpool.size()) ? cpool[idx] : PROTO_NONE;
                  int fnBcId8 = getBytecodeId(pContext, rawFn);
                  if (fnBcId8 >= 0) {
                      const proto::ProtoObject* fnInst = pContext->newObject(true);
                      fnInst = fnInst->setAttribute(pContext, JSSymbols::bytecodeId(pContext),
                          pContext->fromInteger(static_cast<long long>(fnBcId8)));
                      fnInst = fnInst->setAttribute(pContext, JSSymbols::prototype(pContext),
                          pContext->newObject(true));
                      // Set function name from bytecode metadata.
                      if (static_cast<size_t>(fnBcId8) < nested.size()) {
                          const std::string& fn8Name = nested[static_cast<size_t>(fnBcId8)].funcName;
                          if (!fn8Name.empty()) {
                              const proto::ProtoObject* nameVal = pContext->fromUTF8String(fn8Name.c_str());
                              if (nameVal)
                                  fnInst = fnInst->setAttribute(pContext, JSSymbols::name(pContext), nameVal);
                          }
                          // Capture lexical this for arrow functions.
                          if (nested[static_cast<size_t>(fnBcId8)].isArrow) {
                              const proto::ProtoString* arrowThisKey =
                                  pContext->fromUTF8String("__arrow_this__");
                              if (arrowThisKey)
                                  fnInst = fnInst->setAttribute(pContext, arrowThisKey,
                                      thisObj ? thisObj : PROTO_NONE);
                          }
                      }
                      stackPush(pContext, fnInst);
                  } else {
                      stackPush(pContext, rawFn ? rawFn : PROTO_NONE);
                  }
                  break;
              }
  ```

- [ ] **Step 4: Add `__arrow_this__` capture in `OP_fclosure`**

  In `src/runtime/ProtoInterpreter.cpp`, find `case OP_fclosure:` at line ~3500. Apply the identical change to its name block. The full resulting block should look like:

  ```cpp
              case OP_fclosure: {
                  if (pc + 4 > len) return PROTO_NONE;
                  uint32_t idx = get_u32(buf + pc);
                  pc += 4;
                  const proto::ProtoObject* rawFn2 = (idx < cpool.size()) ? cpool[idx] : PROTO_NONE;
                  int fnBcId2 = getBytecodeId(pContext, rawFn2);
                  if (fnBcId2 >= 0) {
                      const proto::ProtoObject* fnInst2 = pContext->newObject(true);
                      fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::bytecodeId(pContext),
                          pContext->fromInteger(static_cast<long long>(fnBcId2)));
                      fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::prototype(pContext),
                          pContext->newObject(true));
                      // Set function name from bytecode metadata.
                      if (static_cast<size_t>(fnBcId2) < nested.size()) {
                          const std::string& fn2Name = nested[static_cast<size_t>(fnBcId2)].funcName;
                          if (!fn2Name.empty()) {
                              const proto::ProtoObject* nameVal2 = pContext->fromUTF8String(fn2Name.c_str());
                              if (nameVal2)
                                  fnInst2 = fnInst2->setAttribute(pContext, JSSymbols::name(pContext), nameVal2);
                          }
                          // Capture lexical this for arrow functions.
                          if (nested[static_cast<size_t>(fnBcId2)].isArrow) {
                              const proto::ProtoString* arrowThisKey2 =
                                  pContext->fromUTF8String("__arrow_this__");
                              if (arrowThisKey2)
                                  fnInst2 = fnInst2->setAttribute(pContext, arrowThisKey2,
                                      thisObj ? thisObj : PROTO_NONE);
                          }
                      }
                      stackPush(pContext, fnInst2);
                  } else {
                      stackPush(pContext, rawFn2 ? rawFn2 : PROTO_NONE);
                  }
                  break;
              }
  ```

- [ ] **Step 5: Use `__arrow_this__` in `callJSFunction`**

  In `src/runtime/ProtoInterpreter.cpp`, find `callJSFunction` at line ~4161. Find the block that resolves the module and calls `runBytecode` (lines ~4191–4204). Add `__arrow_this__` resolution before the `runBytecode` call:

  ```cpp
  // Current code (lines ~4191–4204):
      if (resolveMod) {
          const ProtoBytecodeModule& nf = resolveMod->nestedFunctions[bcId];
          proto::ProtoContext childCtx(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr);
          childCtx.currentFileName = ctx->currentFileName;
          childCtx.currentLineNumber = ctx->currentLineNumber;
          unsigned argc = args ? static_cast<unsigned>(args->getSize(ctx)) : 0u;
          for (unsigned i = 0; i < argc; i++)
              setSlot(&childCtx, i, args->getAt(&childCtx, static_cast<int>(i)));
          const proto::ProtoObject* childEx = PROTO_NONE;
          const proto::ProtoObject* result =
              runBytecode(&childCtx, &nf, thisVal ? thisVal : PROTO_NONE, args, globalRoot, &childEx);
          childCtx.returnValue = result;
          // Exceptions from callbacks are silently suppressed at this level.
          return result ? result : PROTO_NONE;
      }

  // Replace with:
      if (resolveMod) {
          const ProtoBytecodeModule& nf = resolveMod->nestedFunctions[bcId];
          // Arrow functions capture lexical this at closure creation; use it instead of call-site receiver.
          const proto::ProtoObject* effectiveThis = thisVal ? thisVal : PROTO_NONE;
          if (nf.isArrow) {
              const proto::ProtoString* arrowKey = ctx->fromUTF8String("__arrow_this__");
              if (arrowKey) {
                  const proto::ProtoObject* captured = fn->getAttribute(ctx, arrowKey);
                  if (captured && captured != PROTO_NONE)
                      effectiveThis = captured;
              }
          }
          proto::ProtoContext childCtx(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr);
          childCtx.currentFileName = ctx->currentFileName;
          childCtx.currentLineNumber = ctx->currentLineNumber;
          unsigned argc = args ? static_cast<unsigned>(args->getSize(ctx)) : 0u;
          for (unsigned i = 0; i < argc; i++)
              setSlot(&childCtx, i, args->getAt(&childCtx, static_cast<int>(i)));
          const proto::ProtoObject* childEx = PROTO_NONE;
          const proto::ProtoObject* result =
              runBytecode(&childCtx, &nf, effectiveThis, args, globalRoot, &childEx);
          childCtx.returnValue = result;
          return result ? result : PROTO_NONE;
      }
  ```

- [ ] **Step 6: Build**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS/build
  cmake --build . -j$(nproc) 2>&1 | tail -10
  ```

  Expected: 0 errors.

- [ ] **Step 7: Run arrow this test**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  ./build/protojs /tmp/test_arrow_this.js
  ```

  Expected output:
  ```
  42
  1
  2
  ```

- [ ] **Step 8: Run combined test with arguments + arrow this**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  ./build/protojs /tmp/test_arguments.js
  ./build/protojs /tmp/test_arrow_this.js
  ```

  Both must produce correct output.

- [ ] **Step 9: Run test262 slices**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  node tests/test262/run_tests.mjs language/expressions/arrow-function --timeout 5000 2>&1 | tail -5
  node tests/test262/run_tests.mjs language/statements/function --timeout 5000 2>&1 | tail -5
  ```

- [ ] **Step 10: Commit**

  ```bash
  git add src/runtime/ProtoInterpreter.cpp
  git commit -m "feat: implement arrow-function lexical this via __arrow_this__ capture at OP_fclosure"
  ```

---

## Task 5: Full test262 snapshot + update TEST262_STATUS.md

**Files:**
- Modify: `docs/TEST262_STATUS.md`

- [ ] **Step 1: Run full test262 snapshot**

  ```bash
  cd /home/gamarino/Documentos/proyectos/protoJS
  node tests/test262/run_tests.mjs language/expressions --timeout 5000 2>&1 | tail -10
  ```

  Record the total pass count and percentage.

- [ ] **Step 2: Run language/statements slice for completeness**

  ```bash
  node tests/test262/run_tests.mjs language/statements --timeout 5000 2>&1 | tail -10
  ```

- [ ] **Step 3: Update `docs/TEST262_STATUS.md`**

  Add a new Phase 11 section with:
  - New pass count and percentage
  - Delta from Phase 10 (baseline: 9,219/11,036 = 83.5%)
  - Resolved items: `arguments` object, arrow-function lexical `this`
  - Changelog entry with date 2026-04-09

  Mark Phase 10 as superseded.

- [ ] **Step 4: Commit TEST262_STATUS.md**

  ```bash
  git add docs/TEST262_STATUS.md
  git commit -m "docs: update TEST262_STATUS.md for Phase 11 (arguments, arrow this)"
  ```
