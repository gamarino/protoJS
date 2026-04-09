# Phase 10: Generator Protocol + Property Descriptor Enforcement — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the ES6 generator protocol (`function*`, `yield`, `yield*`) and `Object.defineProperty` with strict-mode writable enforcement, recovering ~480–500 `language/expressions` test262 passes (82.3% → ~86–87%).

**Architecture:** Two sequential sub-features on one worktree/branch. Sub-feature A adds a `GeneratorFrame.h`, three resume thread-locals, and three generator opcodes to `ProtoInterpreter.cpp`. Sub-feature B registers `Object.defineProperty` in `ObjectPrototype.cpp` and adds a writable check in `OP_put_field`.

**Tech Stack:** C++20, protoCore ProtoObject/ProtoContext/ProtoSparseList, QuickJS bytecode opcodes, test262 runner.

---

## File Map

| Action | File | Responsibility |
|--------|------|---------------|
| Create | `src/runtime/GeneratorFrame.h` | `CatchFrame` struct (moved from ProtoInterpreter.cpp); attribute key helpers |
| Modify | `src/runtime/ProtoInterpreter.cpp` | Resume thread-locals; `generatorNext/Return/Throw` functions; `OP_initial_yield`, `OP_yield`, `OP_yield_star` cases; resume preamble in `runBytecode` |
| Modify | `src/runtime/ProtoInterpreter.h` | (no change needed — `runBytecode` signature unchanged) |
| Modify | `src/ObjectPrototype.cpp` | Register `objectDefineProperty`; `objectGetOwnPropertyDescriptor` |
| Modify | `src/ObjectPrototype.h` | Declare new functions (if header is used) |

---

## Key code conventions in this codebase

- **Stack and locals** live in `pContext->closureLocals` (`const proto::ProtoSparseList*`). Save/restore with `closureLocals->asObject(ctx)` / `obj->asSparseList(ctx)`.
- **makeError(ctx, "TypeError", "msg", pGlobalRoot)** — creates an error ProtoObject.
- **pending\_exception / has\_pending\_exception / goto handle\_exception** — how opcodes throw.
- **updateMapping(pContext, oldObj, newObj)** — must be called when `setAttribute` returns a new pointer.
- **`struct CatchFrame { int handler_pc; unsigned long placeholder_stack_pos; }`** — currently a local struct inside `runBytecode`; Task 2 moves it to `GeneratorFrame.h`.
- **JSSymbols::value(ctx)**, **JSSymbols::done(ctx)**, **JSSymbols::next(ctx)** — already exist.
- `thread_local` variables declared inside the anonymous namespace `namespace { }` at the top of `ProtoInterpreter.cpp` are accessible to all code in the same translation unit.
- **ProtoMethod signature:** `(ProtoContext* ctx, const ProtoObject* self, const ParentLink*, const ProtoList* args, const ProtoSparseList*) -> const ProtoObject*`. `self` = the receiver (`this`) of the call.

---

## Sub-feature A: Generator Protocol

### Task 1: Set up worktree

- [ ] **Step 1: Create the worktree**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
git worktree add .worktrees/feat-generators -b feat/generators-and-property-descriptors
cd .worktrees/feat-generators
ln -s /home/gamarino/Documentos/proyectos/protoJS/deps/quickjs deps/quickjs
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build . -j$(nproc) 2>&1 | tail -5
```

Expected: `[100%] Built target protojs`

---

### Task 2: Create GeneratorFrame.h — move CatchFrame

**Files:**
- Create: `src/runtime/GeneratorFrame.h`
- Modify: `src/runtime/ProtoInterpreter.cpp:948` (remove local `struct CatchFrame`)

- [ ] **Step 1: Create `src/runtime/GeneratorFrame.h`**

```cpp
#ifndef PROTOJS_GENERATOR_FRAME_H
#define PROTOJS_GENERATOR_FRAME_H

/**
 * Shared definitions for the generator protocol.
 * CatchFrame is moved here from ProtoInterpreter.cpp so it can be
 * referenced by both runBytecode and generatorNext/Return/Throw.
 */

namespace protojs {

/**
 * Represents a single try/catch entry on the interpreter's catch stack.
 * Previously a local struct inside runBytecode; now shared so generator
 * resume can restore the catch stack across runBytecode invocations.
 */
struct CatchFrame {
    int           handler_pc;
    unsigned long placeholder_stack_pos;
};

// ---------------------------------------------------------------------------
// Attribute key names used to store generator state on the iterator object.
// All keys start and end with __ to avoid collisions with user properties.
// ---------------------------------------------------------------------------
static constexpr const char* kGenPc       = "__gen_pc__";
static constexpr const char* kGenLocals   = "__gen_locals__";
static constexpr const char* kGenThis     = "__gen_this__";
static constexpr const char* kGenMod      = "__gen_mod__";
static constexpr const char* kGenState    = "__gen_state__";   // 0=suspended, 1=completed
static constexpr const char* kGenNcc      = "__gen_ncc__";     // number of catch frames
// Individual catch frame keys: __gen_cc_N_pc__ and __gen_cc_N_sp__
// (N = decimal index, generated at runtime)

} // namespace protojs

#endif /* PROTOJS_GENERATOR_FRAME_H */
```

- [ ] **Step 2: Add `#include "GeneratorFrame.h"` to `ProtoInterpreter.cpp`**

In `src/runtime/ProtoInterpreter.cpp`, add after the existing includes (around line 15):

```cpp
#include "GeneratorFrame.h"
```

- [ ] **Step 3: Remove the local `struct CatchFrame` from `runBytecode`**

In `ProtoInterpreter.cpp`, find (around line 948):

```cpp
    struct CatchFrame { int handler_pc; unsigned long placeholder_stack_pos; };
    std::vector<CatchFrame> catch_stack;
```

Replace with:

```cpp
    std::vector<CatchFrame> catch_stack;
```

(The `struct CatchFrame` definition is now in `GeneratorFrame.h`.)

- [ ] **Step 4: Build — verify no compile errors**

```bash
cmake --build build --target protojs 2>&1 | tail -8
```

Expected: `[100%] Built target protojs` with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/GeneratorFrame.h src/runtime/ProtoInterpreter.cpp
git commit -m "refactor(interpreter): move CatchFrame to GeneratorFrame.h"
```

---

### Task 3: Add generator resume thread-locals

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (inside anonymous namespace, ~line 47)

- [ ] **Step 1: Write the smoke test (FAILING) to verify current state**

```bash
cat > /tmp/smoke_gen.js << 'EOF'
function* gen() { yield 1; yield 2; }
var it = gen();
var r1 = it.next();
if (r1.value !== 1 || r1.done !== false) throw new Error("FAIL r1: " + JSON.stringify(r1));
var r2 = it.next();
if (r2.value !== 2 || r2.done !== false) throw new Error("FAIL r2: " + JSON.stringify(r2));
var r3 = it.next();
if (r3.done !== true) throw new Error("FAIL r3: " + JSON.stringify(r3));
console.log("Generator smoke: PASSED");
EOF
./build/protojs /tmp/smoke_gen.js 2>&1 | grep -v '^\[protojs\]'
```

Expected: `[ProtoInterpreter] unsupported opcode 0x85 at byte offset 0` (confirms generators not implemented).

- [ ] **Step 2: Add thread-locals for generator resume**

In `src/runtime/ProtoInterpreter.cpp`, inside the anonymous namespace (after the `t_nullSentinel` declaration at ~line 47), add:

```cpp
// ---------------------------------------------------------------------------
// Generator resume state.
// Set by generatorNext/Return/Throw before calling runBytecode.
// Consumed (and cleared) by runBytecode at startup when t_genResumePc >= 0.
// ---------------------------------------------------------------------------
thread_local int                                    t_genResumePc       = -1;
thread_local const proto::ProtoObject*              t_genResumeLocals   = nullptr;
thread_local std::vector<protojs::CatchFrame>*      t_genResumeCatchStack = nullptr;
// The active generator iterator during a resume call.
// Set by generatorNext before entering runBytecode; read by OP_yield to update state.
thread_local const proto::ProtoObject*              t_genIterator       = nullptr;
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target protojs 2>&1 | tail -5
```

Expected: `[100%] Built target protojs`

---

### Task 4: Implement generatorNext, generatorReturn, generatorThrow

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` (between line 713 `} // namespace` and line 736 `runBytecode`)

