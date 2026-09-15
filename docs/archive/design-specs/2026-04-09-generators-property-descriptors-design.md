# Phase 10: Generator Protocol + Property Descriptor Enforcement

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the ES6 generator protocol (`function*`, `yield`, `yield*`) and enforce `Object.defineProperty` property descriptors (writable/configurable) in strict-mode assignment, recovering ~480–500 `language/expressions` test262 passes from the current 82.3% baseline.

**Architecture:** Two sequential sub-features on one branch. Sub-feature A adds a `GeneratorFrame` struct and three new opcodes to `ProtoInterpreter`. Sub-feature B registers `Object.defineProperty` as a real native method and adds a sidecar-descriptor check in `OP_put_field`.

**Tech Stack:** C++20, protoCore ProtoObject/ProtoContext, QuickJS bytecode opcodes, test262 runner.

---

## Context

Current state: `language/expressions` 82.3% (9,078 / 11,036) after Phase 9 null/undefined fix.

Remaining top failure clusters:

| Cluster | Count | Root cause |
|---------|------:|-----------|
| `generators` semantic | 224 | `OP_initial_yield` (0x85) not implemented |
| `async-generator` semantic | 139 | same — async generator flag |
| `yield` semantic | 58 | `OP_yield` (0x86) not implemented |
| `compound-assignment` semantic | 148 | `Object.defineProperty({writable:false})` not enforced |
| `assignment` semantic | 145 | same root cause |

**Target:** +480–500 passes → ~86–87% on `language/expressions`.

---

## Sub-feature A: Generator Protocol

### Key files

| Action | File |
|--------|------|
| Create | `src/runtime/GeneratorFrame.h` |
| Modify | `src/runtime/ProtoInterpreter.cpp` — `OP_initial_yield`, `OP_yield`, `OP_yield_star`, `resumeGenerator()` |
| Modify | `src/runtime/ProtoInterpreter.h` — declare `resumeGenerator()` |

### Architecture

**`GeneratorFrame` struct** (heap-allocated, owned by a `shared_ptr`):

```cpp
struct GeneratorFrame {
    enum class State { Suspended, Running, Completed };

    int                              pc;
    const ProtoBytecodeModule*       module;
    const proto::ProtoObject*        thisObj;
    const proto::ProtoObject*        closureLocals; // immutable snapshot — copy is O(1)
    std::vector<CatchFrame>          catchStack;    // CatchFrame defined in ProtoInterpreter.cpp
    State                            state = State::Suspended;
    const proto::ProtoObject**       globalRoot;    // pointer to thread-local global root
};

enum class ResumeMode { Next, Return, Throw };
```

**`OP_initial_yield` (opcode 0x85):**
1. Allocate `GeneratorFrame`, capture current `pc` (pointing past the opcode), `module`, `thisObj`, `pContext->closureLocals` snapshot, `catchStack`, `pGlobalRoot`.
2. Set `frame->state = Suspended`.
3. Wrap the `shared_ptr<GeneratorFrame>` as a sentinel ProtoObject with key `__gen_frame__`.
4. Build a generator iterator ProtoObject with:
   - `__gen_frame__` → the frame wrapper
   - `next` → native ProtoMethod calling `resumeGenerator(ctx, frame, sentValue, ResumeMode::Next)`
   - `return` → native ProtoMethod calling `resumeGenerator(ctx, frame, retValue, ResumeMode::Return)`
   - `throw` → native ProtoMethod calling `resumeGenerator(ctx, frame, errVal, ResumeMode::Throw)`
5. Return the iterator object immediately (the generator body has not run yet).

**`OP_yield` (opcode 0x86):**
1. Pop value `v` from stack.
2. Save `pc` (pointing past `OP_yield`), current `closureLocals`, `catchStack` into the active `GeneratorFrame`.
3. Set `frame->state = Suspended`.
4. Build `{value: v, done: false}` result object and return it from the current `resumeGenerator()` call.