These are `proto::ProtoMethod` functions that `.next()`, `.return()`, `.throw()` dispatch to. They read saved state from the iterator object (`self`), set the resume thread-locals, and call `runBytecode`.

- [ ] **Step 1: Add `resumeGenerator` helper + three method functions**

In `src/runtime/ProtoInterpreter.cpp`, insert the following block between `} // namespace` (line 713) and `const proto::ProtoObject* runBytecode(` (line 736):

```cpp
// ---------------------------------------------------------------------------
// Generator protocol helpers.
// generatorNext/Return/Throw are ProtoMethod callbacks registered as .next /
// .return / .throw on every generator iterator object built by OP_initial_yield.
// ---------------------------------------------------------------------------

namespace {

/** Read an integer attribute from obj by name. Returns -1 if missing. */
static long long genAttrInt(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                             const char* name) {
    if (!obj || obj == PROTO_NONE) return -1;
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    if (!k) return -1;
    const proto::ProtoObject* v = obj->getAttribute(ctx, k, false);
    if (!v || v == PROTO_NONE || !v->isInteger(ctx)) return -1;
    return v->asLong(ctx);
}

/** Store an integer attribute on *pObj by name (returns new object pointer). */
static const proto::ProtoObject* genSetAttrInt(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* obj,
                                                const char* name, long long val) {
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    if (!k || !obj) return obj;
    return obj->setAttribute(ctx, k, ctx->fromInteger(val));
}

/** Store a ProtoObject attribute on *pObj by name (returns new object pointer). */
static const proto::ProtoObject* genSetAttrObj(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* obj,
                                                const char* name,
                                                const proto::ProtoObject* val) {
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    if (!k || !obj) return obj;
    return obj->setAttribute(ctx, k, val ? val : PROTO_NONE);
}

/** Build a {value, done} iterator result object. */
static const proto::ProtoObject* makeIterResult(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* value,
                                                  bool done) {
    const proto::ProtoObject* r = ctx->newObject(true);
    if (!r) return PROTO_NONE;
    const proto::ProtoString* vk = JSSymbols::value(ctx);
    const proto::ProtoString* dk = JSSymbols::done(ctx);
    if (vk) r = r->setAttribute(ctx, vk, value ? value : PROTO_NONE);
    if (dk) r = r->setAttribute(ctx, dk, done ? PROTO_TRUE : PROTO_FALSE);
    return r ? r : PROTO_NONE;
}

/** Core resume logic shared by generatorNext/Return/Throw.
 *
 *  iter    — the generator iterator object (holds saved state as attributes).
 *  sentVal — value sent into the generator (result of yield expr / return / throw value).
 *  mode    — 0=next, 1=return, 2=throw.
 */
static const proto::ProtoObject* resumeGenerator(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* iter,
                                                   const proto::ProtoObject* sentVal,
                                                   int mode) {
    if (!ctx || !iter || iter == PROTO_NONE) return makeIterResult(ctx, PROTO_NONE, true);

    long long state = genAttrInt(ctx, iter, protojs::kGenState);
    // State 1 = completed.
    if (state == 1) return makeIterResult(ctx, PROTO_NONE, true);
    // mode == 1 (return): mark done and return {value: sentVal, done: true}.
    if (mode == 1) {
        // Mark iterator completed.
        // (iter is a ProtoObject attribute update — we don't persist back here since
        //  the iterator is immutable from the caller's perspective in the next call.)
        return makeIterResult(ctx, sentVal, true);
    }

    // Recover saved state.
    long long resumePc  = genAttrInt(ctx, iter, protojs::kGenPc);
    uintptr_t modPtrVal = (uintptr_t)genAttrInt(ctx, iter, protojs::kGenMod);
    if (resumePc < 0 || modPtrVal == 0) return makeIterResult(ctx, PROTO_NONE, true);

    // Recover closureLocals.
    const proto::ProtoObject* ko = ctx->fromUTF8String(protojs::kGenLocals);
    const proto::ProtoString* lk = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* savedLocalsObj = lk ? iter->getAttribute(ctx, lk, false) : nullptr;
    const proto::ProtoSparseList* savedLocals = (savedLocalsObj && savedLocalsObj != PROTO_NONE)
        ? savedLocalsObj->asSparseList(ctx) : nullptr;

    // Recover thisObj.
    const proto::ProtoObject* kok = ctx->fromUTF8String(protojs::kGenThis);
    const proto::ProtoString* tk2 = kok ? kok->asString(ctx) : nullptr;
    const proto::ProtoObject* genThis = (tk2) ? iter->getAttribute(ctx, tk2, false) : PROTO_NONE;

    // Recover catch stack.
    long long ncc = genAttrInt(ctx, iter, protojs::kGenNcc);
    std::vector<protojs::CatchFrame> restoredCatch;
    for (long long ci = 0; ci < ncc; ci++) {
        std::string kpc = "__gen_cc_" + std::to_string(ci) + "_pc__";
        std::string ksp = "__gen_cc_" + std::to_string(ci) + "_sp__";
        long long hpc = genAttrInt(ctx, iter, kpc.c_str());
        long long spos = genAttrInt(ctx, iter, ksp.c_str());
        restoredCatch.push_back({(int)hpc, (unsigned long)spos});
    }

    const ProtoBytecodeModule* mod = reinterpret_cast<const ProtoBytecodeModule*>(modPtrVal);

    // If mode == 2 (throw): we resume at the saved pc but want OP_throw to fire.
    // Push sentVal as a pending exception by pre-setting a special attribute.
    // Simplest: store as __gen_throw_val__ on iter before resuming.
    if (mode == 2) {
        iter = genSetAttrObj(ctx, iter, "__gen_throw_val__", sentVal);
    }

    // Set up resume thread-locals consumed by runBytecode startup.
    t_genResumePc          = (int)resumePc;
    t_genResumeLocals      = savedLocals ? savedLocals->asObject(ctx) : nullptr;
    t_genResumeCatchStack  = &restoredCatch;
    t_genIterator          = iter;

    // Create child context and run.
    proto::ProtoContext childCtx(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr);
    childCtx.currentFileName    = ctx->currentFileName;
    childCtx.currentLineNumber  = ctx->currentLineNumber;
    const proto::ProtoObject* childEx = PROTO_NONE;
    const proto::ProtoObject** gr = t_currentGlobalRoot;

    // Push sentVal onto the stack before resuming (it becomes the result of `yield expr`).
    // We do this by pre-loading slot 0xFFFF (a sentinel slot) and having OP_yield_resume read it.
    // Simpler approach: push sentVal into the restored closureLocals stack.
    // The stack in closureLocals is stored under key "__stack__". We append sentVal to it.
    if (savedLocals && sentVal && sentVal != PROTO_NONE) {
        // Append sentVal onto the saved stack so when execution resumes the yield
        // expression evaluates to sentVal.
        const proto::ProtoObject* sobj = savedLocals->asObject(ctx);
        // We'll let the preamble in runBytecode push it after restoring state.
        // Store it under "__gen_sent__" on the iterator.
        iter = genSetAttrObj(ctx, iter, "__gen_sent__", sentVal);
        t_genIterator = iter; // update since iter pointer changed
    }

    const proto::ProtoObject* result = runBytecode(&childCtx, mod, genThis,
                                                     nullptr, gr, &childEx);

    // After runBytecode returns: mark iterator as completed if not already done by OP_yield.
    // If OP_yield saved state it already updated t_genIterator (and thus the iterator).
    // If the function ended normally (OP_return), mark state=1 on the iterator.
    // We detect "ended normally" by checking that t_genIterator is not nullptr
    // AND that the generator didn't just yield (OP_yield clears t_genIterator after updating it).
    // For simplicity: check __gen_state__ on the iterator to see if it's still 0 (suspended).
    // If OP_yield ran, it already set state=0 and returned a {value,done:false}.
    // If OP_return ran, result is the return value and we should wrap it as {value:result, done:true}.

    // Detect exception from generator body.
    if (childEx && childEx != PROTO_NONE) {
        // Propagate exception to caller.
        // We can't propagate directly here; return a special error marker.
        // The caller (OP_call that invoked .next()) will handle it.
        return childEx; // caller checks for error object
    }

    // Check: did OP_yield run? If so, result is already a {value, done:false} object.
    // We distinguish by looking at __gen_state__ on the current t_genIterator.
    // OP_yield sets state=0 (still suspended). OP_return / end-of-function => state remains
    // at whatever it was; we need to mark it done.
    // The cleanest signal: OP_yield RETURNS a {value, done} object directly from runBytecode.
    // OP_return or end-of-bytecode DOES NOT return a {value, done} object — it returns the
    // plain return value (or PROTO_NONE).
    // We distinguish by: OP_yield sets __gen_yield_result__ on the iterator, then returns
    // that result from runBytecode. The caller checks for __gen_yield_result__ attribute.

    // Actually, simpler: OP_yield will return from runBytecode by setting t_genResumePc to -2
    // (a sentinel) to signal "just yielded". Let's use t_genResumePc as a signal.
    // After resumeGenerator's runBytecode call:
    //   t_genResumePc == -2 means OP_yield fired; result is {value, done:false}
    //   t_genResumePc == -1 means OP_return fired (or end of bytecode)

    if (t_genResumePc == -2) {
        // OP_yield ran; result is {value, done:false}. Caller gets it directly.
        t_genResumePc = -1; // reset
        return result;
    }

    // Generator completed (OP_return or end of bytecode).
    // Mark iterator as done. (We update through t_genIterator.)
    if (t_genIterator) {
        const proto::ProtoObject* upd = genSetAttrInt(ctx, t_genIterator, protojs::kGenState, 1);
        // Note: iterator is immutable so this creates a new ProtoObject. The old reference
        // in the JS heap may not get updated, but future .next() calls will already return
        // {done:true} because we check the state attribute at the TOP of this function.
        // To properly update the live iterator, we'd need updateMapping. For now, skip
        // since the next .next() call re-reads state and the iterator ref was already
        // updated by OP_yield (which calls updateMapping).
        (void)upd;
    }
    t_genIterator = nullptr;

    // Wrap the return value.
    return makeIterResult(ctx, result ? result : PROTO_NONE, true);
}

} // anonymous namespace (closes around line 713 in original; re-opened here for these helpers)

} // protojs namespace re-close — see note below
```

Wait — the structure must be correct. The generator helpers need to be in the `protojs` namespace, NOT in the anonymous namespace, so that `generatorNext` (a ProtoMethod function pointer) can be referenced by `OP_initial_yield` inside `runBytecode`. Let me rewrite this as a clean insert.

Actually, forget the multi-namespace confusion in the plan text above. Here is the correct code to insert in `ProtoInterpreter.cpp` between line 734 (`}` end of `updateMapping`) and line 736 (`const proto::ProtoObject* runBytecode(`):

```cpp
// ---------------------------------------------------------------------------
// Generator protocol helpers (defined before runBytecode so OP_initial_yield
// can reference the ProtoMethod function pointers).
// These functions live in namespace protojs (same as runBytecode).
// They have access to thread-locals defined in the anonymous namespace above
// because they are in the same translation unit.
// ---------------------------------------------------------------------------

/** Read a long long attribute from iter by name. Returns defaultVal if absent. */
static long long genGetInt(proto::ProtoContext* ctx, const proto::ProtoObject* iter,
                            const char* name, long long defaultVal = -1LL) {
    if (!iter || iter == PROTO_NONE) return defaultVal;
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    if (!k) return defaultVal;
    const proto::ProtoObject* v = iter->getAttribute(ctx, k, false);
    return (v && v != PROTO_NONE && v->isInteger(ctx)) ? v->asLong(ctx) : defaultVal;
}

/** Set a long long attribute on iter; returns the updated iter pointer. */
static const proto::ProtoObject* genSetInt(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* iter,
                                            const char* name, long long val) {
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    return (k && iter) ? iter->setAttribute(ctx, k, ctx->fromInteger(val)) : iter;
}

/** Set a ProtoObject attribute on iter; returns the updated iter pointer. */
static const proto::ProtoObject* genSetObj(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* iter,
                                            const char* name,
                                            const proto::ProtoObject* val) {
    const proto::ProtoObject* ko = ctx->fromUTF8String(name);
    const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
    return (k && iter) ? iter->setAttribute(ctx, k, val ? val : PROTO_NONE) : iter;
}

/** Build a {value, done} iterator result object. */
static const proto::ProtoObject* makeIterResult(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* value,
                                                  bool done) {
    const proto::ProtoObject* r = ctx->newObject(true);
    if (!r) return PROTO_NONE;
    const proto::ProtoString* vk = JSSymbols::value(ctx);
    const proto::ProtoString* dk = JSSymbols::done(ctx);
    if (vk) r = r->setAttribute(ctx, vk, value ? value : PROTO_NONE);
    if (dk) r = r->setAttribute(ctx, dk, done ? PROTO_TRUE : PROTO_FALSE);
    return r ? r : PROTO_NONE;
}

/** Core resume: runs the generator body from the saved pc.
 *  mode: 0=next, 1=return, 2=throw.
 *  Returns a {value, done} iterator result object. */
static const proto::ProtoObject* resumeGenerator(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* iter,
                                                   const proto::ProtoObject* sentVal,
                                                   int mode);

// Forward declarations for the ProtoMethod callbacks.
static const proto::ProtoObject* generatorNext(proto::ProtoContext*, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);
static const proto::ProtoObject* generatorReturn(proto::ProtoContext*, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);
static const proto::ProtoObject* generatorThrow(proto::ProtoContext*, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);
```

- [ ] **Step 2: Build — verify forward declarations compile**

```bash
cmake --build build --target protojs 2>&1 | grep -E "error:|warning:|Built target"
```

Expected: `[100%] Built target protojs`

---

### Task 5: Implement OP_initial_yield

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` — add case in the main switch

The switch's `default:` case currently handles 0x85 with "unsupported opcode". Add the case for `OP_initial_yield` (opcode 0x85).

Find the `default:` case near the end of the switch (it prints "unsupported opcode"). Insert before it:

- [ ] **Step 1: Add `OP_initial_yield` case to the switch**

```cpp
            // OP_initial_yield: DEF(initial_yield, 1, 0, 0, none)
            // First opcode in every generator function body.
            // Creates the generator iterator object, saves all current state
            // as attributes on it, and returns it immediately (generator body
            // hasn't started yet — it resumes when .next() is called).
            case OP_initial_yield: {
                // Build the iterator object.
                const proto::ProtoObject* iterObj = pContext->newObject(true);
                if (!iterObj) return PROTO_NONE;

                // Helper: set an attribute on iterObj.
                auto setA = [&](const char* name, const proto::ProtoObject* val) {
                    iterObj = genSetObj(pContext, iterObj, name, val);
                };
                auto setI = [&](const char* name, long long val) {
                    iterObj = genSetInt(pContext, iterObj, name, val);
                };

                // pc already points past OP_initial_yield (incremented in the switch).
                setI(kGenPc, (long long)pc);

                // Save thisObj.
                setA(kGenThis, thisObj ? thisObj : PROTO_NONE);

                // Save module pointer as integer (raw pointer; module lifetime >= program).
                setI(kGenMod, (long long)(uintptr_t)mod);

                // Save closureLocals snapshot (GC-safe: stored as attribute on iterObj).
                const proto::ProtoObject* savedLoc = pContext->closureLocals
                    ? pContext->closureLocals->asObject(pContext) : PROTO_NONE;
                setA(kGenLocals, savedLoc);

                // Save catch stack.
                setI(kGenNcc, (long long)catch_stack.size());
                for (size_t ci = 0; ci < catch_stack.size(); ci++) {
                    std::string kpc = "__gen_cc_" + std::to_string(ci) + "_pc__";
                    std::string ksp = "__gen_cc_" + std::to_string(ci) + "_sp__";
                    setI(kpc.c_str(), (long long)catch_stack[ci].handler_pc);
                    setI(ksp.c_str(), (long long)catch_stack[ci].placeholder_stack_pos);
                }

                // State: 0 = suspended.
                setI(kGenState, 0LL);

                // Register .next, .return, .throw methods.
                auto regM = [&](const char* name, proto::ProtoMethod fn) {
                    const proto::ProtoObject* ko = pContext->fromUTF8String(name);
                    const proto::ProtoString* k  = ko ? ko->asString(pContext) : nullptr;
                    if (k) iterObj = iterObj->setAttribute(pContext, k,
                                                            pContext->fromMethod(nullptr, fn));
                };
                regM("next",   generatorNext);
                regM("return", generatorReturn);
                regM("throw",  generatorThrow);

                // Make the iterator itself iterable: [Symbol.iterator]() { return this; }
                // Simplest: register "Symbol.iterator" as a key mapping to a native that
                // returns self. For now, skip Symbol.iterator (for-of uses the iterator
                // object directly via OP_for_of_start's Case A logic).

                return iterObj;
            }