**`resumeGenerator(ctx, frame, sentValue, mode)`:**
```
if frame->state == Completed: return {value: undefined, done: true}
if mode == Return:            frame->state = Completed; return {value: sentValue, done: true}
if mode == Throw:             restore frame state, inject sentValue as pending_exception, continue loop
// mode == Next:
frame->state = Running
restore pContext->closureLocals = frame->closureLocals
restore catchStack
push sentValue onto restored stack (the value becomes the result of the yield expression)
re-enter the interpreter loop at frame->pc
on OP_return or end-of-bytecode: frame->state = Completed; return {value: returnVal, done: true}
on next OP_yield: (handled above) return {value: v, done: false}
```

**`OP_yield_star` (opcode 0x87):**
Delegates to an inner iterable. Uses the existing `OP_iterator_next` infrastructure:
1. Pop inner iterator from stack.
2. Loop: call `iterator.next(sentValue)`; if `done: true`, push the final value and continue; if `done: false`, yield the intermediate value out to the outer caller (via `OP_yield` path).

**`%GeneratorPrototype%`:**
- Registered in `runBytecode` bootstrap (alongside `ensureArrayPrototype`, etc.).
- The iterator objects produced by `OP_initial_yield` inherit from this prototype.
- Prototype carries `next`, `return`, `throw` so that prototype-chain tests pass.

**Async generators:**
- `module->isAsync` flag (already in `ProtoBytecodeModule`) distinguishes async generators.
- `OP_initial_yield` for async generators returns an `AsyncGenerator` object whose `.next()` returns a `Promise<{value, done}>`.
- Implement after sync generators are passing; share the same `GeneratorFrame` struct.

### Thread-local bookkeeping

Add `thread_local GeneratorFrame* t_activeGenerator = nullptr` in `ProtoInterpreter.cpp`. Set it to the active frame at the start of `resumeGenerator()`, restore to the previous value on return (RAII). `OP_yield` reads `t_activeGenerator` to find the frame to save into — no extra parameter needed through the opcode dispatch loop.

`struct CatchFrame` is currently defined as a local struct inside `runBytecode`. Move its definition to `GeneratorFrame.h` so both files share the same type.

---

## Sub-feature B: Property Descriptor Enforcement

### Key files

| Action | File |
|--------|------|
| Modify | `src/ObjectPrototype.cpp` — register `objectDefineProperty`, `objectGetOwnPropertyDescriptor` |
| Modify | `src/ObjectPrototype.h` — declare new functions |
| Modify | `src/runtime/ProtoInterpreter.cpp` — descriptor check in `OP_put_field` and `OP_put_var_strict` |

### Architecture

#### Sidecar descriptor storage

For each property with an explicit descriptor, store a compact flags byte on the same ProtoObject under a hidden key:

```
key:   "__pd_<propName>__"
value: integer with bits:  bit0=writable, bit1=configurable, bit2=enumerable
```

Default JS semantics (no `defineProperty` called): all three bits = 1 → no sidecar key stored.
`Object.defineProperty` default: `writable=false, configurable=false, enumerable=false` unless specified.

#### `Object.defineProperty` native implementation

```cpp
static const proto::ProtoObject* objectDefineProperty(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    // args[0] = target object, args[1] = property name string, args[2] = descriptor object
    // 1. Extract propName string from args[1]
    // 2. Extract writable/configurable/enumerable/value/get/set from args[2]
    // 3. If value present: obj = obj->setAttribute(propName, value)
    // 4. Compute bits; if bits != 0b111: obj = obj->setAttribute("__pd_propName__", fromInteger(bits))
    // 5. updateMapping if obj changed
    // 6. Return obj
}
```

Register in `ObjectPrototype.cpp`:
```cpp
reg("defineProperty",            objectDefineProperty);
reg("getOwnPropertyDescriptor",  objectGetOwnPropertyDescriptor); // returns descriptor object
```