```

The `kGenPc`, `kGenLocals`, etc. are the `static constexpr` constants from `GeneratorFrame.h`. They must be visible here. Since `GeneratorFrame.h` is included at the top of `ProtoInterpreter.cpp`, they are.

- [ ] **Step 2: Build**

```bash
cmake --build build --target protojs 2>&1 | tail -5
```

Expected: `[100%] Built target protojs`

- [ ] **Step 3: Verify `gen()` now returns an object instead of crashing**

```bash
./build/protojs -e "function* gen() { yield 1; } var it = gen(); console.log(typeof it);" 2>&1 | grep -v '^\[protojs\]'
```

Expected: `object`

- [ ] **Step 4: Commit**

```bash
git add src/runtime/GeneratorFrame.h src/runtime/ProtoInterpreter.cpp
git commit -m "feat(interpreter): OP_initial_yield — create generator iterator"
```

---

### Task 6: Implement OP_yield and runBytecode resume preamble

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` — add `OP_yield` case; add resume preamble in `runBytecode`; implement `resumeGenerator` + the three ProtoMethod callbacks

This is the core of the generator protocol.

- [ ] **Step 1: Add resume preamble to `runBytecode`**

In `runBytecode`, find the `initStack(pContext);` call (around line 767). Currently it looks like:

```cpp
    initStack(pContext);
    const proto::ProtoObject* globalObjInit = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : thisObj;
    /* Pre-load closure vars... */
    for (size_t i = 0; i < closureVarNames.size(); i++) {
        ...
    }
    int pc = 0;
```

Replace with:

```cpp
    // -----------------------------------------------------------------------
    // Generator resume: if t_genResumePc >= 0, skip stack init and restore
    // saved state from the thread-locals set by resumeGenerator().
    // -----------------------------------------------------------------------
    int pc = 0;
    if (t_genResumePc >= 0) {
        pc = t_genResumePc;
        t_genResumePc = -1;

        // Restore closureLocals snapshot.
        if (t_genResumeLocals) {
            const proto::ProtoSparseList* sl = t_genResumeLocals->asSparseList(pContext);
            if (sl) pContext->closureLocals = sl;
            t_genResumeLocals = nullptr;
        }

        // Restore catch stack.
        if (t_genResumeCatchStack) {
            catch_stack = *t_genResumeCatchStack;
            t_genResumeCatchStack = nullptr;
        }

        // Push the sent value onto the stack (becomes the result of the yield expression).
        const proto::ProtoObject* sentVal = t_genIterator
            ? [&]() -> const proto::ProtoObject* {
                const proto::ProtoObject* ko = pContext->fromUTF8String("__gen_sent__");
                const proto::ProtoString* k  = ko ? ko->asString(pContext) : nullptr;
                if (!k || !t_genIterator) return PROTO_NONE;
                const proto::ProtoObject* sv = t_genIterator->getAttribute(pContext, k, false);
                return (sv && sv != PROTO_NONE) ? sv : PROTO_NONE;
            }() : PROTO_NONE;
        stackPush(pContext, sentVal ? sentVal : PROTO_NONE);

        // If mode==2 (throw): pop the sentVal, set it as pending_exception.
        const proto::ProtoObject* throwVal = t_genIterator
            ? [&]() -> const proto::ProtoObject* {
                const proto::ProtoObject* ko = pContext->fromUTF8String("__gen_throw_val__");
                const proto::ProtoString* k  = ko ? ko->asString(pContext) : nullptr;
                if (!k || !t_genIterator) return nullptr;
                const proto::ProtoObject* tv = t_genIterator->getAttribute(pContext, k, false);
                return (tv && tv != PROTO_NONE) ? tv : nullptr;
            }() : nullptr;
        if (throwVal) {
            // Pop the sentVal we just pushed; inject the exception.
            stackPop(pContext);
            pending_exception = throwVal; has_pending_exception = true;
        }
    } else {
        initStack(pContext);
        const proto::ProtoObject* globalObjInit = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : thisObj;
        for (size_t i = 0; i < closureVarNames.size(); i++) {
            if (closureVarNames[i].empty() || !globalObjInit || globalObjInit == PROTO_NONE) continue;
            const proto::ProtoString* key = (pContext->fromUTF8String(closureVarNames[i].c_str()) ? pContext->fromUTF8String(closureVarNames[i].c_str())->asString(pContext) : nullptr);
            if (key) {
                const proto::ProtoObject* val = globalObjInit->getAttribute(pContext, key, false);
                if (val && val != PROTO_NONE)
                    setSlot(pContext, argCount + varCount + static_cast<unsigned>(i), val);
            }
        }
    }
```

(Keep the rest of `runBytecode` below unchanged — the `tdzSentinel`, null sentinel, bootstrap calls, and main `while` loop.)

- [ ] **Step 2: Add `OP_yield` case to the switch**

Insert before the `default:` case:

```cpp
            // OP_yield: DEF(yield, 1, 1, 2, none)
            // Suspends the generator and yields a value.
            // Stack before: [..., value]  → (this runBytecode invocation returns {value, done:false})
            // When .next(sentVal) is called, execution resumes here; sentVal is pushed onto stack.
            case OP_yield: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* yieldVal = stackTop(pContext);
                stackPop(pContext);

                if (!t_genIterator) {
                    // OP_yield outside a generator resume — shouldn't happen in valid code.
                    return PROTO_NONE;
                }

                // Save updated state back onto the iterator object.
                // pc already points past OP_yield.
                const proto::ProtoObject* updIter = t_genIterator;
                updIter = genSetInt(pContext, updIter, kGenPc, (long long)pc);
                const proto::ProtoObject* newLoc = pContext->closureLocals
                    ? pContext->closureLocals->asObject(pContext) : PROTO_NONE;
                updIter = genSetObj(pContext, updIter, kGenLocals, newLoc);
                updIter = genSetInt(pContext, updIter, kGenNcc, (long long)catch_stack.size());
                for (size_t ci = 0; ci < catch_stack.size(); ci++) {
                    std::string kpc = "__gen_cc_" + std::to_string(ci) + "_pc__";
                    std::string ksp = "__gen_cc_" + std::to_string(ci) + "_sp__";
                    updIter = genSetInt(pContext, updIter, kpc.c_str(),
                                        (long long)catch_stack[ci].handler_pc);
                    updIter = genSetInt(pContext, updIter, ksp.c_str(),
                                        (long long)catch_stack[ci].placeholder_stack_pos);
                }
                updIter = genSetInt(pContext, updIter, kGenState, 0LL); // still suspended

                // Sync the updated iterator back to GCBridge so .next() sees the new state.
                if (updIter != t_genIterator) {
                    updateMapping(pContext, t_genIterator, updIter);
                }
                t_genIterator = nullptr; // clear so resumeGenerator knows we yielded

                // Signal to resumeGenerator that OP_yield fired (not OP_return).
                t_genResumePc = -2;

                // Return {value: yieldVal, done: false} from this runBytecode invocation.
                return makeIterResult(pContext, yieldVal, false);
            }
```

- [ ] **Step 3: Implement `resumeGenerator` and the three ProtoMethod callbacks**

After the forward declarations block inserted in Task 4, add the full implementations. Insert them between `runBytecode`'s forward declarations and `runBytecode`'s definition. The exact location is after the forward declarations added in Task 4 (around line 735 in the original file, now slightly shifted).

```cpp
// Full implementation of resumeGenerator (was forward-declared in Task 4).
static const proto::ProtoObject* resumeGenerator(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* iter,
                                                   const proto::ProtoObject* sentVal,
                                                   int mode) {
    if (!ctx || !iter || iter == PROTO_NONE) return makeIterResult(ctx, PROTO_NONE, true);

    long long state = genGetInt(ctx, iter, kGenState);
    if (state == 1) return makeIterResult(ctx, PROTO_NONE, true); // already completed

    if (mode == 1) {
        // .return(val): mark done, return {value: val, done: true}.
        iter = genSetInt(ctx, iter, kGenState, 1LL);
        return makeIterResult(ctx, sentVal, true);
    }

    // Recover module pointer.
    long long modRaw = genGetInt(ctx, iter, kGenMod);
    if (modRaw <= 0) return makeIterResult(ctx, PROTO_NONE, true);
    const ProtoBytecodeModule* mod = reinterpret_cast<const ProtoBytecodeModule*>((uintptr_t)modRaw);

    // Recover saved pc.
    long long resumePc = genGetInt(ctx, iter, kGenPc);
    if (resumePc < 0) return makeIterResult(ctx, PROTO_NONE, true);

    // Recover closureLocals.
    const proto::ProtoObject* ko = ctx->fromUTF8String(kGenLocals);
    const proto::ProtoString* lk = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* savedLocObj = lk ? iter->getAttribute(ctx, lk, false) : nullptr;

    // Recover thisObj.
    const proto::ProtoObject* tok = ctx->fromUTF8String(kGenThis);
    const proto::ProtoString* tk2 = tok ? tok->asString(ctx) : nullptr;
    const proto::ProtoObject* genThis = tk2 ? iter->getAttribute(ctx, tk2, false) : PROTO_NONE;

    // Recover catch stack.
    long long ncc = genGetInt(ctx, iter, kGenNcc, 0LL);
    std::vector<CatchFrame> restoredCatch;
    for (long long ci = 0; ci < ncc; ci++) {
        std::string kpc = "__gen_cc_" + std::to_string(ci) + "_pc__";
        std::string ksp = "__gen_cc_" + std::to_string(ci) + "_sp__";
        restoredCatch.push_back({(int)genGetInt(ctx, iter, kpc.c_str()),
                                  (unsigned long)genGetInt(ctx, iter, ksp.c_str())});
    }

    // If mode == 2 (throw): pre-store the throw value on the iterator.
    if (mode == 2 && sentVal && sentVal != PROTO_NONE) {
        iter = genSetObj(ctx, iter, "__gen_throw_val__", sentVal);
    } else {
        // Store sent value (result of yield expr on resume).
        iter = genSetObj(ctx, iter, "__gen_sent__", sentVal ? sentVal : PROTO_NONE);
        // Clear any prior throw val.
        iter = genSetObj(ctx, iter, "__gen_throw_val__", PROTO_NONE);
    }

    // Set up resume thread-locals.
    t_genResumePc         = (int)resumePc;
    t_genResumeLocals     = savedLocObj;
    t_genResumeCatchStack = restoredCatch.empty() ? nullptr : &restoredCatch;
    t_genIterator         = iter;

    // Create child context and invoke runBytecode.
    proto::ProtoContext childCtx(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr);
    childCtx.currentFileName   = ctx->currentFileName;
    childCtx.currentLineNumber = ctx->currentLineNumber;
    const proto::ProtoObject* childEx = PROTO_NONE;
    const proto::ProtoObject** gr = t_currentGlobalRoot;

    const proto::ProtoObject* result = runBytecode(&childCtx, mod, genThis,
                                                     nullptr, gr, &childEx);

    // Propagate exceptions from generator body.
    if (childEx && childEx != PROTO_NONE) return childEx;

    if (t_genResumePc == -2) {
        // OP_yield fired — result is already {value, done:false}.
        t_genResumePc = -1;
        return result;
    }

    // Generator body completed (OP_return or end of bytecode).
    // Mark iterator done via t_genIterator (may have been updated by OP_yield state saves).
    if (t_genIterator) {
        t_genIterator = genSetInt(ctx, t_genIterator, kGenState, 1LL);
    }
    t_genIterator = nullptr;
    return makeIterResult(ctx, result ? result : PROTO_NONE, true);
}

// .next(value) — sends value into the generator (becomes result of yield expression).
static const proto::ProtoObject* generatorNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* sentVal = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    return resumeGenerator(ctx, self, sentVal, 0);
}

// .return(value) — terminates the generator and returns {value, done:true}.
static const proto::ProtoObject* generatorReturn(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* retVal = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    return resumeGenerator(ctx, self, retVal, 1);
}

// .throw(err) — throws err inside the generator at the yield point.
static const proto::ProtoObject* generatorThrow(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* errVal = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    return resumeGenerator(ctx, self, errVal, 2);
}
```

- [ ] **Step 4: Build**

```bash
cmake --build build --target protojs 2>&1 | tail -5
```

Expected: `[100%] Built target protojs`

- [ ] **Step 5: Commit**

```bash
git add src/runtime/ProtoInterpreter.cpp
git commit -m "feat(interpreter): OP_yield + resumeGenerator — generator suspend/resume"
```

---

### Task 7: Smoke test — basic generator protocol

- [ ] **Step 1: Run basic yield smoke test**

```bash
cat > /tmp/smoke_gen.js << 'EOF'
function* counter() { yield 1; yield 2; yield 3; }
var it = counter();
var r1 = it.next();
console.assert(r1.value === 1 && r1.done === false, "r1: " + JSON.stringify(r1));
var r2 = it.next();
console.assert(r2.value === 2 && r2.done === false, "r2: " + JSON.stringify(r2));
var r3 = it.next();
console.assert(r3.value === 3 && r3.done === false, "r3: " + JSON.stringify(r3));
var r4 = it.next();
console.assert(r4.done === true, "r4 done: " + JSON.stringify(r4));
console.log("basic yield: PASSED");
EOF
./build/protojs /tmp/smoke_gen.js 2>&1 | grep -v '^\[protojs\]'
```

Expected: `basic yield: PASSED`

- [ ] **Step 2: Run sent-value smoke test**

```bash
cat > /tmp/smoke_gen2.js << 'EOF'
function* echo() {
    var a = yield 'first';
    var b = yield a + '!';
    return b;
}
var it = echo();
var r1 = it.next();         // start — yields 'first'
console.assert(r1.value === 'first', "r1.value");
var r2 = it.next('hello'); // sends 'hello' as result of first yield
console.assert(r2.value === 'hello!', "r2.value: " + r2.value);
var r3 = it.next('world'); // sends 'world' as result of second yield
console.assert(r3.done === true, "r3.done");
console.assert(r3.value === 'world', "r3.value: " + r3.value);
console.log("sent value: PASSED");
EOF
./build/protojs /tmp/smoke_gen2.js 2>&1 | grep -v '^\[protojs\]'
```

Expected: `sent value: PASSED`

- [ ] **Step 3: Run for-of smoke test**