#### Check in `OP_put_field`

Insert before the existing `obj->setAttribute(key, val)` call:

```cpp
// Strict-mode write guard
if (mod->isStrict && key) {
    std::string keyStr;
    key->toUTF8String(pContext, keyStr);
    std::string pdKeyStr = "__pd_" + keyStr + "__";
    const proto::ProtoString* pdKey = pContext->fromUTF8String(pdKeyStr.c_str())
                                               ->asString(pContext);
    if (pdKey) {
        const proto::ProtoObject* desc = obj->getAttribute(pContext, pdKey, false);
        if (desc && desc != PROTO_NONE && desc->isInteger(pContext)) {
            bool writable = (static_cast<uint8_t>(desc->asLong(pContext)) & 0x1) != 0;
            if (!writable) {
                pending_exception = makeError(pContext, "TypeError",
                    "Cannot assign to read only property");
                goto handle_exception;
            }
        }
    }
}
```

Apply the same guard to `OP_put_array_el` and `OP_put_var_strict` for completeness.

#### Non-strict silent ignore

In non-strict mode, assignment to a `writable:false` property silently fails (does not update the value). Add a `break` before `setAttribute` when the guard fires and `!isStrict`.

#### Accessor properties (get/set)

If `args[2]` has `get` or `set` functions, store them as:
```
obj.__getter_propName__ = getFunc
obj.__setter_propName__ = setFunc
```

Check for `__getter_propName__` in `OP_get_field` and `__setter_propName__` in `OP_put_field`. This is a secondary enhancement; primary target is `writable:false` enforcement.

---

## Expected test recovery

| Sub-feature | Tests recovered | Confidence |
|-------------|----------------:|-----------|
| Generator protocol (sync) | ~340 | High — all 224 generator + 58 yield + ~60 others |
| Async generators | ~140 | Medium — depends on Promise integration |
| Property descriptor enforcement | ~60–80 | High for writable:false cluster |
| **Total** | **~480–500** | — |

Post-phase target: `language/expressions` **~86–87%** (9,558–9,578 / 11,036).

---

## Verification

### Smoke tests (run after each sub-feature)

**Generator smoke test:**
```js
function* counter() { yield 1; yield 2; yield 3; }
const it = counter();
console.assert(JSON.stringify(it.next()) === '{"value":1,"done":false}');
console.assert(JSON.stringify(it.next()) === '{"value":2,"done":false}');
console.assert(JSON.stringify(it.next()) === '{"value":3,"done":false}');
console.assert(JSON.stringify(it.next()) === '{"value":undefined,"done":true}');

// for-of over generator
const vals = [];
for (const v of counter()) vals.push(v);
console.assert(vals.join(',') === '1,2,3');
console.log('Generator smoke: PASSED');
```

**Property descriptor smoke test:**
```js
'use strict';
var obj = {};
Object.defineProperty(obj, 'x', { value: 42, writable: false });
console.assert(obj.x === 42);
var threw = false;
try { obj.x = 99; } catch(e) { threw = true; }
console.assert(threw, 'should throw TypeError in strict mode');
console.assert(obj.x === 42, 'value unchanged');
console.log('PropDescriptor smoke: PASSED');
```

### test262 run

```bash
TEST262_PATTERNS="language/expressions/generators,language/expressions/yield,language/expressions/async-generator,language/expressions/assignment,language/expressions/compound-assignment" \
  node tests/test262/runner/test262_runner.js
```

Full `language/expressions` run at end of phase to establish new baseline.

---

## Non-goals

- Async/await (`OP_await`) — separate phase
- `Symbol.iterator` protocol on user-defined objects — separate phase  
- `Object.freeze` / `Object.seal` enforcement — follow-on to property descriptors
- Getter/setter accessor traps in `OP_get_field` — secondary; only writable enforcement required for this phase
- `eval` implementation — deferred indefinitely