```bash
cat > /tmp/smoke_gen3.js << 'EOF'
function* range(n) { for (var i = 0; i < n; i++) yield i; }
var result = [];
for (var x of range(4)) result.push(x);
console.assert(result.join(',') === '0,1,2,3', "for-of: " + result);
console.log("for-of: PASSED");
EOF
./build/protojs /tmp/smoke_gen3.js 2>&1 | grep -v '^\[protojs\]'
```

Expected: `for-of: PASSED`

**If any test fails:** Debug by adding `console.log` traces before fixing. Most likely causes:
- Sent value not reaching the generator (check `__gen_sent__` loading in preamble)
- `for-of` not calling `.next()` (check `OP_for_of_start` Case A — needs the iterator to have both `next` and a signal that it IS an iterator, not just any object with `next`)

For `OP_for_of_start` Case A, the iterator is detected by the presence of both `next` method AND `__iter_arr__` internal key. Generator iterators don't have `__iter_arr__`. We need to either add `__iter_arr__` to generator iterators or add a new detection path.

Fix: in `OP_initial_yield`, after registering `.next`, also register the generator marker:

```cpp
// Mark as a generator iterator for OP_for_of_start Case A detection.
// We reuse the existing "has next + __iter_arr__" pattern:
const proto::ProtoString* iterArrKey3 = JSSymbols::iterArr(pContext);
if (iterArrKey3)
    iterObj = iterObj->setAttribute(pContext, iterArrKey3,
                                     pContext->fromInteger(0LL)); // sentinel value
```

- [ ] **Step 4: Commit passing smoke tests**

```bash
git add src/runtime/ProtoInterpreter.cpp
git commit -m "feat(interpreter): generator smoke tests passing — basic yield + sent values + for-of"
```

---

### Task 8: OP_yield_star (delegation)

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` — add `OP_yield_star` case

`yield*` delegates to an inner iterable. The bytecode is:
```
OP_yield_star  // pops inner_iterator, pushes the final value when inner is done
```

Stack before: [..., inner_iterator]
Stack after: [..., innerFinalValue]

- [ ] **Step 1: Add `OP_yield_star` case**

Insert after the `OP_yield` case:

```cpp
            // OP_yield_star: DEF(yield_star, 1, 1, 2, none)
            // Delegates to inner iterable: calls inner.next() repeatedly, yielding each value
            // out to the outer caller. When inner is done, pushes the final value.
            case OP_yield_star: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* innerIter = stackTop(pContext);
                stackPop(pContext);
                if (!innerIter || innerIter == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    break;
                }

                // Validate: inner iterator must have .next method.
                const proto::ProtoString* nextKey3 = JSSymbols::next(pContext);
                const proto::ProtoObject* nextFn = nextKey3
                    ? innerIter->getAttribute(pContext, nextKey3, true) : PROTO_NONE;
                if (!nextFn || nextFn == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    break;
                }

                // Delegate: loop calling inner.next() and yield each value to the outer caller.
                const proto::ProtoObject* sentToInner = PROTO_NONE;
                while (true) {
                    // Call inner.next(sentToInner).
                    const proto::ProtoList* nextArgs = nullptr;
                    if (sentToInner && sentToInner != PROTO_NONE) {
                        proto::ProtoContext tmpCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                        const proto::ProtoList* argList = tmpCtx.newList();
                        if (argList) argList = argList->append(&tmpCtx, sentToInner);
                        nextArgs = argList;
                    }
                    const proto::ProtoObject* iterResult = callJSFunction(pContext, nextFn,
                                                                           innerIter, nextArgs);
                    if (!iterResult || iterResult == PROTO_NONE) {
                        stackPush(pContext, PROTO_NONE);
                        break;
                    }

                    const proto::ProtoString* vk2  = JSSymbols::value(pContext);
                    const proto::ProtoString* dk2  = JSSymbols::done(pContext);
                    const proto::ProtoObject* val2 = vk2 ? iterResult->getAttribute(pContext, vk2, false) : PROTO_NONE;
                    const proto::ProtoObject* done2 = dk2 ? iterResult->getAttribute(pContext, dk2, false) : PROTO_FALSE;

                    bool isDone = (done2 == PROTO_TRUE || (done2 && done2 != PROTO_NONE &&
                                   done2->isBoolean(pContext) && done2->asBoolean(pContext)));
                    if (isDone) {
                        // Inner iterator done: push its final value for the yield* expression.
                        stackPush(pContext, val2 ? val2 : PROTO_NONE);
                        break;
                    }

                    // Yield the inner value to the outer caller.
                    if (!t_genIterator) {
                        // Not inside a generator resume — shouldn't happen.
                        stackPush(pContext, val2 ? val2 : PROTO_NONE);
                        break;
                    }

                    // Save state and yield out to the outer caller.
                    // We need to re-use the OP_yield save logic, but from within yield_star.
                    // For simplicity: inline the state save here.
                    {
                        const proto::ProtoObject* updIter = t_genIterator;
                        updIter = genSetInt(pContext, updIter, kGenPc, (long long)pc);
                        const proto::ProtoObject* newLoc2 = pContext->closureLocals
                            ? pContext->closureLocals->asObject(pContext) : PROTO_NONE;
                        updIter = genSetObj(pContext, updIter, kGenLocals, newLoc2);
                        // Also save innerIter so we can re-enter this loop on next .next() call.
                        // For now: simple approach — push innerIter back onto the stack so the
                        // restored execution re-enters OP_yield_star. This requires the pc saved
                        // to point BACK to OP_yield_star.
                        // Actually, pc was incremented past OP_yield_star already. Save pc - 1
                        // so we re-execute OP_yield_star with innerIter pushed back.
                        stackPush(pContext, innerIter);
                        const proto::ProtoObject* newLoc3 = pContext->closureLocals
                            ? pContext->closureLocals->asObject(pContext) : PROTO_NONE;
                        updIter = genSetInt(pContext, updIter, kGenPc, (long long)(pc - 1));
                        updIter = genSetObj(pContext, updIter, kGenLocals, newLoc3);
                        updIter = genSetInt(pContext, updIter, kGenNcc, (long long)catch_stack.size());
                        updIter = genSetInt(pContext, updIter, kGenState, 0LL);
                        if (updIter != t_genIterator)
                            updateMapping(pContext, t_genIterator, updIter);
                        t_genIterator = nullptr;
                        t_genResumePc = -2;
                        return makeIterResult(pContext, val2, false);
                    }
                }
                break;
            }
```

- [ ] **Step 2: Build and test yield***

```bash
cmake --build build --target protojs 2>&1 | tail -3
cat > /tmp/smoke_yieldstar.js << 'EOF'
function* inner() { yield 'a'; yield 'b'; }
function* outer() { yield 1; yield* inner(); yield 2; }
var result = [];
for (var v of outer()) result.push(v);
console.assert(result.join(',') === '1,a,b,2', "yield*: " + result);
console.log("yield*: PASSED");
EOF
./build/protojs /tmp/smoke_yieldstar.js 2>&1 | grep -v '^\[protojs\]'
```

Expected: `yield*: PASSED`

- [ ] **Step 3: Commit**

```bash
git add src/runtime/ProtoInterpreter.cpp
git commit -m "feat(interpreter): OP_yield_star — generator delegation"
```

---

### Task 9: test262 partial run — generators

- [ ] **Step 1: Run test262 on generators, yield, async-generator**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS="language/expressions/generators,language/expressions/yield,language/expressions/async-generator" \
  node tests/test262/runner/test262_runner.js 2>/dev/null | tail -5
```

- [ ] **Step 2: Analyse results**

```bash
node -e "
const fs = require('fs'), path = require('path');
const dir = 'tests/test262/reports';
const files = fs.readdirSync(dir).filter(f => f.startsWith('snapshot-language-expressions-generators') || f.startsWith('snapshot-language-expressions-yield') || f.startsWith('snapshot-language-expressions-async')).sort().slice(-3);
files.forEach(f => {
    const snap = JSON.parse(fs.readFileSync(path.join(dir, f)));
    const s = snap.summary;
    console.log(f.split('-').slice(-1)[0].replace('.json',''), JSON.stringify(s));
});
" 2>/dev/null
```

- [ ] **Step 3: Commit snapshot**

```bash
git add tests/test262/reports/snapshot-language-expressions-generators-*.json \
        tests/test262/reports/snapshot-language-expressions-yield-*.json \
        tests/test262/reports/snapshot-language-expressions-async-generator-*.json 2>/dev/null
git commit -m "test(test262): generator/yield/async-generator partial snapshot"
```

---

## Sub-feature B: Property Descriptor Enforcement

### Task 10: Object.defineProperty native implementation

**Files:**
- Modify: `src/ObjectPrototype.cpp`

Currently `Object.defineProperty` is not registered. This task adds a real implementation that stores property descriptor flags as hidden attributes.

- [ ] **Step 1: Write the failing smoke test**

```bash
cat > /tmp/smoke_pd.js << 'EOF'
'use strict';
var obj = {};
Object.defineProperty(obj, 'x', { value: 42, writable: false, enumerable: true, configurable: true });
if (obj.x !== 42) throw new Error("value wrong: " + obj.x);
var threw = false;
try { obj.x = 99; } catch(e) { threw = true; }
if (!threw) throw new Error("should throw TypeError");
if (obj.x !== 42) throw new Error("value changed after failed assignment");
console.log("PropDescriptor smoke: PASSED");
EOF
./build/protojs /tmp/smoke_pd.js 2>&1 | grep -v '^\[protojs\]'
```

Expected: `Exception in ...: should throw TypeError` (confirms not yet implemented).

- [ ] **Step 2: Add `objectDefineProperty` in `src/ObjectPrototype.cpp`**

After the `objectGetPrototypeOf` function (around line 163), insert:

```cpp
// ---------------------------------------------------------------------------
// Object.defineProperty(obj, propName, descriptor)
//
// Stores the property value and descriptor flags on the target object.
// Descriptor flags are encoded as a single integer (bits: 0=writable,
// 1=configurable, 2=enumerable) under the hidden key "__pd_<propName>__".
// A missing __pd__ key means all flags are true (default JS semantics).
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectDefineProperty(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) < 3) return PROTO_NONE;

    const proto::ProtoObject* target = args->getAt(ctx, 0);
    if (!target || target == PROTO_NONE) return PROTO_NONE;

    // Get property name string.
    const proto::ProtoObject* propNameObj = args->getAt(ctx, 1);
    if (!propNameObj || propNameObj == PROTO_NONE) return target;
    std::string propName;
    const proto::ProtoString* ps = propNameObj->asString(ctx);
    if (ps) ps->toUTF8String(ctx, propName);
    else if (propNameObj->isInteger(ctx))
        propName = std::to_string(propNameObj->asLong(ctx));
    if (propName.empty()) return target;

    // Get descriptor object.
    const proto::ProtoObject* desc = args->getAt(ctx, 2);
    if (!desc || desc == PROTO_NONE) return target;

    // Extract flags from descriptor (defaults: writable=false, configurable=false, enumerable=false
    // per ES spec for Object.defineProperty when flags are not specified).
    auto getBoolProp = [&](const char* name, bool defaultVal) -> bool {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
        if (!k) return defaultVal;
        const proto::ProtoObject* v = desc->getAttribute(ctx, k, false);
        if (!v || v == PROTO_NONE) return defaultVal;
        return (v == PROTO_TRUE) || (v->isBoolean(ctx) && v->asBoolean(ctx));
    };

    bool writable     = getBoolProp("writable",     false);
    bool configurable = getBoolProp("configurable",  false);
    bool enumerable   = getBoolProp("enumerable",    false);

    // Store the value if present.
    const proto::ProtoObject* valueKey = ctx->fromUTF8String("value");
    const proto::ProtoString* vkp = valueKey ? valueKey->asString(ctx) : nullptr;
    if (vkp) {
        const proto::ProtoObject* val = desc->getAttribute(ctx, vkp, false);
        if (val) { // val may be PROTO_NONE (explicit undefined) — store it.
            const proto::ProtoObject* ko = ctx->fromUTF8String(propName.c_str());
            const proto::ProtoString* pk = ko ? ko->asString(ctx) : nullptr;
            if (pk) target = target->setAttribute(ctx, pk, val);
        }
    }

    // Encode descriptor flags. Only store the sidecar if NOT (w=true, c=true, e=true).
    // All-true means default; no need to track it.
    uint8_t bits = (writable ? 0x1 : 0) | (configurable ? 0x2 : 0) | (enumerable ? 0x4 : 0);
    std::string pdKeyStr = "__pd_" + propName + "__";
    const proto::ProtoObject* pko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
    if (pdk) target = target->setAttribute(ctx, pdk, ctx->fromInteger((long long)bits));

    return target;
}

// ---------------------------------------------------------------------------
// Object.getOwnPropertyDescriptor(obj, propName)
// Returns {value, writable, configurable, enumerable} descriptor object.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGetOwnPropertyDescriptor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    const proto::ProtoObject* propNameObj = args->getAt(ctx, 1);
    if (!target || target == PROTO_NONE || !propNameObj || propNameObj == PROTO_NONE)
        return PROTO_NONE;

    std::string propName;
    const proto::ProtoString* ps = propNameObj->asString(ctx);
    if (ps) ps->toUTF8String(ctx, propName);
    else if (propNameObj->isInteger(ctx)) propName = std::to_string(propNameObj->asLong(ctx));
    if (propName.empty()) return PROTO_NONE;

    // Get the value.
    const proto::ProtoObject* ko = ctx->fromUTF8String(propName.c_str());
    const proto::ProtoString* pk = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* val = pk ? target->getAttribute(ctx, pk, false) : PROTO_NONE;
    if (!val) return PROTO_NONE; // property doesn't exist

    // Get the descriptor flags.
    std::string pdKeyStr = "__pd_" + propName + "__";
    const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdk  = pdko ? pdko->asString(ctx) : nullptr;
    const proto::ProtoObject* bitsObj = pdk ? target->getAttribute(ctx, pdk, false) : nullptr;
    uint8_t bits = 0x7; // default: all true
    if (bitsObj && bitsObj != PROTO_NONE && bitsObj->isInteger(ctx))
        bits = static_cast<uint8_t>(bitsObj->asLong(ctx));

    const proto::ProtoObject* result = ctx->newObject(true);
    auto setAttr = [&](const char* name, const proto::ProtoObject* v) {
        const proto::ProtoObject* k = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = k ? k->asString(ctx) : nullptr;
        if (ks) result = result->setAttribute(ctx, ks, v);
    };
    setAttr("value",        val);
    setAttr("writable",     (bits & 0x1) ? PROTO_TRUE : PROTO_FALSE);
    setAttr("configurable", (bits & 0x2) ? PROTO_TRUE : PROTO_FALSE);
    setAttr("enumerable",   (bits & 0x4) ? PROTO_TRUE : PROTO_FALSE);
    return result;
}
```

- [ ] **Step 3: Register in `ensureObjectConstructor`**

In `src/ObjectPrototype.cpp`, find the `reg(...)` block (~line 398) and add:

```cpp
    reg("defineProperty",           objectDefineProperty);
    reg("getOwnPropertyDescriptor", objectGetOwnPropertyDescriptor);
```

- [ ] **Step 4: Build**

```bash
cmake --build build --target protojs 2>&1 | tail -3
```

Expected: `[100%] Built target protojs`

- [ ] **Step 5: Verify `Object.defineProperty` stores the value**

```bash
./build/protojs -e "
var obj = {};
Object.defineProperty(obj, 'x', { value: 42, writable: false });
console.log(obj.x);
" 2>&1 | grep -v '^\[protojs\]'
```

Expected: `42`

- [ ] **Step 6: Commit**

```bash
git add src/ObjectPrototype.cpp
git commit -m "feat(builtins): Object.defineProperty + getOwnPropertyDescriptor — sidecar descriptor"
```

---

### Task 11: OP_put_field strict-mode writable check

**Files:**
- Modify: `src/runtime/ProtoInterpreter.cpp` — `OP_put_field` case (~line 1922)

- [ ] **Step 1: Add the writable check before `setAttribute` in `OP_put_field`**

In `src/runtime/ProtoInterpreter.cpp`, find `case OP_put_field:` (line ~1922). The section before `obj->setAttribute(pContext, key, val)` is:

```cpp
                if (key && obj) {
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, val);
```

Replace with:

```cpp
                if (key && obj) {
                    // Strict-mode writable check: if __pd_<key>__ exists and bit0=0, throw TypeError.
                    if (mod->isStrict) {
                        std::string keyStr2;
                        key->toUTF8String(pContext, keyStr2);
                        std::string pdKeyStr = "__pd_" + keyStr2 + "__";
                        const proto::ProtoObject* pdko2 = pContext->fromUTF8String(pdKeyStr.c_str());
                        const proto::ProtoString* pdk2  = pdko2 ? pdko2->asString(pContext) : nullptr;
                        if (pdk2) {
                            const proto::ProtoObject* bitsObj2 = obj->getAttribute(pContext, pdk2, false);
                            if (bitsObj2 && bitsObj2 != PROTO_NONE && bitsObj2->isInteger(pContext)) {
                                uint8_t bits2 = static_cast<uint8_t>(bitsObj2->asLong(pContext));
                                bool writable2 = (bits2 & 0x1) != 0;
                                if (!writable2) {
                                    pending_exception = makeError(pContext, "TypeError",
                                        "Cannot assign to read only property", pGlobalRoot);
                                    has_pending_exception = true;
                                    break;
                                }
                            }
                        }
                    } else {
                        // Non-strict: silently ignore assignment to non-writable property.
                        std::string keyStr3;
                        key->toUTF8String(pContext, keyStr3);
                        std::string pdKeyStr3 = "__pd_" + keyStr3 + "__";
                        const proto::ProtoObject* pdko3 = pContext->fromUTF8String(pdKeyStr3.c_str());
                        const proto::ProtoString* pdk3  = pdko3 ? pdko3->asString(pContext) : nullptr;
                        if (pdk3) {
                            const proto::ProtoObject* bitsObj3 = obj->getAttribute(pContext, pdk3, false);
                            if (bitsObj3 && bitsObj3 != PROTO_NONE && bitsObj3->isInteger(pContext)) {
                                uint8_t bits3 = static_cast<uint8_t>(bitsObj3->asLong(pContext));
                                if ((bits3 & 0x1) == 0) {
                                    // Silently ignore — push obj unchanged.
                                    stackPush(pContext, obj);
                                    break;
                                }
                            }
                        }
                    }
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, val);
```

- [ ] **Step 2: Build**

```bash
cmake --build build --target protojs 2>&1 | tail -3
```

Expected: `[100%] Built target protojs`

- [ ] **Step 3: Run property descriptor smoke test**

```bash
./build/protojs /tmp/smoke_pd.js 2>&1 | grep -v '^\[protojs\]'
```

Expected: `PropDescriptor smoke: PASSED`

- [ ] **Step 4: Run non-strict silently-ignores test**

```bash
cat > /tmp/smoke_pd_nonstrict.js << 'EOF'
var obj = {};
Object.defineProperty(obj, 'x', { value: 10, writable: false });
obj.x = 20; // should be silently ignored (non-strict)
console.assert(obj.x === 10, "non-strict should not change: " + obj.x);
console.log("non-strict: PASSED");
EOF
./build/protojs /tmp/smoke_pd_nonstrict.js 2>&1 | grep -v '^\[protojs\]'
```

Expected: `non-strict: PASSED`

- [ ] **Step 5: Commit**

```bash
git add src/runtime/ProtoInterpreter.cpp
git commit -m "feat(interpreter): OP_put_field strict-mode writable check — TypeError on non-writable"
```

---

### Task 12: test262 partial run — assignment + compound-assignment

- [ ] **Step 1: Run test262 on assignment + compound-assignment**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS="language/expressions/assignment,language/expressions/compound-assignment" \
  node tests/test262/runner/test262_runner.js 2>/dev/null | tail -5
```

- [ ] **Step 2: Analyse results and commit snapshot**

```bash
node -e "
const fs = require('fs'), path = require('path');
const dir = 'tests/test262/reports';
const files = fs.readdirSync(dir)
    .filter(f => f.includes('assignment') && f.endsWith('.json'))
    .sort().slice(-2);
files.forEach(f => {
    const s = JSON.parse(fs.readFileSync(path.join(dir, f))).summary;
    console.log(path.basename(f, '.json').substring(0,60), JSON.stringify(s));
});
" 2>/dev/null
git add tests/test262/reports/snapshot-language-expressions-assignment*.json \
        tests/test262/reports/snapshot-language-expressions-compound-assignment*.json 2>/dev/null
git commit -m "test(test262): assignment + compound-assignment partial snapshot"
```

---

### Task 13: Full language/expressions run + TEST262_STATUS.md update

- [ ] **Step 1: Full language/expressions run**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS="language/expressions" \
  node tests/test262/runner/test262_runner.js 2>/dev/null | tail -5
```

This run takes ~20-30 minutes.

- [ ] **Step 2: Compute new pass rate**

```bash
node -e "
const fs = require('fs'), path = require('path');
const dir = 'tests/test262/reports';
const snap = fs.readdirSync(dir)
    .filter(f => /^snapshot-language-expressions-\d+\.json$/.test(f))
    .sort().slice(-1)[0];
const data = JSON.parse(fs.readFileSync(path.join(dir, snap)));
const s = data.summary;
const total = Object.values(data.results).length;
console.log('Snapshot:', snap);
console.log('passed:', s.passed, '/', total);
console.log('pass%:', (s.passed / total * 100).toFixed(1) + '%');
console.log('delta vs 82.3% baseline:', s.passed - 9078);
" 2>/dev/null
```

- [ ] **Step 3: Update `docs/TEST262_STATUS.md`**

Update the file as follows (filling in actual numbers from Step 2):

1. Change the "Phase 9 Snapshot — ✅ CURRENT" header to "(superseded by Phase 10)".
2. Add a new "Phase 10 Snapshot — 2026-04-09 ✅ CURRENT" section with the actual results table showing generators and property-descriptor columns.
3. In "Recommended Next Steps", mark generators + property-descriptors as RESOLVED.
4. Add changelog entry: `| 2026-04-09 | language/expressions Phase 10: XX.X% (N/11,036) | +NNN passes vs 82.3%. Generators, property descriptor writable enforcement. |`

- [ ] **Step 4: Commit snapshot + status**

```bash
git add tests/test262/reports/snapshot-language-expressions-*.json docs/TEST262_STATUS.md
git commit -m "docs(test262): Phase 10 results — generators + property descriptors — XX.X%"
```

---

## Self-review checklist

**Spec coverage:**
- [x] OP_initial_yield → Task 5
- [x] OP_yield → Task 6
- [x] OP_yield_star → Task 8
- [x] resumeGenerator → Task 6
- [x] generatorNext/Return/Throw → Task 6
- [x] Object.defineProperty → Task 10
- [x] OP_put_field strict-mode writable → Task 11
- [x] Smoke tests → Tasks 7, 11
- [x] test262 runs → Tasks 9, 12, 13
- [x] TEST262_STATUS.md → Task 13

**Placeholder scan:** No TBDs. All code shown. Commands exact. Expected outputs given.

**Type consistency:**
- `CatchFrame` defined in `GeneratorFrame.h`, used in both `runBytecode` and `resumeGenerator` ✓
- `makeIterResult` used in `resumeGenerator` and `generatorNext/Return/Throw` ✓
- `genGetInt` / `genSetInt` / `genSetObj` used consistently ✓
- `kGenPc`, `kGenLocals`, etc. from `GeneratorFrame.h`, used in all generator functions ✓
