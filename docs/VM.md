# The protoJS virtual machine

This document describes the bytecode virtual machine that runs protoJS
programs: how source becomes bytecode, how a frame is laid out, every opcode
the interpreter knows, and worked examples. Every bytecode listing in it is
real output of `protojs --disasm`, checked by the ctest `docs/vm-examples`;
none is written by hand.

Contents:

- [Start here](#start-here)
- [1. Overview](#1-overview)
- [2. Opcode reference](#2-opcode-reference)
- [3. Worked examples](#3-worked-examples)
- [4. How to add an opcode](#4-how-to-add-an-opcode)
- [5. How to add a native method](#5-how-to-add-a-native-method)
- [6. Known issues and oddities](#6-known-issues-and-oddities)

The code described here is `master` at commit `0dcefe1be` (2026-10-25).
Line numbers (`ProtoInterpreter.cpp:6520`) refer to that commit and drift as
the file changes; the function and label names do not.

## Start here

### Reading order

protoJS does not have a compiler of its own. The vendored QuickJS parses and
compiles JavaScript to QuickJS bytecode in compile-only mode; protoJS copies
that bytecode, rewrites a few instruction sequences, and runs it in its own
interpreter over protoCore objects. Read the source in the order a script
travels through it:

| Step | Files | Key functions | What happens |
|---|---|---|---|
| 1. Driver | `src/main.cpp`, `src/JSContext.cpp` | `main`, `JSContextWrapper::eval` | The whole script is one compilation unit: it is compiled, loaded and run once. `eval` creates the top-level `ProtoContext` (`frameCtx`) and keeps the loaded module alive for callbacks that run later. |
| 2. Parser and compiler (QuickJS) | `deps/quickjs/quickjs.c`, `deps/quickjs/quickjs-opcode.h`, `src/runtime/ProtoCompileOnly.cpp` | `compileToBytecodeWithFlags`, QuickJS's `js_parse_*`, `resolve_variables`, `resolve_labels`, `code_match` | `JS_Eval(..., JS_EVAL_FLAG_COMPILE_ONLY)` produces a `JSFunctionBytecode`. QuickJS's three passes (emit, resolve scopes, resolve labels and peephole/short opcodes) run unchanged. `quickjs-opcode.h` is the instruction set. |
| 3. Loader and rewrites | `src/runtime/ProtoBytecodeLoader.cpp`, `src/runtime/BytecodeSpecialiser.{h,cpp}`, `src/runtime/ProtoBytecodeModule.h` | `loadBytecode`, `loadBytecodeRecursive`, `markObjectLiterals`, `markPutFieldGroups`, `specialise`, `lowerClassConstructorPush`, `preResolveAllAtoms` | Each QuickJS function becomes a `ProtoBytecodeModule`: a rewritten copy of the bytecode, the constant pool converted to `ProtoObject*`, closure-variable tables, and nested functions flattened into the root module's `nestedFunctions`. Atoms are resolved to interned protoCore strings here, so the interpreter never calls QuickJS. |
| 4. VM | `src/runtime/ProtoInterpreter.{h,cpp}`, `src/runtime/GeneratorFrame.h` | `runBytecode`, `runBytecodeImpl`, the `PROTOJS_DISPATCH_TARGETS` table, the `L_OP_<name>` handlers, `runProtectedRange`, `callJSFunction`, `resumeGenerator`, `resumeContinuation` | The dispatch loop. One `ProtoContext` per activation of a bytecode function. |
| 5. Built-ins | `src/*Prototype.cpp`, `src/MathBuiltin.cpp`, `src/JSONBuiltin.cpp`, `src/modules/` | `ensureMathObject`, `BuildNumberPrototype`, ... | Native methods (`proto::ProtoMethod`) installed on the protoCore-native global and on the prototypes. |

`src/runtime/Disassembler.cpp` prints a loaded module; reading its operand
decoder (`Lister::decode`) next to `quickjs-opcode.h` is a quick way to learn
the operand of every opcode.

### Trying things with `--disasm`

```
protojs --disasm script.js
```

compiles and loads `script.js` exactly as `protojs script.js` would, prints
the top-level code and every nested function, depth first, and exits
**without running the script**. The listing shows the bytecode after the
loader's rewrites (section 1.3): what the dispatch loop executes. Example 01
in section 3 shows the listing of `console.log(1 + 2)`.

How to read a listing:

- `<script>:` is the top-level code. A nested function is named by its path
  of constant-pool indices: `function 0 fib` is the function in constant 0
  of the script; `function 0.1 <anonymous>` is the function in constant 1 of
  function 0. `fclosure8 0` in the script creates a closure of `function 0`.
- The header gives the frame shape: `args` (declared parameters), `locals`,
  `closure vars`, `stack` (the operand-stack size QuickJS computed, plus what
  write groups need) and flags: `strict`, `arrow`, `async`, `generator`,
  `constructor` (has `[[Construct]]`), `uses arguments` (the call site must
  build the argument list, 1.5).
- `slots:` names the argument and local slots. QuickJS's own hidden locals
  appear with their QuickJS names: `<ret>` (the completion value of the
  script), `this`, `new.target`, `this.active_func`, `<class_fields_init>`.
- `constants:` is the constant pool: index, kind (`function`, `number`,
  `bigint`, `string`, `regexp`, `object`) and value.
- `closure vars:` lists the variables the function reaches outside its own
  frame, with their QuickJS closure type: `parent local N` / `parent arg N`
  / `parent closure var N` (captured from the enclosing function),
  `global ref` (a global read from a function), `global decl` (a global
  declared by this script), `global` (an undeclared global name used by the
  script), plus `lexical` / `const`.
- `code:` has one instruction per line: byte offset, opcode name, raw
  operands, and a comment that decodes them: the slot or variable name, the
  constant, the property name (atom), a jump's absolute target (`-> 30`), a
  call's argument count.

Environment variables change what the loader emits (1.3): the golden
listings are produced with `PROTOJS_SPECIALISER`, `PROTOJS_LITERAL_BUILD`
and `PROTOJS_PUTFIELD_GROUPS` unset. Under the debugger (`--dap`) the
specialiser is off.

## 1. Overview

### 1.1 Pipeline

```
source --compileToBytecodeWithFlags (QuickJS, compile only)--> JSFunctionBytecode
  --loadBytecode (copy, rewrite, convert constants, resolve atoms)--> ProtoBytecodeModule
  --runBytecode (ProtoInterpreter)--> value
```

- `JSContextWrapper::eval` (`src/JSContext.cpp`) is the entry for a script
  file, `-e`, the REPL and preload files. CommonJS modules
  (`src/modules/CommonJSLoader.cpp`) use the same three steps.
- **Exceptions to the pipeline.** ES modules (`--input-type=module`) are
  evaluated by the QuickJS module evaluator, not by this VM. If the
  protoCore-side compile fails, `eval` retries with QuickJS's `JS_Eval`
  unless `PROTOJS_NO_FALLBACK=1`.
- The top level is an ordinary function body with no arguments. Its
  `var` and `function` declarations and its undeclared names are QuickJS
  *closure variables of global type*; `get_var` / `put_var` reach them by
  closure-variable index (2.4).
- QuickJS keeps the compiled function alive for the whole run
  (`ProtoCompileOnly.cpp` deliberately does not free it); the loaded
  module is pinned in the wrapper's root set.

### 1.2 Instruction encoding

- **Variable-length byte code.** One opcode byte followed by 0 to 9 operand
  bytes; instructions are 1 to 10 bytes long (`with_*` are the 10-byte
  ones). Operands are little-endian. The size and operand format of each
  QuickJS opcode come from its `DEF(id, size, n_pop, n_push, format)` entry
  in `quickjs-opcode.h`; protoJS's own opcodes have their sizes in two
  tables, `getOpSizes` (`ProtoBytecodeLoader.cpp`) and `getOpcodeSizes`
  (`BytecodeSpecialiser.cpp`).
- **Byte values.** `QuickJSOpcodeEnum.h` numbers the `DEF` entries in
  order: values 0 to 243 are QuickJS's final opcodes (`OP_COUNT` = 244).
  Values 244 to 250 are protoJS's rewritten and fused opcodes
  (`BytecodeSpecialiser.h` / `.cpp`). Values 251 to 255 are free. The 19
  phase-1 `def()` opcodes (`enter_scope`, `scope_get_var`, `label`, ...)
  are numbered from `OP_nop + 1` = 178 upwards, overlapping the short
  opcodes; QuickJS removes them before the bytecode is final, and they
  never reach protoJS.
- **Short forms.** QuickJS's last pass replaces common instructions with
  shorter ones: `get_loc0`..`get_loc3` and `get_loc8` for `get_loc`,
  `push_0`..`push_7`, `push_i8`, `push_i16` for `push_i32`, `call0`..
  `call3` for `call`, `if_false8` / `goto8` / `goto16` for the 32-bit jumps,
  and so on. The VM handles every form.
- **Jumps.** A QuickJS jump offset is relative to the address of its first
  operand byte: `target = opcode address + 1 + offset` for `goto`,
  `goto8`, `goto16`, `if_true`, `if_false` (and their 8-bit forms),
  `catch` and `gosub`; `with_*` carry their label after a 4-byte atom
  (`target = address + 5 + offset`). The fused opcodes 245 and 246 count
  from the **end** of the instruction (`address + 8 + offset` and
  `address + 9 + offset`). The disassembler prints the absolute target.
- **Operands that name things.**
  - *Atoms* (property names, string literals) are 32-bit QuickJS atom
    indices; the loader resolves each to an interned protoCore string
    (`atomToProto`).
  - *Locals* and *arguments* are slot indices (2.3).
  - *Closure variables* are indices into the function's closure-variable
    table. In this QuickJS version global variables are closure variables
    too, so `get_var`, `put_var` and `put_var_init` take a 16-bit
    closure-variable index, not an atom.
  - *Constants* index the constant pool: numbers, strings, BigInts,
    template objects, regular expressions and nested functions. The loader
    converts each with `TypeBridge::fromJS`; a nested function becomes a
    placeholder object carrying `__bytecode_id__`, an index into the root
    module's flat `nestedFunctions` table.
- **Unknown opcodes.** A byte without a handler goes to `L_default`, which
  raises a JavaScript error naming it: a SyntaxError
  `protoJS does not support <construct> (opcode 0x.. at byte offset N)` for
  a construct the VM does not implement (dynamic `import()`), an `Error`
  `protoJS internal error: unknown opcode 0x..` otherwise. A truncated
  operand or an operand-stack underflow raises `Error: protoJS internal
  error: malformed bytecode`. The function never returns `undefined` as if
  it had completed.

### 1.3 Loader rewrites

`loadBytecodeRecursive` runs four passes over each function, in this order.
Only the specialiser moves code; the other three keep every instruction's
size, so no jump changes.

1. **`markObjectLiterals`** (`PROTOJS_LITERAL_BUILD=off` disables it). An
   object literal `{a: x, b: y}` is compiled by QuickJS as `object` followed
   by one `define_field` per field. The pass turns `object` into
   `proto_object_imm` (247), which creates an **immutable** object, and the
   literal's last `define_field` into `proto_define_field_last` (248), which
   defines the last field and makes the object mutable with one clone. The
   fields in between extend the immutable object by structural sharing. It
   needs QuickJS's stack levels, so it runs first, on the unmodified
   bytecode. Skipped for a literal crossed by a jump or a `catch`, or with
   no `define_field`.
2. **`markPutFieldGroups`** (`PROTOJS_PUTFIELD_GROUPS=off`). A run of 2 to
   64 consecutive `recv.name = value` statements on the same receiver
   (`this`, an argument or a local), whose values cannot run code
   (constants, slot reads, arithmetic, comparisons, reads of the receiver's
   own fields), is rewritten: each `put_field` becomes
   `proto_put_field_group` (249), the last `proto_put_field_group_end`
   (250). At run time the first write checks once whether the receiver and
   the values allow it; if so, the run keeps its values on the stack and
   publishes **one** new version of the object with all the fields
   (`setAttributes`); otherwise every write takes the ordinary `put_field`
   path. The run's description (names, operands to check) is stored in the
   module (`putFieldGroups`), and the frame's stack size grows by the values
   the run keeps.
3. **`specialise`** (`PROTOJS_SPECIALISER=on|off`; **on is the default**,
   and the debugger forces off). A peephole pass that puts a fused
   instruction with a Number fast path in front of three loop shapes:
   - `get_loc dst; get_loc src; add; put_loc dst` (and QuickJS's
     `get_loc src; add_loc dst`) gets `proto_acc_loc8_loc8 dst src skip`
     (244): `s += i`.
   - `get_loc a; get_loc b; lt; if_false T` gets
     `proto_lt_loc8_loc8_jfalse a b skip T` (245): `i < n` as a loop test.
   - `get_loc a; get_var v; lt; if_false T` gets
     `proto_lt_loc_var_jfalse a v skip T` (246): `i < N` with `N` a global.

   `get_loc` here means any of `get_loc0`..`3`, `get_loc8`, `get_loc` (index
   up to 255) and `get_loc_check`. The fused instruction is a **guarded
   header**: the original instructions stay right after it. When both
   operands are Numbers (read through closure cells; for 246 the
   global object's own property, which is what `get_var` reads first) the
   header computes the result and skips them (`skip` bytes) or takes the
   jump; otherwise execution falls through into the original instructions,
   so BigInts, objects, strings, TDZ and undeclared globals keep
   their exact semantics. The buffer grows, so every code offset is
   re-encoded through an old-to-new offset table: jumps, `catch` handlers,
   `gosub` targets and `with_*` labels. A sequence with a jump target
   inside it is not fused, and a function whose short jump can no longer
   reach its target is left unspecialised. Example 06 shows a fused
   accumulator.
4. **`lowerClassConstructorPush`**. QuickJS emits a class as
   `push_const <constructor>; define_class`, and its `define_class` builds the
   constructor closure itself. protoJS's `define_class` expects a closure, so
   the loader rewrites that one `push_const` / `push_const8` into
   `fclosure` / `fclosure8` (same size). Example 14 shows it.

### 1.4 Frames

**One `ProtoContext` per activation of a bytecode function.** Each call of
an interpreted function constructs a child `proto::ProtoContext` on the C++
stack, chained to the caller's context (`previous`); the frame's slots are
that context's *automatic locals*, which protoCore scans as GC roots. The
layout (comment above `InterpFrame`, `ProtoInterpreter.cpp:3180`; set up in
`runBytecodeImpl`, 6684-6751):

```
[0, A)                arguments            A = argCount (declared parameters)
[A, A+V)              locals               V = varCount
[A+V, A+V+C)          closure variables    C = number of closure variables
[S, S+R)              operand stack        S = A+V+C, R = stackSize + 16
  S+R-3 .. S+R-1        (inside R) the active function, the async owner and the
                        module metadata, kept here as GC roots
[S+R, ...)            for-of state: 3 slots per for-of loop of the function
```

- `InterpFrame {ctx, stackBase, stackTop, stackCap}` records live in the
  thread-local vector `t_interpFrames`; they hold integers only, never a
  `ProtoObject*`. The dispatch loop caches the slot array pointer and
  refreshes it (`REFRESH_INTERP_STATE`) after anything that may grow it.
- **Absolute rule** (`src/runtime/README.md`): interpreter state that holds
  `ProtoObject*` (locals, arguments, operand stack) lives in context slots,
  never in a C++ container.
- Popping does not clear a slot: a popped operand stays rooted until a
  later push overwrites it, which is what lets a handler pop its operands
  and then allocate.
- Arguments are written into slots `0..` by the call site, from the
  caller's operand stack. Missing parameters stay `PROTO_NONE`
  (`undefined`). Locals start as `PROTO_NONE`; lexical ones are set to the
  TDZ sentinel by `set_loc_uninitialized`.
- **Closure cells.** When `fclosure` creates a closure that captures a local
  or argument, that slot is promoted *in place* to a cell (a mutable object
  whose value is its `__cv__` attribute), shared by the declaring frame and
  every closure. The captured cells are stored on the closure's *capture
  scope* (`__capture_scope__`, its `[[Environment]]`); at call entry
  `populateClosureCellsFromInstance` copies them into the callee's closure
  slots, which `get_var_ref` / `put_var_ref` address by index. `close_loc`
  replaces a cell by its current value when a binding ends, so the next
  closure of that local gets a fresh cell (per-iteration `let`).
- Closure slots of global type are pre-loaded at entry from the global
  object by name; `get_var` reads the live global first and uses the slot
  only as a fallback.

### 1.5 Calling conventions

All calls go through the opcodes `call` (and `call0`..`call3`),
`call_method`, `call_constructor`, `apply` (spread calls), their `tail_`
forms, and, from native code, `callJSFunction`. The callee decides the path:

1. **Bytecode function** (it has `__bytecode_id__`). The id is resolved
   against the function table of the module that created the closure
   (`__closure_module__`). A child `ProtoContext` is created
   (`ProtoInterpreter.cpp:16805` for `call`, 15825 for `call_method`, 16287
   for `call_constructor`, 20265 in `callJSFunction`), the first
   `min(argc, argCount)` arguments are copied into its slots, the closure
   cells are populated, and `runBytecode` runs the body. A `ProtoList` of
   all the arguments is built only when the callee's module has
   `usesArguments` (it contains `arguments`, a rest parameter or
   `init_ctor`); `call_constructor` always builds it. `this` is
   `undefined` for `call`, the receiver for `call_method`, the new object
   for `call_constructor`, and the captured `__arrow_this__` for an arrow.
   Sloppy-mode coercion of `this` happens lazily in `push_this`.
2. **Native method** (a `proto::ProtoMethod`, possibly wrapped as
   `__native_fn__`). Called with `callNative(fn, pContext, this, args)` **in
   the caller's context**; no context is created (5). Up to 16 arguments are
   passed in protoCore's array call form without allocating. `call` and
   `call_method` hold a `ProtoContext::CriticalSection` on the caller's
   context for the duration of the native call.
3. **Bound function** (`__bound_fn__`): the bound arguments are prepended
   and `callJSFunction(target, boundThis, args)` is called.
4. **Proxy**: every call site loops while the callee is a Proxy, calling the
   `apply` / `construct` trap or unwrapping to the target.
5. **Class constructor**: `call` throws
   `TypeError: Class constructor cannot be invoked without 'new'`;
   `call_constructor` creates the new object from `new_target.prototype`
   before running the body.

The result of a bytecode call is anchored in the caller's context when the
child context is destroyed (`anchorCallOutcome`; the `ProtoContext`
destructor turns `returnValue` into a `ReturnReference` in the parent).
Exceptions are C++ exceptions (1.7) and unwind through the call site.

### 1.6 GC safepoints and rooting

- `DISPATCH()` polls `pContext->safepoint()` once every **1024
  dispatches** (`(++t_dispatchCount & 1023) == 0`, `ProtoInterpreter.cpp:8792`).
  The counter is per thread and shared by all frames. There is no separate
  poll on backward jumps or calls; a loop reaches the poll through the
  counter.
- Allocation also polls (protoCore's allocator), and entering a
  `CriticalSection` at depth 0 runs a heap-limit checkpoint. While a
  critical section is open on a context, polls on that context do not park.
- Every value the interpreter holds is in a slot of the frame's context at
  each of these points: operands stay on the operand stack until the
  result is computed; values that cross an asynchronous boundary follow
  `docs/GC_BRIDGING.md`.
- `DISPATCH()` also stops the loop when `pc` leaves `[0, len)`: the function
  returns `undefined`.

### 1.7 Exceptions

A JavaScript `try` is a C++ `try`. Every throw raises a
`proto::ProtoException` carrying the thrown value (`throw` uses
`JSException`; handlers use `throwJS` or `signalNativeException`).

- `catch <handler>` pushes a catch frame (`{handler_pc, sentinel slot}`) on
  `catch_stack` and a sentinel on the operand stack, then runs the protected
  range as a **nested level** of the dispatch loop: `runProtectedRange`
  re-enters `runBytecodeImpl` on the same activation (same context, slots and
  `InterpFrame`) inside a C++ `try`. No context is created.
- The range ends when `drop` removes the sentinel or at `nip_catch`; the
  outer level continues after it. A `return` inside the range returns from
  every level.
- When a `ProtoException` (or a `std::exception`, converted to `Error`)
  escapes the range, the operand stack is cut back to the sentinel slot, the
  thrown value is pushed there, and the outer level continues at the
  handler.
- `finally` blocks are QuickJS subroutines: each exit from the `try` runs
  `gosub finally` (which pushes the return offset), the block ends with
  `ret`, and the exceptional path is `gosub finally; throw`. Example 13
  shows all three.
- Generators and async functions save `catch_stack` when they suspend; the
  resume re-enters one nested level per open range, outermost first.

### 1.8 Generators and async functions

- **Sync generators.** `initial_yield` creates the generator object and
  stores the suspended activation on it as attributes (`__gen_pc__`,
  `__gen_slots__`, a copy of the frame slots, and the others listed in
  `GeneratorFrame.h`), then returns it. `Generator.prototype.next` calls
  `resumeGenerator`, which creates a **new** `ProtoContext` chained to the
  caller of `next()`, restores the slots, operand stack and open `try`
  ranges, pushes the sent value and the resume kind, and continues after
  the `yield`.
- **Async functions and async generators.** A call of an async function goes
  to `startAsyncActivation`, which creates the promise and runs the body
  synchronously up to the first `await`. `await` records an immutable
  continuation (13 fields: pc, slots, high slots, stack depth, catch stack,
  `this`, the active function, the arguments, ...) and registers a reaction
  on the awaited value; the reaction job calls `resumeAwait` →
  `resumeContinuation`, which creates a new `ProtoContext` and re-enters the
  dispatch loop with the settled value pushed (or the reason thrown at the
  `await`). Async generators queue their `next` / `return` / `throw`
  requests and suspend the same way.
- Captured variables live in cells, so the slot copies keep sharing them.

## 2. Opcode reference

**251 opcode values are defined** (0 to 250): the 244 final opcodes of
QuickJS (values 0 to 243, `quickjs-opcode.h`) and 7 protoJS opcodes (244 to
250). The VM's dispatch table (`PROTOJS_DISPATCH_TARGETS`,
`ProtoInterpreter.cpp`) has handlers for **250** of them: 243 QuickJS
opcodes and all 7 protoJS ones. The one opcode without a handler is
`invalid` (0), which QuickJS never emits; reaching it, or a byte that is not
an opcode, raises `Error: protoJS internal error: unknown opcode` (1.2).
`import` (dynamic `import()`) has no handler on purpose: it raises
`SyntaxError: protoJS does not support dynamic import()` where it is
reached (section 6).
Every handled opcode is emitted by QuickJS or by the loader; `invalid` is
never emitted.

Notation: the stack effect is `before -> after`, top of stack on the right.
"Runs JS" means the instruction may call user code (getters, setters, Proxy
traps, `valueOf` / `toString`, iterators, callees). Every handler ends with
`DISPATCH()`, so every instruction can be followed by the safepoint poll
(1.6); the GC column lists only what the instruction adds. Unless stated
otherwise, malformed bytecode (a truncated operand, a stack underflow)
raises `Error: protoJS internal error: malformed bytecode` (1.2).

`undefined` has two representations: `PROTO_NONE` (an absent value) and the
heap singleton pushed by `undefined` (so that `[void 0]` stores an element,
not a hole). `null` is a heap singleton too. Both are `undefined` to
JavaScript.

### 2.1 Constants and immediates

| Op | # | Size | Operands | Stack | Semantics and errors | GC | Emitted by |
|---|---|---|---|---|---|---|---|
| `invalid` | 0 | 1 | none | | No handler: an internal error if reached (1.2). | | never |
| `push_i32` | 1 | 5 | i32 | `-> n` | Pushes the integer. | | integer literals outside the i16 range |
| `push_const` | 2 | 5 | u32 constant | `-> v` | Pushes the constant (number, string, BigInt, template object, ...). An out-of-range index pushes `undefined`. | | doubles, large BigInts, template objects; class constructors before the loader's rewrite (1.3) |
| `push_atom_value` | 4 | 5 | u32 atom | `-> string` | Pushes the atom as an interned string. | | string literals |
| `undefined` | 6 | 1 | none | `-> undefined` | Pushes the undefined singleton (not `PROTO_NONE`). | | `void e`, implicit `undefined` values, destructuring prologues |
| `null` | 7 | 1 | none | `-> null` | | | `null` |
| `push_this` | 8 | 1 | none | `-> this` | Strict code: `this` as passed. Sloppy code: `undefined` / `null` become the global object; a primitive is **not** boxed. | | function prologue that initialises the hidden `this` local |
| `push_false` | 9 | 1 | none | `-> false` | | | `false`; `delete` of a local |
| `push_true` | 10 | 1 | none | `-> true` | | | `true` |
| `special_object` | 12 | 2 | u8 kind | `-> obj` | 0 arguments / 1 mapped arguments: an array-like copy of the call's arguments (`length`, `Symbol.toStringTag`); both kinds are unmapped and have no `callee`. 2 the active function, 3 `new.target`, 4 the home object (`__home_object__` of the active function), 5 the variable object of a sloppy function with a direct `eval` (an object with no prototype), 6 `import.meta`: `undefined`. | Allocates (kinds 0, 1, 5). | function prologue: `arguments`, `new.target`, `super`, a named function expression's own name, sloppy direct `eval` |
| `rest` | 13 | 3 | u16 first index | `-> array` | An Array of the arguments from the index on. | Allocates. | rest parameter `...r` |
| `push_bigint_i32` | 176 | 5 | i32 | `-> bigint` | A BigInt of the value. | Allocates. | BigInt literals that fit in 32 bits |
| `push_minus1`, `push_0` .. `push_7` | 178, 179-186 | 1 | value in the opcode | `-> n` | | | short forms |
| `push_i8` | 187 | 2 | i8 | `-> n` | | | short form |
| `push_i16` | 188 | 3 | i16 | `-> n` | | | short form |
| `push_const8` | 189 | 2 | u8 constant | `-> v` | As `push_const`. | | short form |
| `push_empty_string` | 191 | 1 | none | `-> ""` | | | `''` |

### 2.2 Operand-stack manipulation

Pure pointer moves: none allocates or runs JS. `dup` and `drop` on an empty
stack do nothing. `drop` of a catch frame's sentinel ends the protected
range (1.7).

| Op | # | Size | Stack | Emitted by (examples) |
|---|---|---|---|---|
| `drop` | 14 | 1 | `a ->` | expression statements, discarded results, end of a `try` range |
| `nip` | 15 | 1 | `a b -> b` | comma and optional-chain cleanups |
| `nip1` | 16 | 1 | `a b c -> b c` | iterator cleanups, destructuring |
| `dup` | 17 | 1 | `a -> a a` | assignments used as values, `&&` / `||`, `switch` |
| `dup1` | 18 | 1 | `a b -> a a b` | array literals with spread and holes |
| `dup2` | 19 | 1 | `a b -> a b a b` | `o[k] op= v` |
| `dup3` | 20 | 1 | `a b c -> a b c a b c` | `super[k] op= v` |
| `insert2` | 21 | 1 | `obj a -> a obj a` | `o.x = v` used as a value, postfix on fields |
| `insert3` | 22 | 1 | `obj prop a -> a obj prop a` | `o[k] = v` used as a value |
| `insert4` | 23 | 1 | `this obj prop a -> a this obj prop a` | `super[k] = v` used as a value |
| `perm3` | 24 | 1 | `obj a b -> a obj b` | `o.x++` |
| `perm4` | 25 | 1 | `obj prop a b -> a obj prop b` | `o[k]++` |
| `perm5` | 26 | 1 | `this obj prop a b -> a this obj prop b` | postfix on `super[k]` |
| `swap` | 27 | 1 | `a b -> b a` | calls, `in`, class definitions, destructuring |
| `swap2` | 28 | 1 | `a b c d -> c d a b` | for-of and destructuring |
| `rot3l` | 29 | 1 | `x a b -> a b x` | compound and logical assignment |
| `rot3r` | 30 | 1 | `a b x -> x a b` | destructuring defaults, logical assignment |
| `rot4l` | 31 | 1 | `x a b c -> a b c x` | logical assignment on elements |
| `rot5l` | 32 | 1 | `x a b c d -> a b c d x` | logical assignment on `super[k]`, iterator close paths |

### 2.3 Arguments, locals and closure variables

"local i" is slot `A + i`, "arg i" slot `i`, "var_ref i" slot `A + V + i`
(1.4). Every read goes through the cell when the slot holds one, and every
write writes the cell. An out-of-range index makes a read push `undefined`
and a write do nothing. A write into a cell is a `setAttribute` on a mutable
object and may allocate.

| Op | # | Size | Operands | Stack | Semantics and errors | Emitted by |
|---|---|---|---|---|---|---|
| `get_loc` | 85 | 3 | u16 local | `-> v` | | reads of a local |
| `put_loc` | 86 | 3 | u16 local | `v ->` | | assignment, initialisation |
| `set_loc` | 87 | 3 | u16 local | `v -> v` | As `put_loc`, keeping the value. | assignment used as a value |
| `get_arg` | 88 | 3 | u16 arg | `-> v` | | parameter reads |
| `put_arg` | 89 | 3 | u16 arg | `v ->` | | parameter assignment, defaults |
| `set_arg` | 90 | 3 | u16 arg | `v -> v` | | the same, as a value |
| `get_var_ref` | 91 | 3 | u16 closure var | `-> v` | ReferenceError `Cannot access before initialization` when the slot or cell holds the TDZ sentinel. | captured variables; top-level function declarations |
| `put_var_ref` | 92 | 3 | u16 closure var | `v ->` | TDZ check for lexical bindings. A slot that is not a cell is also published to the global binding (`writeGlobalBinding`): this is how a top-level `function f` reaches the global. | captured variables; `fclosure; put_var_ref` for top-level functions |
| `set_var_ref` | 93 | 3 | u16 closure var | `v -> v` | | the same, as a value |
| `set_loc_uninitialized` | 94 | 3 | u16 local | `->` | Stores the TDZ sentinel. | entry of a scope with `let` / `const` / `class` |
| `get_loc_check` | 95 | 3 | u16 local | `-> v` | ReferenceError `Cannot access before initialization` on the TDZ sentinel (the message omits the name). | reads of a lexical local not proven initialised |
| `put_loc_check` | 96 | 3 | u16 local | `v ->` | As `put_loc`; ReferenceError `Cannot access before initialization` while the slot (or its cell) holds the TDZ sentinel. | assignment to a `let` local |
| `set_loc_check` | 97 | 3 | u16 local | `v -> v` | As `put_loc_check`, keeping the value. | QuickJS peephole `dup; put_loc_check` (example 02) |
| `put_loc_check_init` | 98 | 3 | u16 local | `v ->` | ReferenceError `'this' can be initialized only once` unless the slot (or its cell, when an arrow captured `this`) holds the TDZ sentinel. | `this` after `super()` in a derived constructor |
| `get_loc_checkthis` | 99 | 3 | u16 local | `-> this` | ReferenceError `this is not initialized - super() must be called first` on TDZ. | implicit `return this` of a derived constructor |
| `get_var_ref_check` | 100 | 3 | u16 closure var | `-> v` | As `get_var_ref`. | reads of a captured `let` / `const` / `class` |
| `put_var_ref_check` | 101 | 3 | u16 closure var | `v ->` | As `put_var_ref`; ReferenceError while the cell holds the TDZ sentinel. | assignment to a captured `let` |
| `put_var_ref_check_init` | 102 | 3 | u16 closure var | `v ->` | As `put_var_ref`; ReferenceError `'this' can be initialized only once` unless the cell holds the TDZ sentinel. | `this` initialised by `super()` inside an arrow |
| `close_loc` | 103 | 3 | u16 local | `->` | Replaces the local's cell by the cell's value; closures created before keep the old cell. | end of a scope whose locals were captured; per-iteration `let` |
| `get_loc8`, `put_loc8`, `set_loc8` | 192-194 | 2 | u8 local | as above | | short forms |
| `get_loc0`..`3`, `put_loc0`..`3`, `set_loc0`..`3` | 195-206 | 1 | index in the opcode | as above | | short forms |
| `get_arg0`..`3`, `put_arg0`..`3`, `set_arg0`..`3` | 207-218 | 1 | index in the opcode | as above | | short forms |
| `get_var_ref0`..`3`, `put_var_ref0`..`3`, `set_var_ref0`..`3` | 219-230 | 1 | index in the opcode | as above | | short forms |
| `make_loc_ref`, `make_arg_ref`, `make_var_ref_ref` | 118-120 | 7 | u32 atom, u16 index | `-> ref name` | A reference to the variable: an object holding its cell (the slot is promoted to a cell, as for a closure), read and written by `get_ref_value` / `put_ref_value`. | assignments to a variable inside `with` or after a sloppy direct `eval` |

### 2.4 Globals and scope variables

The global names a function uses are its closure variables of global type;
the operand is the closure-variable index and the name is
`closureSymbols[index]`. Reads and writes go to the live binding root
`*pGlobalRoot`: for the main script a scope object for top-level
`let` / `const` / `class` whose parent is the global object (`var`,
`function`, built-ins). The global TDZ is "a lexical name absent from the
root".

| Op | # | Size | Operands | Stack | Semantics and errors | GC | Emitted by |
|---|---|---|---|---|---|---|---|
| `get_var_undef` | 55 | 3 | u16 closure var | `-> v` | Reads the global; `undefined` when absent and undeclared; ReferenceError for a declared lexical global in its TDZ. Getters on the global object are not called. | | `typeof x` of a global name |
| `get_var` | 56 | 3 | u16 closure var | `-> v` | As above, plus ReferenceError `Cannot access 'x' before initialization` (lexical, absent) and `x is not defined` (absent and not declared). | | reads of a global or undeclared name |
| `put_var` | 57 | 3 | u16 closure var | `v ->` | `writeGlobalBinding`. TypeError on a `const`; ReferenceError `Cannot assign to read only binding 'undefined'` for `undefined`; SyntaxError for a lexical `eval` / `arguments`. ReferenceError `Cannot access 'x' before initialization` for a lexical global not yet declared. No strict-mode unresolvable-reference check (section 6). | Allocates. | assignment to a global |
| `put_var_init` | 58 | 3 | u16 closure var | `v ->` | As `put_var` without the `const` check; creating the binding ends its TDZ. | Allocates. | initialisation of a top-level `let` / `const` / `class` |
| `get_ref_value`, `put_ref_value` | 59, 60 | 1 | none | `obj name -> obj name v` / `obj name v ->` | GetValue / PutValue of a reference: the cell of a variable reference, else [[HasProperty]] then [[Get]] / [[Set]] on the object. An unresolvable reference (`undefined`) is a ReferenceError, except a sloppy-mode write, which creates a global. | Runs JS. | identifier references inside `with` |
| `delete_var` | 151 | 5 | u32 atom | `-> bool` | `delete x` of a global name: deletes the global property; false when it is not configurable (a `var` or function declaration), true when absent. | Runs JS (Proxy). | sloppy `delete x` of a global |
| `with_get_var`, `with_put_var`, `with_delete_var`, `with_make_ref`, `with_get_ref` | 113-117 | 10 | u32 atom, i32 label, u8 is_with | `obj -> ...` | One object environment record (a `with` object, or a sloppy function's variable object): when `obj` has the binding (and, for `with`, `obj[Symbol.unscopables]` does not block it), the access is done on `obj` and execution jumps to the label; otherwise `obj` is dropped and the next record follows. | Runs JS. | identifiers inside `with (o) { ... }`; variables of a sloppy direct `eval` |
| `make_var_ref` | 121 | 5 | u32 atom | `-> obj name` | The global object when it has the name, else `undefined` (an unresolvable reference, resolved by `put_ref_value`). | | global references inside `with` |

### 2.5 Control flow

| Op | # | Size | Operands | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|---|
| `if_false` | 104 | 5 | i32 offset | `c ->` | Jumps if `c` is falsy (`toBool`). | `if`, loops, `?:`, `&&`, `||`, `??` |
| `if_true` | 105 | 5 | i32 offset | `c ->` | Jumps if `c` is truthy. | inverted tests, `do..while` |
| `goto` | 106 | 5 | i32 offset | `->` | Unconditional jump; no safepoint of its own. | loop back edges, `break`, `continue`, `else` |
| `if_false8`, `if_true8`, `goto8` | 232-234 | 2 | i8 offset | as above | | short forms |
| `goto16` | 235 | 3 | i16 offset | `->` | | short form |
| `gosub` | 108 | 5 | i32 offset | `-> ret` | Pushes the offset of the next instruction as an integer and jumps to a `finally` block. | every exit through a `finally` |
| `ret` | 109 | 1 | none | `ret ->` | Pops the integer and jumps to it. | end of a `finally` block |
| `nop` | 177 | 1 | none | `->` | | QuickJS leftovers |

Falsy values (`toBool`): `undefined` (both forms), `null`, `false`, 0, -0,
`NaN`, `""`, `0n`. Everything else is truthy.

### 2.6 Calls and returns

| Op | # | Size | Operands | Stack | Semantics and errors | GC | Emitted by |
|---|---|---|---|---|---|---|---|
| `call` | 34 | 3 | u16 argc | `f a1..an -> r` | Calls `f` with `this` undefined (1.5). TypeError `is not a function` (no callee name); `Class constructor cannot be invoked without 'new'`. | Runs JS. Natives run under a critical section of the caller's context. | `f(args)` |
| `call0`..`call3` | 236-239 | 1 | argc in the opcode | as `call` | | | short forms |
| `tail_call` | 35 | 3 | u16 argc | `f a1..an ->` | As `call`, then returns the result from the current function. | | `return f(args)` (QuickJS peephole `call; return`) |
| `call_method` | 36 | 3 | u16 argc | `this f a1..an -> r` | Calls `f` with the receiver as `this`. TypeError `Class constructor cannot be invoked without 'new'` for a class constructor. | Runs JS. | `o.m(args)`, `o[k](args)` |
| `tail_call_method` | 37 | 3 | u16 argc | `this f a1..an ->` | As `call_method`, then returns the result. | | `return o.m(args)` (examples 12, 14) |
| `call_constructor` | 33 | 3 | u16 argc | `f newTarget a1..an -> obj` | `new` and `super(...)`: creates the object from `newTarget.prototype`, runs the constructor, keeps its result if it is an object. TypeError `function is not a constructor` (arrows, methods, generators, async); the Proxy `construct` trap errors; RangeError `Invalid array length` (Array). | Allocates. The whole handler runs inside a critical section on the caller's context; the body runs in a child context. | `new F(args)`, `super(args)` |
| `apply` | 39 | 3 | u16 magic (bit 0: construct) | `f this array -> r` | Spread call: `callJSFunction(f, this, elements)`; with magic 1 the middle operand is the new.target, as in QuickJS (`f` for `new F(...xs)`, the running constructor's new.target for `super(...xs)`): the object is created from its `prototype`. | Runs JS. | `f(...xs)`, `new F(...xs)`, `super(...xs)` (example 17) |
| `array_from` | 38 | 3 | u16 count | `e1..en -> array` | An Array of the values; `PROTO_NONE` becomes the undefined singleton. | Allocates. | array literals; argument arrays of spread calls |
| `eval` | 50 | 5 | u16 argc, u16 scope | `f a1..an -> r` | When `f` is %eval%, a direct eval (`performEval`): a string argument is compiled against the calling function's QuickJS bytecode at `scope` (`protojs_compile_direct_eval`), cached per (caller, scope, source), and run in a child context whose closure variables are the caller's variables (as cells); any other argument is the result. Otherwise an ordinary call with `this` undefined. Calling %eval% itself is an indirect eval (global code). | Runs JS. Allocates. | `eval(...)` |
| `apply_eval` | 51 | 3 | u16 scope | `f array -> r` | `eval(...xs)`: as `eval` with the array's first element. | Runs JS. | `eval(...xs)` |
| `import` | 54 | 1 | none | | **No handler**: SyntaxError `protoJS does not support dynamic import()` (section 6). | | dynamic `import()` |
| `return` | 40 | 1 | none | `v ->` | Returns `v` from the activation (from every nested level). | | `return e` |
| `return_undef` | 41 | 1 | none | `->` | Returns `undefined`. | | `return;`, end of a body |
| `return_async` | 47 | 1 | none | `v ->` | Returns `v` from a generator or async activation; the caller settles the promise or wraps `{value, done: true}`. | | every `return` of generators and async functions |
| `check_ctor` | 43 | 1 | none | `->` | TypeError `Class constructor cannot be invoked without 'new'` when the activation has no new.target (every construct path publishes one). | | start of a class constructor |
| `check_ctor_return` | 42 | 1 | none | `r -> r useThis` | `undefined` → true, an object → false, else TypeError `derived class constructor must return an object or undefined`. Symbol and BigInt results are accepted (section 6). | | `return` in a derived constructor |
| `init_ctor` | 44 | 1 | none | `-> this` | Default derived constructor: calls the parent constructor with the call's arguments and pushes the object. | Runs JS. Allocates. | `class B extends A {}` without a constructor |

### 2.7 Closures

| Op | # | Size | Operands | Stack | Semantics | GC | Emitted by |
|---|---|---|---|---|---|---|---|
| `fclosure` | 3 | 5 | u32 constant | `-> fn` | Creates a function object for the nested function in the constant: `__bytecode_id__`, `__closure_module__`, `name`, `length`, `__arrow_this__` (arrows), the capture scope (captured slots promoted to cells, 1.4), a lazy `prototype` for ordinary functions, an eager one for generators. | Allocates several objects. No JS. | function declarations and expressions, arrows, methods; class constructors (loader rewrite) |
| `fclosure8` | 190 | 2 | u8 constant | `-> fn` | As `fclosure`, but does not stamp `__metadata__`. | | short form |

### 2.8 Exceptions

| Op | # | Size | Operands | Stack | Semantics and errors | Emitted by |
|---|---|---|---|---|---|---|
| `catch` | 107 | 5 | i32 offset (handler) | `-> sentinel` | Opens a protected range as a nested level (1.7). The handler starts with the thrown value in the sentinel's slot. | `try` |
| `nip_catch` | 110 | 1 | none | `sentinel .. v -> v` | Closes the innermost range, keeping the top value; ends the nested level. When a for-of record is above the innermost range, the value replaces the record's catch slot instead (`return` inside a for-of body). | `return` / `break` out of a `try` or a for-of |
| `throw` | 48 | 1 | none | `v ->` | Throws `v` (any value). | `throw`; rethrow after a `finally` |
| `throw_error` | 49 | 6 | u32 atom, u8 kind | `->` | Kind 0 TypeError `'x' is read-only`, 1 SyntaxError `redeclaration of 'x'`, 2 ReferenceError `x is not initialized`, 3 ReferenceError `unsupported reference to 'super'`, 4 TypeError `iterator does not have a throw method`. | assignment to a `const`, redeclarations, `delete super.x`, `yield*` |

### 2.9 Arithmetic, comparison and logic

Common to the binary numeric operators: two BigInts give a BigInt; a BigInt
and another primitive throw TypeError `Cannot mix BigInt and other types`;
then booleans and `null` become numbers; then a SmallInteger fast path
(inline, no allocation; results outside the SmallInteger range are promoted
by protoCore); then a double fast path; then ToPrimitive (`Symbol.toPrimitive`,
`valueOf`, `toString`; TypeError `Cannot convert object to primitive value`)
and ToNumber. The slow paths run JS and allocate.

| Op | # | Size | Stack | Semantics and errors |
|---|---|---|---|---|
| `add` | 155 | 1 | `a b -> a+b` | String concatenation if either primitive is a string, else numeric addition. |
| `sub` | 156 | 1 | `a b -> a-b` | |
| `mul` | 152 | 1 | `a b -> a*b` | SmallInteger path only for exact non-zero results (keeps -0). |
| `div` | 153 | 1 | `a b -> a/b` | BigInt division by zero: RangeError `Division by zero`. |
| `mod` | 154 | 1 | `a b -> a%b` | |
| `pow` | 157 | 1 | `a b -> a**b` | `std::pow`; converts the right operand first, and `1 ** Infinity` gives 1 (section 6). RangeError for a negative BigInt exponent. |
| `shl`, `sar`, `shr` | 158-160 | 1 | `a b -> r` | `ToInt32(a)` shifted by `ToUint32(b) & 31`; `>>>` on BigInts: TypeError `BigInt does not support unsigned right shift`. |
| `and`, `xor`, `or` | 171-173 | 1 | `a b -> r` | Bitwise on `ToInt32`, or on BigInts. |
| `lt`, `lte`, `gt`, `gte` | 161-164 | 1 | `a b -> bool` | BigInt comparison; `undefined` on either side gives false without converting the other (section 6); strings compare lexicographically, else numerically; `NaN` gives false. |
| `eq`, `neq` | 167, 168 | 1 | `a b -> bool` | Abstract equality (§7.2.14): two objects are equal only when identical, an object never equals `null` / `undefined`, and ToPrimitive runs only for an object compared with another primitive. |
| `strict_eq`, `strict_neq` | 169, 170 | 1 | `a b -> bool` | Identity, numbers by value (`NaN` unequal, `0 === -0`), strings by content, BigInts by value. No JS. |
| `instanceof` | 165 | 1 | `obj C -> bool` | Walks the prototype chain against `C.prototype` (Proxy `getPrototypeOf` traps honoured). TypeError `Right-hand side of 'instanceof' is not callable` for a primitive `C`. Ignores `Symbol.hasInstance` and bound functions (section 6). |
| `in` | 166 | 1 | `key obj -> bool` | Own and inherited properties, accessors, Proxy `has`. TypeError `Cannot use 'in' operator to search for property in non-object`. |
| `private_in` | 175 | 1 | `obj x -> bool` | `#x in obj`: whether `obj` has the private field `x` (a private name), or the brand of the class of the private method `x`. TypeError for a primitive `obj`. |
| `neg` | 138 | 1 | `a -> -a` | |
| `plus` | 139 | 1 | `a -> +a` | ToNumber; TypeError for BigInt and Symbol. |
| `inc`, `dec` | 141, 140 | 1 | `a -> a±1` | |
| `post_inc`, `post_dec` | 143, 142 | 1 | `a -> old new` | Pushes `ToNumeric(a)` and `a±1`. |
| `inc_loc`, `dec_loc` | 145, 144 | 2 | `->` | u8 local updated in place (through its cell). No BigInt path. Emitted by QuickJS's peephole for `i++` / `i--` statements. |
| `add_loc` | 146 | 2 | `v ->` | u8 local `+= v` in place, with the semantics of `add` (SmallInteger fast path, then the same `addValues` helper). Emitted by QuickJS's peephole for `x += e`. |
| `not` | 147 | 1 | `a -> ~a` | |
| `lnot` | 148 | 1 | `a -> !a` | `!toBool(a)`. |
| `typeof` | 149 | 1 | `v -> string` | `"symbol"` also for a string whose text starts with `Symbol(` or `Symbol.` (section 6); a callable Proxy is `"object"`. |
| `delete` | 150 | 1 | `obj key -> bool` | Deletes an own property: false (sloppy) or TypeError `Cannot delete non-configurable property` (strict) when non-configurable; Proxy `deleteProperty`. A `null` / `undefined` base is not rejected (section 6). |

### 2.10 Type tests

| Op | # | Size | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `is_undefined_or_null` | 174 | 1 | `v -> bool` | | `??`, `?.`, destructuring defaults |
| `is_undefined` | 240 | 1 | `v -> bool` | | peephole of `=== undefined`, default parameters |
| `is_null` | 241 | 1 | `v -> bool` | | peephole of `=== null` |
| `typeof_is_undefined` | 242 | 1 | `v -> bool` | | peephole of `typeof x === "undefined"` |
| `typeof_is_function` | 243 | 1 | `v -> bool` | Same function test as `typeof`. | peephole of `typeof x === "function"` |

### 2.11 Objects and properties

Property reads walk protoCore's attribute lookup (prototype chain
included), with protoJS's accessors (`__get_x__` / `__set_x__` sidecars),
property descriptors (`__pd_x__`), the per-object property dictionary for
computed keys, lazily created `prototype` objects and Proxy traps layered on
top. A missing property is `undefined`.

| Op | # | Size | Operands | Stack | Semantics and errors | GC | Emitted by |
|---|---|---|---|---|---|---|---|
| `object` | 11 | 1 | none | `-> obj` | A new mutable child of `Object.prototype`. | Allocates. | object literals the loader does not rewrite; destructuring rest |
| `get_field` | 61 | 5 | u32 atom | `obj -> v` | `[[Get]]`. TypeError `Cannot read properties of undefined (reading 'x')` / `of null`. | Runs JS. | `o.x` |
| `get_field2` | 62 | 5 | u32 atom | `obj -> obj v` | As `get_field`, keeping the object (the receiver of a method call). | Runs JS. | `o.m(...)`, `o.x op= v` |
| `put_field` | 63 | 5 | u32 atom | `obj v ->` | `[[Set]]`: setters, Proxy `set`, non-extensible objects (TypeError in strict code), `length` of arrays (RangeError `Invalid array length`); TypeError `Cannot set properties of undefined`. A write to the global object republishes the binding root. | Runs JS. Allocates a new object version. | `o.x = v` |
| `get_array_el` | 67 | 1 | none | `obj key -> v` | `[[Get]]` with a computed key; dense-array, typed-array and string fast paths. TypeError on `null` / `undefined` (without the key). | Runs JS. | `o[k]` |
| `get_array_el2` | 68 | 1 | none | `obj key -> obj v` | As `get_array_el`, keeping the object. No Proxy dispatch (section 6). | Runs JS. | `o[k](...)` |
| `get_array_el3` | 69 | 1 | none | `obj key -> obj key v` | As `get_array_el`, keeping both (the raw key, not the converted one). | Runs JS. | `o[k] op= v`, `o[k]++` |
| `put_array_el` | 70 | 1 | none | `obj key v ->` | `[[Set]]` with a computed key. | Runs JS. Allocates. | `o[k] = v` |
| `get_length` | 231 | 1 | none | `obj -> n` | `length`: code points of a string (not UTF-16 units), the element count of an array, else the property. Returns `undefined` for a `null` / `undefined` receiver instead of throwing (section 6). | Runs JS (getter). | `x.length` |
| `define_field` | 73 | 5 | u32 atom | `obj v -> obj'` | Defines an own data property (no setter call) and pushes the new version. | Allocates. | object literal fields; class fields |
| `set_name` | 74 | 5 | u32 atom | `fn -> fn` | Sets the function's `name`. | Allocates. | anonymous functions assigned to a name |
| `set_name_computed` | 75 | 1 | none | `key fn -> key fn` | `name` from a computed key; a Symbol key gives `Symbol(d)` instead of `[d]` in object literals (section 6). | Runs JS. | `{[k]: function () {}}` |
| `set_proto` | 76 | 1 | none | `obj proto -> obj` | `__proto__: p` in a literal (records a JS prototype override). | | `{__proto__: p}` |
| `set_home_object` | 77 | 1 | none | `home fn -> home fn` | Stores `__home_object__`. | Allocates. | methods that use `super` |
| `define_array_el` | 78 | 1 | none | `arr i v -> arr i` | Defines element `i`. | Allocates. | array literals after a spread or a hole |
| `append` | 79 | 1 | none | `arr pos iterable -> arr pos` | Spreads `iterable` into `arr` from `pos`. TypeError `... is not iterable`. An object with a numeric `length` is copied by index without calling `@@iterator` (section 6). | Runs JS. Allocates. | `[...xs]`, `f(...xs)` |
| `copy_data_properties` | 80 | 2 | u8 stack offsets | unchanged | Copies own enumerable properties (getters' values included) into the target, skipping an exclude list. A string source is skipped (section 6). | Runs JS. Allocates. | `{...o}`, object rest |
| `define_method` | 81 | 6 | u32 atom, u8 flags | `obj fn -> obj` | Method (flags 0), getter (1) or setter (2); bit 2 enumerable. | Allocates. | object literal and class methods and accessors |
| `define_method_computed` | 82 | 2 | u8 flags | `obj key fn -> obj` | As `define_method` with a computed key. | Runs JS. | `{[k]() {}}` |
| `regexp` | 52 | 1 | none | `pattern bytecode -> rx` | A RegExp from the pattern and the flags read from QuickJS's compiled pattern. Flags above the low byte (named groups) are lost (section 6). | Allocates. | regular expression literals |
| `to_object` | 111 | 1 | none | `v -> v` | TypeError `Cannot convert undefined to object` / `null`; the value is left as is. | | destructuring |
| `to_propkey` | 112 | 1 | none | `k -> key` | ToPropertyKey: an integer, a string or a Symbol stays; an object goes through ToPrimitive with hint string; any other value becomes its ToString. | Runs JS. | computed keys of object literals and classes |

### 2.12 Classes, `super` and private members

| Op | # | Size | Operands | Stack | Semantics and errors | Emitted by |
|---|---|---|---|---|---|---|
| `define_class` | 83 | 6 | u32 atom (name), u8 flags (bit 0: has `extends`) | `parent ctor -> ctor proto` | Uses the constructor closure (loader rewrite, 1.3); builds `proto` as a child of `parent.prototype` (or `Object.prototype`), links `prototype` / `constructor`, marks the class constructor, sets `name` and `length`. TypeError `Class extends value does not have valid prototype property`. | `class` |
| `define_class_computed` | 84 | 6 | u32 atom, u8 flags | `name parent ctor -> name ctor proto` | As `define_class`, the name taken from the stack. | class expressions with a computed name |
| `get_super` | 53 | 1 | none | `obj -> parent` | The parent constructor (`__class_parent__`) for `super(...)`, else the prototype of the home object. | `super(...)`, `super.x` |
| `get_super_value` | 71 | 1 | none | `this super key -> v` | `[[Get]]` on the super object with `this` as receiver. | `super.x` |
| `put_super_value` | 72 | 1 | none | `this super key v ->` | `[[Set]]` with `this` as receiver; TypeError in strict code when refused. | `super.x = v` |
| `private_symbol` | 5 | 5 | u32 atom | `-> sym` | A new private name for this evaluation of the class: an object holding a unique attribute key (`__priv_<n>_#x__`, hidden from every key enumeration) and the source name. | each `#x` declaration |
| `get_private_field` | 64 | 1 | none | `obj sym -> v` | The own field under the name's key; TypeError `Cannot read private member #x from an object whose class did not declare it` when absent. | `this.#x` |
| `put_private_field` | 65 | 1 | none | `obj v sym ->` | Writes the own field; TypeError when absent. | `this.#x = v` |
| `define_private_field` | 66 | 1 | none | `obj sym v -> obj` | Adds the field; TypeError when it exists (`Cannot initialize #x twice on the same object`). | private field initialisers |
| `check_brand` | 45 | 1 | none | `obj fn -> obj fn` | TypeError unless `obj` carries the brand of the class (the home object) of the private method or accessor `fn`. | private method and accessor access |
| `add_brand` | 46 | 1 | none | `obj home ->` | Gives the class (`home`) a unique brand key on first use and stamps it on `obj`. | classes with private methods |

### 2.13 Iteration

| Op | # | Size | Operands | Stack | Semantics and errors | GC | Emitted by |
|---|---|---|---|---|---|---|---|
| `for_in_start` | 122 | 1 | none | `obj -> enum` | Snapshots the enumerable string keys: array indices, then each prototype-chain level's attributes (protoCore order) and dictionary keys; nearer levels shadow farther ones. Primitives give an empty list. | Allocates. Proxy traps run JS. | `for (k in o)` |
| `for_in_next` | 125 | 1 | none | `enum -> enum key done` | Next key of the snapshot; keys deleted during the loop are still visited (section 6). | | `for (k in o)` |
| `for_of_start` | 123 | 1 | none | `iterable -> rec next catch` | Gets the iterator (`[Symbol.iterator]()`; built-in array iterators and generators directly) and keeps its state in the frame's for-of slots (1.4); pushes a small record naming the slots. TypeError `null is not iterable` / `undefined is not iterable` / `object is not iterable (cannot read property Symbol.iterator)`. | Runs JS. Allocates. | `for (x of y)`, array destructuring, sync `yield*` |
| `for_of_next` | 126 | 2 | u8 depth (values between the record and the top) | `rec next catch .. -> rec next catch .. value done` | Steps the iterator: array fast path, else `next()`; TypeError `Iterator result is not an object`. `next` is read on every step (section 6). | Runs JS. | for-of loop head, destructuring elements |
| `for_await_of_start` | 124 | 1 | none | `iterable -> iter next catch` | `[Symbol.asyncIterator]()`, or an async-from-sync wrapper of `[Symbol.iterator]()`. TypeError `object is not async iterable`. | Runs JS. | `for await (x of y)`, `yield*` in async generators |
| `for_await_of_next` | 127 | 1 | none | `iter next catch -> iter next catch r` | Calls `next()`; the following `await` waits for it. | Runs JS. | `for await` |
| `iterator_get_value_done` | 129 | 1 | none | `catch r -> catch value done` | Reads `value` and `done` from a result object; does not check that it is an object and accepts only the booleans as `done` (section 6). | | `for await` |
| `iterator_check_object` | 128 | 1 | none | `v -> v` | TypeError `iterator result is not an object`. | | `yield*` |
| `iterator_close` | 130 | 1 | none | `rec next catch ->` | IteratorClose with a normal completion: calls `return()` when the iterator is not done; its exceptions propagate, and a non-object result is a TypeError. An exception leaving a for-of loop or a destructuring pattern closes its iterator with a throw completion (the handler's stack cut, or the function's exit), dropping what `return()` raises. | Runs JS. | `break` / `return` out of for-of, end of array destructuring |
| `iterator_next` | 131 | 1 | none | `iter next catch v -> iter next catch r` | `next.call(iter, v)`. | Runs JS. | `yield*` |
| `iterator_call` | 132 | 2 | u8 flags (bit 0: `throw` else `return`; bit 1: no argument) | `iter next catch v -> iter next catch r noMethod` | Calls `iter.throw` / `iter.return` if present. | Runs JS. | `yield*` |

### 2.14 Generators and async functions

| Op | # | Size | Stack | Semantics | Emitted by |
|---|---|---|---|---|---|
| `initial_yield` | 133 | 1 | `->` (returns) | Creates the generator object (sync: state stored on it; async generator: a continuation) and returns it. Parameter initialisation runs before it, at call time. | generator functions, after the parameters |
| `yield` | 134 | 1 | `v -> sent kind` | Suspends; the caller of `next()` gets `{value: v, done: false}`. On resume pushes the sent value and the kind (0 next, 1 return); a `throw()` resume throws at this point. | `yield v` |
| `yield_star` | 135 | 1 | `r -> sent kind` | As `yield`, returning the inner iterator's result object unchanged. | `yield* e` in sync generators |
| `async_yield_star` | 136 | 1 | `v -> sent kind` | The async-generator form. | `yield* e` in async generators |
| `await` | 137 | 1 | `v -> settled` | Records the continuation and waits for `v` (1.8); the rejection reason is thrown here. Outside an async activation it leaves `v` and continues. | `await e`; async-generator plumbing |

`return_async` is in 2.6.

### 2.15 protoJS opcodes (244 to 250)

QuickJS never emits these; the loader passes of section 1.3 create them.

| Op | # | Size | Operands | Stack | Semantics | Created by |
|---|---|---|---|---|---|---|
| `proto_acc_loc8_loc8` | 244 | 4 | u8 dst, u8 src locals, u8 skip | `->` | Header in front of `s += i`: when both locals (through cells) are Numbers, stores their sum and skips `skip` bytes; otherwise falls through into the original instructions. | `specialise` |
| `proto_lt_loc8_loc8_jfalse` | 245 | 8 | u8 a, u8 b, u8 skip, i32 offset from the end | `->` | Header in front of `i < n; if_false`: with two Numbers, jumps when `!(local[a] < local[b])` and skips the original instructions otherwise; any other operand falls through into them. | `specialise` |
| `proto_lt_loc_var_jfalse` | 246 | 9 | u8 local, u16 closure var, u8 skip, i32 offset from the end | `->` | As 245 with a global on the right, read as the global object's own property; an absent or non-Number global falls through to `get_var`. | `specialise` |
| `proto_object_imm` | 247 | 1 | none | `-> obj` | A new **immutable** child of `Object.prototype`. | `markObjectLiterals` |
| `proto_define_field_last` | 248 | 5 | u32 atom | `obj v -> obj'` | Defines the field, then makes the object mutable with one clone. | `markObjectLiterals` |
| `proto_put_field_group` | 249 | 5 | u16 group, u16 position | `recv v -> v` (group active) or `recv v ->` | Position 0 checks once whether the group applies (`putFieldGroupApplies`); if it does, the value stays on the stack for the final publication; if not, `put_field`. | `markPutFieldGroups` |
| `proto_put_field_group_end` | 250 | 5 | u16 group, u16 position | `v.. recv v ->` | Active group: publishes one new version of the object with every field of the run (`setAttributes`). Otherwise `put_field`. | `markPutFieldGroups` |

## 3. Worked examples

Each example is a file in `docs/vm-examples/` and its golden listing. The
listings are regenerated with
`python3 -I docs/vm-examples/disasm_examples.py build/protojs --update` and
checked by the ctest `docs/vm-examples`. The top level of every example
calls `console.log`; its listing is shown only for example 01.

### Example 01: a constant and arithmetic, `1 + 2`

<!-- vm-example: 01-arithmetic -->
```js
console.log(1 + 2);
```

```text
<script>: args 0, locals 1, closure vars 1, stack 4
  slots:
    loc 0   <ret>
  closure vars:
    #0   console  (global)
  code:
      0  get_var                   0           ; console
      3  get_field2                            ; log
      8  push_1                                ; 1
      9  push_2                                ; 2
     10  add
     11  call_method               1           ; 1 arg
     14  set_loc0                              ; <ret>
     15  return
```

- QuickJS does not fold constants: `1 + 2` is `push_1; push_2; add`. `add`
  takes its SmallInteger fast path.
- `console` is a global the script does not declare: closure variable 0 of
  type `global`, read with `get_var 0`.
- `get_field2 log` keeps `console` on the stack as the receiver of
  `call_method 1`.
- `set_loc0 <ret>` stores the script's completion value (what `-p` prints)
  and keeps it as the return value.

### Example 02: local bindings, `let` and `const`

<!-- vm-example: 02-let-const -->
```js
function area(w, h) {
  const scale = 2;
  let a = w * h;
  a = a * scale;
  return a;
}
console.log(area(3, 4));
```

```text
<script>: args 0, locals 1, closure vars 2, stack 5
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 area
  closure vars:
    #0   area  (global decl)
    #1   console  (global)
  code:
      0  fclosure8                 0           ; function 0 area
      2  put_var_ref0                          ; area
      3  get_var                   1           ; console
      6  get_field2                            ; log
     11  get_var                   0           ; area
     14  push_3                                ; 3
     15  push_4                                ; 4
     16  call2                                 ; 2 args
     17  call_method               1           ; 1 arg
     20  set_loc0                              ; <ret>
     21  return

function 0 area: args 2, locals 2, closure vars 0, stack 2, constructor
  slots:
    arg 0   w
    arg 1   h
    loc 0   scale
    loc 1   a
  code:
      0  set_loc_uninitialized     1           ; a
      3  set_loc_uninitialized     0           ; scale
      6  push_2                                ; 2
      7  put_loc0                              ; scale
      8  get_arg0                              ; w
      9  get_arg1                              ; h
     10  mul
     11  put_loc1                              ; a
     12  get_loc_check             1           ; a
     15  get_loc_check             0           ; scale
     18  mul
     19  set_loc_check             1           ; a
     22  return
```

- The top level creates the function (`fclosure8 0`) and publishes it with
  `put_var_ref0`: a top-level function declaration is a closure variable of
  type `global decl`, and `put_var_ref` on a non-cell slot also writes the
  global binding.
- In `area`, `set_loc_uninitialized` puts `scale` and `a` in their TDZ;
  their first writes are plain `put_loc`.
- Later reads use `get_loc_check`, which raises ReferenceError while the
  slot holds the TDZ sentinel.
- `a = a * scale; return a;` ends in `set_loc_check 1; return`: QuickJS's
  peephole turned `dup; put_loc_check` into `set_loc_check`.

### Example 03: `if`, `?:` and `switch`

<!-- vm-example: 03-conditionals -->
```js
function classify(n) {
  if (n < 0) return "negative";
  else if (n === 0) return "zero";
  return n % 2 === 0 ? "even" : "odd";
}

function day(d) {
  switch (d) {
    case 0: return "Sun";
    case 6: return "Sat";
    default: return "weekday";
  }
}

console.log(classify(-1), classify(0), classify(7), day(6), day(3));
```

```text
<script>: args 0, locals 1, closure vars 3, stack 8
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 classify
    #1   function  function 1 day
  closure vars:
    #0   classify  (global decl)
    #1   day  (global decl)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 classify
      2  put_var_ref0                          ; classify
      3  fclosure8                 1           ; function 1 day
      5  put_var_ref1                          ; day
      6  get_var                   2           ; console
      9  get_field2                            ; log
     14  get_var                   0           ; classify
     17  push_minus1                           ; -1
     18  call1                                 ; 1 arg
     19  get_var                   0           ; classify
     22  push_0                                ; 0
     23  call1                                 ; 1 arg
     24  get_var                   0           ; classify
     27  push_7                                ; 7
     28  call1                                 ; 1 arg
     29  get_var                   1           ; day
     32  push_6                                ; 6
     33  call1                                 ; 1 arg
     34  get_var                   1           ; day
     37  push_3                                ; 3
     38  call1                                 ; 1 arg
     39  call_method               5           ; 5 args
     42  set_loc0                              ; <ret>
     43  return

function 0 classify: args 1, locals 0, closure vars 0, stack 2, constructor
  slots:
    arg 0   n
  code:
      0  get_arg0                              ; n
      1  push_0                                ; 0
      2  lt
      3  if_false8                 7           ; -> 11
      5  push_atom_value                       ; "negative"
     10  return
     11  get_arg0                              ; n
     12  push_0                                ; 0
     13  strict_eq
     14  if_false8                 7           ; -> 22
     16  push_atom_value                       ; "zero"
     21  return
     22  get_arg0                              ; n
     23  push_2                                ; 2
     24  mod
     25  push_0                                ; 0
     26  strict_eq
     27  if_false8                 7           ; -> 35
     29  push_atom_value                       ; "even"
     34  return
     35  push_atom_value                       ; "odd"
     40  return

function 1 day: args 1, locals 0, closure vars 0, stack 3, constructor
  slots:
    arg 0   d
  code:
      0  get_arg0                              ; d
      1  dup
      2  push_0                                ; 0
      3  strict_eq
      4  if_false8                 7           ; -> 12
      6  push_atom_value                       ; "Sun"
     11  return
     12  dup
     13  push_6                                ; 6
     14  strict_eq
     15  if_false8                 7           ; -> 23
     17  push_atom_value                       ; "Sat"
     22  return
     23  push_atom_value                       ; "weekday"
     28  return
```

- Every `if` and `?:` is a test and an `if_false8` over the then-branch; the
  short forms are used because every offset fits in a byte. `if_false8 7`
  at 3 goes to 3 + 1 + 7 = 11.
- `n === 0` is `strict_eq`.
- `switch` keeps the discriminant on the stack (`dup` before each
  `strict_eq`); a matching `case` returns, so the copy is never dropped on
  those paths.

### Example 04: a function definition and a call

<!-- vm-example: 04-function-call -->
```js
function add(a, b) {
  return a + b;
}
console.log(add(2, 3));
```

```text
<script>: args 0, locals 1, closure vars 2, stack 5
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 add
  closure vars:
    #0   add  (global decl)
    #1   console  (global)
  code:
      0  fclosure8                 0           ; function 0 add
      2  put_var_ref0                          ; add
      3  get_var                   1           ; console
      6  get_field2                            ; log
     11  get_var                   0           ; add
     14  push_2                                ; 2
     15  push_3                                ; 3
     16  call2                                 ; 2 args
     17  call_method               1           ; 1 arg
     20  set_loc0                              ; <ret>
     21  return

function 0 add: args 2, locals 0, closure vars 0, stack 2, constructor
  slots:
    arg 0   a
    arg 1   b
  code:
      0  get_arg0                              ; a
      1  get_arg1                              ; b
      2  add
      3  return
```

- `add(2, 3)` from the top level is `get_var 0` (the global `add`), two
  pushes and `call2`, the 1-byte short form of `call` with 2 arguments.
  `this` is `undefined`.
- `add` reads its parameters with `get_arg0` / `get_arg1` from slots 0 and 1
  of its own frame (a new `ProtoContext`, 1.4).

### Example 05: recursion, `fib`

<!-- vm-example: 05-fib -->
```js
function fib(n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}
console.log(fib(10));
```

```text
<script>: args 0, locals 1, closure vars 2, stack 4
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 fib
  closure vars:
    #0   fib  (global decl)
    #1   console  (global)
  code:
      0  fclosure8                 0           ; function 0 fib
      2  put_var_ref0                          ; fib
      3  get_var                   1           ; console
      6  get_field2                            ; log
     11  get_var                   0           ; fib
     14  push_i8                   10
     16  call1                                 ; 1 arg
     17  call_method               1           ; 1 arg
     20  set_loc0                              ; <ret>
     21  return

function 0 fib: args 1, locals 0, closure vars 1, stack 4, constructor
  slots:
    arg 0   n
  closure vars:
    #0   fib  (global ref)
  code:
      0  get_arg0                              ; n
      1  push_2                                ; 2
      2  lt
      3  if_false8                 3           ; -> 7
      5  get_arg0                              ; n
      6  return
      7  get_var                   0           ; fib
     10  get_arg0                              ; n
     11  push_1                                ; 1
     12  sub
     13  call1                                 ; 1 arg
     14  get_var                   0           ; fib
     17  get_arg0                              ; n
     18  push_2                                ; 2
     19  sub
     20  call1                                 ; 1 arg
     21  add
     22  return
```

- The recursive call reads `fib` as a global each time (`get_var 0`, closure
  variable `fib` of type `global ref`).
- `fib(n - 1) + fib(n - 2)` is not a tail call, so it uses `call1`; each
  call nests a native `runBytecodeImpl` frame and a `ProtoContext`. Deep
  recursion ends in RangeError `Maximum call stack size exceeded` (the native
  stack guard).
- Stack at `21 add`: `fib(n-1) fib(n-2)`.

### Example 06: loops, `for` and `while`

<!-- vm-example: 06-loops -->
```js
function sum(n) {
  let s = 0;
  for (let i = 0; i < n; i++) s += i;
  return s;
}

function countdown(n) {
  let steps = 0;
  while (n > 0) { n--; steps++; }
  return steps;
}

console.log(sum(10), countdown(3));
```

```text
<script>: args 0, locals 1, closure vars 3, stack 5
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 sum
    #1   function  function 1 countdown
  closure vars:
    #0   sum  (global decl)
    #1   countdown  (global decl)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 sum
      2  put_var_ref0                          ; sum
      3  fclosure8                 1           ; function 1 countdown
      5  put_var_ref1                          ; countdown
      6  get_var                   2           ; console
      9  get_field2                            ; log
     14  get_var                   0           ; sum
     17  push_i8                   10
     19  call1                                 ; 1 arg
     20  get_var                   1           ; countdown
     23  push_3                                ; 3
     24  call1                                 ; 1 arg
     25  call_method               2           ; 2 args
     28  set_loc0                              ; <ret>
     29  return

function 0 sum: args 1, locals 2, closure vars 0, stack 2, constructor
  slots:
    arg 0   n
    loc 0   s
    loc 1   i
  code:
      0  set_loc_uninitialized     0           ; s
      3  push_0                                ; 0
      4  put_loc0                              ; s
      5  set_loc_uninitialized     1           ; i
      8  push_0                                ; 0
      9  put_loc1                              ; i
     10  get_loc_check             1           ; i
     13  get_arg0                              ; n
     14  lt
     15  if_false8                 25          ; -> 41
     17  proto_acc_loc8_loc8       0 1 10      ; s += i; fast path skips to 31
     21  get_loc_check             0           ; s
     24  get_loc_check             1           ; i
     27  add
     28  put_loc_check             0           ; s
     31  get_loc_check             1           ; i
     34  post_inc
     35  put_loc_check             1           ; i
     38  drop
     39  goto8                     -30         ; -> 10
     41  get_loc_check             0           ; s
     44  return

function 1 countdown: args 1, locals 1, closure vars 0, stack 2, constructor
  slots:
    arg 0   n
    loc 0   steps
  code:
      0  set_loc_uninitialized     0           ; steps
      3  push_0                                ; 0
      4  put_loc0                              ; steps
      5  get_arg0                              ; n
      6  push_0                                ; 0
      7  gt
      8  if_false8                 14          ; -> 23
     10  get_arg0                              ; n
     11  dec
     12  put_arg0                              ; n
     13  get_loc_check             0           ; steps
     16  post_inc
     17  put_loc_check             0           ; steps
     20  drop
     21  goto8                     -17         ; -> 5
     23  get_loc_check             0           ; steps
     26  return
```

- `s += i` got the header `proto_acc_loc8_loc8 0 1 10` (the specialiser,
  1.3) in front of QuickJS's `get_loc_check s; get_loc_check i; add;
  put_loc_check s` (10 bytes). With two Numbers the header does the
  addition and skips the 10 bytes; otherwise those instructions run.
  The loop test `i < n` compares a local with an *argument*, which no fused
  pattern covers, so it stays `get_loc_check; get_arg0; lt; if_false8`.
- The jumps were re-encoded for the 4 inserted bytes: `goto8 -30` at 39
  goes back to the test at 10, and `if_false8` at 15 leaves the loop at 41.
- `i++` is `get_loc_check; post_inc; put_loc_check; drop`: `post_inc` pushes
  the old and the new value.
- In `countdown`, `n--` on a parameter is `get_arg0; dec; put_arg0`, and
  `steps++` is the same `post_inc` sequence as `i++`.
- There is no safepoint opcode: the backward jumps reach the GC poll through
  the dispatch counter (1.6).

### Example 07: a closure capturing a local

<!-- vm-example: 07-closure -->
```js
function adder(n) {
  return (x) => x + n;
}
const add5 = adder(5);
console.log(add5(1));
```

```text
<script>: args 0, locals 1, closure vars 3, stack 4
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 adder
  closure vars:
    #0   adder  (global decl)
    #1   add5  (global decl, const)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 adder
      2  put_var_ref0                          ; adder
      3  get_var                   0           ; adder
      6  push_5                                ; 5
      7  call1                                 ; 1 arg
      8  put_var_init              1           ; add5
     11  get_var                   2           ; console
     14  get_field2                            ; log
     19  get_var                   1           ; add5
     22  push_1                                ; 1
     23  call1                                 ; 1 arg
     24  call_method               1           ; 1 arg
     27  set_loc0                              ; <ret>
     28  return

function 0 adder: args 1, locals 0, closure vars 0, stack 1, constructor
  slots:
    arg 0   n
  constants:
    #0   function  function 0.0 <anonymous>
  code:
      0  fclosure8                 0           ; function 0.0 <anonymous>
      2  return

function 0.0 <anonymous>: args 1, locals 0, closure vars 1, stack 2, arrow
  slots:
    arg 0   x
  closure vars:
    #0   n  (parent arg 0)
  code:
      0  get_arg0                              ; x
      1  get_var_ref0                          ; n
      2  add
      3  return
```

- `adder` creates the arrow with `fclosure8 0`. The arrow's closure variable
  `n` is `parent arg 0`: `fclosure` promotes `adder`'s argument slot 0 to a
  cell and stores it on the arrow's capture scope (1.4).
- When the arrow is called, the cell is copied into its closure slot 0, and
  `get_var_ref0` reads the cell's value.
- `return (x) => x + n` is a plain `return`: creating a closure is not a call.

### Example 08: default parameters

<!-- vm-example: 08-default-params -->
```js
function greet(name, greeting = "Hello") {
  return greeting + ", " + name;
}
console.log(greet("Ada"), greet("Alan", "Hi"));
```

```text
<script>: args 0, locals 1, closure vars 2, stack 6
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 greet
  closure vars:
    #0   greet  (global decl)
    #1   console  (global)
  code:
      0  fclosure8                 0           ; function 0 greet
      2  put_var_ref0                          ; greet
      3  get_var                   1           ; console
      6  get_field2                            ; log
     11  get_var                   0           ; greet
     14  push_atom_value                       ; "Ada"
     19  call1                                 ; 1 arg
     20  get_var                   0           ; greet
     23  push_atom_value                       ; "Alan"
     28  push_atom_value                       ; "Hi"
     33  call2                                 ; 2 args
     34  call_method               2           ; 2 args
     37  set_loc0                              ; <ret>
     38  return

function 0 greet: args 2, locals 2, closure vars 0, stack 2, constructor
  slots:
    arg 0   name
    arg 1   greeting
    loc 0   name
    loc 1   greeting
  code:
      0  set_loc_uninitialized     1           ; greeting
      3  set_loc_uninitialized     0           ; name
      6  get_arg0                              ; name
      7  put_loc0                              ; name
      8  get_arg1                              ; greeting
      9  dup
     10  is_undefined
     11  if_false8                 8           ; -> 20
     13  drop
     14  push_atom_value                       ; "Hello"
     19  set_arg1                              ; greeting
     20  put_loc1                              ; greeting
     21  get_arg1                              ; greeting
     22  push_atom_value                       ; ", "
     27  add
     28  get_arg0                              ; name
     29  add
     30  return
```

- With a default value present, QuickJS gives the parameters a separate
  scope: `name` and `greeting` exist as arguments (slots `arg 0`, `arg 1`)
  and again as locals, which the prologue initialises from the arguments.
- The default is `get_arg1; dup; is_undefined; if_false8`: only `undefined`
  (missing or explicit) takes the default, which `set_arg1` stores back into
  the argument.
- The body then reads the arguments (`get_arg1`, `get_arg0`).

### Example 09: destructuring

<!-- vm-example: 09-destructuring -->
```js
function demo(pair, opts) {
  const [a, b] = pair;
  const { x, y = 10 } = opts;
  return a + b + x + y;
}
console.log(demo([1, 2], { x: 3 }));
```

```text
<script>: args 0, locals 1, closure vars 2, stack 6
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 demo
  closure vars:
    #0   demo  (global decl)
    #1   console  (global)
  code:
      0  fclosure8                 0           ; function 0 demo
      2  put_var_ref0                          ; demo
      3  get_var                   1           ; console
      6  get_field2                            ; log
     11  get_var                   0           ; demo
     14  push_1                                ; 1
     15  push_2                                ; 2
     16  array_from                2           ; 2 elements
     19  proto_object_imm
     20  push_3                                ; 3
     21  proto_define_field_last               ; x
     26  call2                                 ; 2 args
     27  call_method               1           ; 1 arg
     30  set_loc0                              ; <ret>
     31  return

function 0 demo: args 2, locals 4, closure vars 0, stack 5, constructor
  slots:
    arg 0   pair
    arg 1   opts
    loc 0   a
    loc 1   b
    loc 2   x
    loc 3   y
  code:
      0  set_loc_uninitialized     3           ; y
      3  set_loc_uninitialized     2           ; x
      6  set_loc_uninitialized     1           ; b
      9  set_loc_uninitialized     0           ; a
     12  undefined
     13  dup
     14  is_undefined
     15  if_true8                  13          ; -> 29
     17  for_of_start
     18  for_of_next               0           ; depth 0
     20  drop
     21  put_loc0                              ; a
     22  for_of_next               0           ; depth 0
     24  drop
     25  put_loc1                              ; b
     26  iterator_close
     27  goto8                     5           ; -> 33
     29  drop
     30  get_arg0                              ; pair
     31  goto8                     -15         ; -> 17
     33  undefined
     34  dup
     35  is_undefined
     36  if_true8                  24          ; -> 61
     38  to_object
     39  get_field2                            ; x
     44  put_loc2                              ; x
     45  get_field2                            ; y
     50  dup
     51  is_undefined
     52  if_false8                 4           ; -> 57
     54  drop
     55  push_i8                   10
     57  put_loc3                              ; y
     58  drop
     59  goto8                     5           ; -> 65
     61  drop
     62  get_arg1                              ; opts
     63  goto8                     -26         ; -> 38
     65  get_loc_check             0           ; a
     68  get_loc_check             1           ; b
     71  add
     72  get_loc_check             2           ; x
     75  add
     76  get_loc_check             3           ; y
     79  add
     80  return
```

- QuickJS compiles each destructuring declaration with a default-value test
  on a constant `undefined` (`undefined; dup; is_undefined; if_true8`), which
  always jumps: the code at 29 and 61 drops it, pushes the real value
  (`get_arg0 pair`, `get_arg1 opts`) and jumps back to the pattern.
- Array destructuring is iteration: `for_of_start` on `pair`, one
  `for_of_next 0` per element (it pushes `value done`; `drop` discards
  `done`), and `iterator_close`.
- Object destructuring is `to_object` (TypeError on `null` / `undefined`) and
  one `get_field2` per property; the default `y = 10` is
  `dup; is_undefined; if_false8; drop; push_i8 10`.

### Example 10: keyed access, `o.k` and `o[k]`

<!-- vm-example: 10-property-access -->
```js
function read(o, k) {
  return o.name + o[k];
}
function write(o, k, v) {
  o.count = 1;
  o[k] = v;
  return o;
}
console.log(read({ name: "a", b: "c" }, "b"), write({}, "z", 2).z);
```

```text
<script>: args 0, locals 1, closure vars 3, stack 7
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 read
    #1   function  function 1 write
  closure vars:
    #0   read  (global decl)
    #1   write  (global decl)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 read
      2  put_var_ref0                          ; read
      3  fclosure8                 1           ; function 1 write
      5  put_var_ref1                          ; write
      6  get_var                   2           ; console
      9  get_field2                            ; log
     14  get_var                   0           ; read
     17  proto_object_imm
     18  push_atom_value                       ; "a"
     23  define_field                          ; name
     28  push_atom_value                       ; "c"
     33  proto_define_field_last               ; b
     38  push_atom_value                       ; "b"
     43  call2                                 ; 2 args
     44  get_var                   1           ; write
     47  object
     48  push_atom_value                       ; "z"
     53  push_2                                ; 2
     54  call3                                 ; 3 args
     55  get_field                             ; z
     60  call_method               2           ; 2 args
     63  set_loc0                              ; <ret>
     64  return

function 0 read: args 2, locals 0, closure vars 0, stack 3, constructor
  slots:
    arg 0   o
    arg 1   k
  code:
      0  get_arg0                              ; o
      1  get_field                             ; name
      6  get_arg0                              ; o
      7  get_arg1                              ; k
      8  get_array_el
      9  add
     10  return

function 1 write: args 3, locals 0, closure vars 0, stack 3, constructor
  slots:
    arg 0   o
    arg 1   k
    arg 2   v
  code:
      0  get_arg0                              ; o
      1  push_1                                ; 1
      2  put_field                             ; count
      7  get_arg0                              ; o
      8  get_arg1                              ; k
      9  get_arg2                              ; v
     10  put_array_el
     11  get_arg0                              ; o
     12  return
```

- A static name is an atom operand: `get_field name`, `put_field count`.
- A computed key is on the stack: `get_array_el` (read) and `put_array_el`
  (write).
- `put_field` creates a new version of `o`'s attributes and makes the
  mutable object refer to it: JavaScript objects are mutable protoCore
  objects over immutable attribute versions.

### Example 11: built-in calls, `arr.length` and `Math.max`

<!-- vm-example: 11-builtin-call -->
```js
function stats(arr) {
  return [arr.length, Math.max(arr[0], arr[1])];
}
console.log(stats([3, 9]));
```

```text
<script>: args 0, locals 1, closure vars 3, stack 5
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 stats
  closure vars:
    #0   stats  (global decl)
    #1   Math  (global)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 stats
      2  put_var_ref0                          ; stats
      3  get_var                   2           ; console
      6  get_field2                            ; log
     11  get_var                   0           ; stats
     14  push_3                                ; 3
     15  push_i8                   9
     17  array_from                2           ; 2 elements
     20  call1                                 ; 1 arg
     21  call_method               1           ; 1 arg
     24  set_loc0                              ; <ret>
     25  return

function 0 stats: args 1, locals 0, closure vars 1, stack 6, constructor
  slots:
    arg 0   arr
  closure vars:
    #0   Math  (global ref)
  code:
      0  get_arg0                              ; arr
      1  get_length
      2  get_var                   0           ; Math
      5  get_field2                            ; max
     10  get_arg0                              ; arr
     11  push_0                                ; 0
     12  get_array_el
     13  get_arg0                              ; arr
     14  push_1                                ; 1
     15  get_array_el
     16  call_method               2           ; 2 args
     19  array_from                2           ; 2 elements
     22  return
```

- `arr.length` is the dedicated `get_length`, the array's element count.
- `Math.max(...)` is a method call on the global `Math`: `get_var 0`,
  `get_field2 max`, the arguments, `call_method 2`. `Math.max` is a native
  method: it runs in this frame's `ProtoContext`, with the arguments read
  from this frame's operand stack (1.5, 5).
- The array literal is `array_from 2`.

### Example 12: a higher-order call, `reduce` with a callback

<!-- vm-example: 12-reduce -->
```js
function total(xs) {
  return xs.reduce((acc, x) => acc + x, 0);
}
console.log(total([1, 2, 3]));
```

```text
<script>: args 0, locals 1, closure vars 2, stack 6
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 total
  closure vars:
    #0   total  (global decl)
    #1   console  (global)
  code:
      0  fclosure8                 0           ; function 0 total
      2  put_var_ref0                          ; total
      3  get_var                   1           ; console
      6  get_field2                            ; log
     11  get_var                   0           ; total
     14  push_1                                ; 1
     15  push_2                                ; 2
     16  push_3                                ; 3
     17  array_from                3           ; 3 elements
     20  call1                                 ; 1 arg
     21  call_method               1           ; 1 arg
     24  set_loc0                              ; <ret>
     25  return

function 0 total: args 1, locals 0, closure vars 0, stack 4, constructor
  slots:
    arg 0   xs
  constants:
    #0   function  function 0.0 <anonymous>
  code:
      0  get_arg0                              ; xs
      1  get_field2                            ; reduce
      6  fclosure8                 0           ; function 0.0 <anonymous>
      8  push_0                                ; 0
      9  tail_call_method          2           ; 2 args

function 0.0 <anonymous>: args 2, locals 0, closure vars 0, stack 2, arrow
  slots:
    arg 0   acc
    arg 1   x
  code:
      0  get_arg0                              ; acc
      1  get_arg1                              ; x
      2  add
      3  return
```

- `return xs.reduce(...)` is `tail_call_method 2`: QuickJS's peephole turned
  `call_method; return` into the tail form. protoJS does not reuse the frame;
  it calls and returns the result.
- `Array.prototype.reduce` is native. It calls the arrow back through
  `callJSFunction`, which creates a `ProtoContext` for each activation of the
  arrow (the arrow is interpreted code); the native itself runs in `total`'s
  context.

### Example 13: `try` / `catch` / `finally` / `throw`

<!-- vm-example: 13-try-catch -->
```js
function safeDiv(a, b) {
  try {
    if (b === 0) throw new Error("division by zero");
    return a / b;
  } catch (e) {
    return e.message;
  } finally {
    console.log("done");
  }
}
console.log(safeDiv(6, 3), safeDiv(1, 0));
```

```text
<script>: args 0, locals 1, closure vars 3, stack 6
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 safeDiv
  closure vars:
    #0   safeDiv  (global decl)
    #1   Error  (global)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 safeDiv
      2  put_var_ref0                          ; safeDiv
      3  get_var                   2           ; console
      6  get_field2                            ; log
     11  get_var                   0           ; safeDiv
     14  push_6                                ; 6
     15  push_3                                ; 3
     16  call2                                 ; 2 args
     17  get_var                   0           ; safeDiv
     20  push_1                                ; 1
     21  push_0                                ; 0
     22  call2                                 ; 2 args
     23  call_method               2           ; 2 args
     26  set_loc0                              ; <ret>
     27  return

function 0 safeDiv: args 2, locals 1, closure vars 2, stack 5, constructor
  slots:
    arg 0   a
    arg 1   b
    loc 0   e
  closure vars:
    #0   Error  (global ref)
    #1   console  (global ref)
  code:
      0  catch                     32          ; -> 33
      5  get_arg1                              ; b
      6  push_0                                ; 0
      7  strict_eq
      8  if_false8                 14          ; -> 23
     10  get_var                   0           ; Error
     13  dup
     14  push_atom_value                       ; "division by zero"
     19  call_constructor          1           ; 1 arg
     22  throw
     23  get_arg0                              ; a
     24  get_arg1                              ; b
     25  div
     26  nip_catch
     27  gosub                     30          ; -> 58
     32  return
     33  put_loc0                              ; e
     34  catch                     17          ; -> 52
     39  get_loc0                              ; e
     40  get_field                             ; message
     45  nip_catch
     46  gosub                     11          ; -> 58
     51  return
     52  gosub                     5           ; -> 58
     57  throw
     58  get_var                   1           ; console
     61  get_field2                            ; log
     66  push_atom_value                       ; "done"
     71  call_method               1           ; 1 arg
     74  drop
     75  ret
```

- `catch 32` at 0 opens the protected range; its handler is at 33. The range
  runs as a nested level of the dispatch loop (1.7).
- `new Error(...)` is `get_var Error; dup; push_atom_value;
  call_constructor 1`: the constructor is pushed twice, as the function and
  as `new.target`.
- `return a / b` inside the `try`: `nip_catch` closes the range keeping the
  result, `gosub 30` runs the `finally` block at 58, then `return`.
- The handler at 33 stores the exception in `e` and opens a second range
  (`catch 17`) for the catch body, because the `finally` must also run if
  the catch body throws. That range's handler, at 52, is
  `gosub 5` (the `finally`) then `throw` (rethrow).
- The `finally` block (58 to 75) ends with `ret`, which pops the offset that
  `gosub` pushed.

### Example 14: desugaring, classes and `async`

<!-- vm-example: 14-class-async -->
```js
class Point {
  constructor(x, y) { this.x = x; this.y = y; }
  norm1() { return Math.abs(this.x) + Math.abs(this.y); }
}

class Point3 extends Point {
  constructor(x, y, z) { super(x, y); this.z = z; }
}

async function later(v) {
  const r = await v;
  return r + 1;
}

later(new Point3(1, -2, 3).norm1()).then((v) => console.log(v));
```

```text
<script>: args 0, locals 5, closure vars 5, stack 6
  slots:
    loc 0   <ret>
    loc 1   Point
    loc 2   <class_fields_init>
    loc 3   Point3
    loc 4   <class_fields_init>
  constants:
    #0   function  function 0 <anonymous>
    #1   function  function 1 <anonymous>
    #2   function  function 2 <anonymous>
    #3   function  function 3 later
    #4   function  function 4 <anonymous>
  closure vars:
    #0   Point  (global decl, lexical)
    #1   Point3  (global decl, lexical)
    #2   later  (global decl)
    #3   Math  (global)
    #4   console  (global)
  code:
      0  fclosure8                 3           ; function 3 later
      2  put_var_ref2                          ; later
      3  set_loc_uninitialized     1           ; Point
      6  undefined
      7  set_loc_uninitialized     2           ; <class_fields_init>
     10  fclosure8                 0           ; function 0 <anonymous>
     12  define_class              0           ; Point
     18  fclosure8                 1           ; function 1 <anonymous>
     20  define_method             0           ; norm1
     26  undefined
     27  put_loc2                              ; <class_fields_init>
     28  drop
     29  set_loc1                              ; Point
     30  close_loc                 2           ; <class_fields_init>
     33  put_var_init              0           ; Point
     36  set_loc_uninitialized     3           ; Point3
     39  get_var                   0           ; Point
     42  set_loc_uninitialized     4           ; <class_fields_init>
     45  fclosure8                 2           ; function 2 <anonymous>
     47  define_class              1           ; Point3
     53  undefined
     54  put_loc8                  4           ; <class_fields_init>
     56  drop
     57  set_loc3                              ; Point3
     58  close_loc                 4           ; <class_fields_init>
     61  put_var_init              1           ; Point3
     64  get_var                   2           ; later
     67  get_var                   1           ; Point3
     70  dup
     71  push_1                                ; 1
     72  push_i8                   -2
     74  push_3                                ; 3
     75  call_constructor          3           ; 3 args
     78  get_field2                            ; norm1
     83  call_method               0           ; 0 args
     86  call1                                 ; 1 arg
     87  get_field2                            ; then
     92  fclosure8                 4           ; function 4 <anonymous>
     94  call_method               1           ; 1 arg
     97  set_loc0                              ; <ret>
     98  return

function 0 <anonymous>: args 2, locals 1, closure vars 1, stack 3, strict, constructor
  slots:
    arg 0   x
    arg 1   y
    loc 0   this
  closure vars:
    #0   <class_fields_init>  (parent local 2, const)
  code:
      0  push_this
      1  put_loc0                              ; this
      2  check_ctor
      3  get_var_ref_check         0           ; <class_fields_init>
      6  dup
      7  if_false8                 6           ; -> 14
      9  get_loc0                              ; this
     10  swap
     11  call_method               0           ; 0 args
     14  drop
     15  get_loc0                              ; this
     16  get_arg0                              ; x
     17  proto_put_field_group     0 0         ; x, write 1 of 2
     22  get_loc0                              ; this
     23  get_arg1                              ; y
     24  proto_put_field_group_end 0 1         ; y, write 2 of 2
     29  return_undef

function 1 <anonymous>: args 0, locals 1, closure vars 1, stack 4, strict
  slots:
    loc 0   this
  closure vars:
    #0   Math  (global ref)
  code:
      0  push_this
      1  put_loc0                              ; this
      2  get_var                   0           ; Math
      5  get_field2                            ; abs
     10  get_loc0                              ; this
     11  get_field                             ; x
     16  call_method               1           ; 1 arg
     19  get_var                   0           ; Math
     22  get_field2                            ; abs
     27  get_loc0                              ; this
     28  get_field                             ; y
     33  call_method               1           ; 1 arg
     36  add
     37  return

function 2 <anonymous>: args 3, locals 3, closure vars 1, stack 4, strict, constructor
  slots:
    arg 0   x
    arg 1   y
    arg 2   z
    loc 0   this.active_func
    loc 1   new.target
    loc 2   this
  closure vars:
    #0   <class_fields_init>  (parent local 4, const)
  code:
      0  special_object            2           ; this function
      2  put_loc0                              ; this.active_func
      3  special_object            3           ; new.target
      5  put_loc1                              ; new.target
      6  set_loc_uninitialized     2           ; this
      9  check_ctor
     10  get_loc0                              ; this.active_func
     11  get_super
     12  get_loc1                              ; new.target
     13  get_arg0                              ; x
     14  get_arg1                              ; y
     15  call_constructor          2           ; 2 args
     18  dup
     19  put_loc_check_init        2           ; this
     22  get_var_ref_check         0           ; <class_fields_init>
     25  dup
     26  if_false8                 8           ; -> 35
     28  get_loc_check             2           ; this
     31  swap
     32  call_method               0           ; 0 args
     35  drop
     36  drop
     37  get_loc_check             2           ; this
     40  get_arg2                              ; z
     41  put_field                             ; z
     46  get_loc_checkthis         2           ; this
     49  return

function 3 later: args 1, locals 1, closure vars 0, stack 2, async
  slots:
    arg 0   v
    loc 0   r
  code:
      0  set_loc_uninitialized     0           ; r
      3  get_arg0                              ; v
      4  await
      5  put_loc0                              ; r
      6  get_loc_check             0           ; r
      9  push_1                                ; 1
     10  add
     11  return_async

function 4 <anonymous>: args 1, locals 0, closure vars 1, stack 3, arrow
  slots:
    arg 0   v
  closure vars:
    #0   console  (global ref)
  code:
      0  get_var                   0           ; console
      3  get_field2                            ; log
      8  get_arg0                              ; v
      9  tail_call_method          1           ; 1 arg
```

- A class is a constructor function plus `define_class`. The loader rewrote
  `push_const8 0` before `define_class` into `fclosure8 0` (1.3). The class
  name `Point` is the atom of `define_class`; the constructor function itself
  has no name in QuickJS's metadata, hence `<anonymous>`.
- Methods are `fclosure8` + `define_method`.
- `Point`'s constructor starts with `push_this; put_loc0` and `check_ctor`,
  then runs the field initialiser (`<class_fields_init>`, `undefined` here).
  `this.x = x; this.y = y;` became a write group (`proto_put_field_group` /
  `_end`, 1.3): one new version of the object for both fields.
- `Point3`'s constructor keeps `this` in its TDZ until `super(x, y)`:
  `get_super` on the active function gives `Point`, `call_constructor 2` runs
  it with `new.target`, and `put_loc_check_init` initialises `this`. It ends
  with `get_loc_checkthis`. Its `this.z = z` is a single write, so it stays a
  `put_field`.
- `later` is `async`: `await` suspends the activation (1.8) and
  `return_async` hands the result to the code that settles the promise.
- The arrow passed to `then` ends in a tail call (`tail_call_method 1`).

### Example 15: generators

<!-- vm-example: 15-generator -->
```js
function* range(n) {
  for (let i = 0; i < n; i++) yield i;
}
let s = 0;
for (const v of range(4)) s += v;
console.log(s);
```

```text
<script>: args 0, locals 2, closure vars 3, stack 5
  slots:
    loc 0   <ret>
    loc 1   v
  constants:
    #0   function  function 0 range
  closure vars:
    #0   range  (global decl)
    #1   s  (global decl, lexical)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 range
      2  put_var_ref0                          ; range
      3  push_0                                ; 0
      4  put_var_init              1           ; s
      7  undefined
      8  put_loc0                              ; <ret>
      9  set_loc_uninitialized     1           ; v
     12  get_var                   0           ; range
     15  push_4                                ; 4
     16  call1                                 ; 1 arg
     17  for_of_start
     18  goto8                     14          ; -> 33
     20  put_loc1                              ; v
     21  get_var                   1           ; s
     24  get_loc_check             1           ; v
     27  add
     28  dup
     29  put_var                   1           ; s
     32  put_loc0                              ; <ret>
     33  for_of_next               0           ; depth 0
     35  if_false8                 -16         ; -> 20
     37  drop
     38  iterator_close
     39  get_var                   2           ; console
     42  get_field2                            ; log
     47  get_var                   1           ; s
     50  call_method               1           ; 1 arg
     53  set_loc0                              ; <ret>
     54  return

function 0 range: args 1, locals 1, closure vars 0, stack 2, generator
  slots:
    arg 0   n
    loc 0   i
  code:
      0  initial_yield
      1  set_loc_uninitialized     0           ; i
      4  push_0                                ; 0
      5  put_loc0                              ; i
      6  get_loc_check             0           ; i
      9  get_arg0                              ; n
     10  lt
     11  if_false8                 19          ; -> 31
     13  get_loc_check             0           ; i
     16  yield
     17  if_false8                 2           ; -> 20
     19  return_async
     20  drop
     21  get_loc_check             0           ; i
     24  post_inc
     25  put_loc_check             0           ; i
     28  drop
     29  goto8                     -24         ; -> 6
     31  undefined
     32  return_async
```

- `range` starts with `initial_yield`: calling it returns the generator
  object before the body runs.
- `yield` (at 16) suspends; when `next()` resumes it, the stack holds the sent
  value and the resume kind. `if_false8` tests the kind: 0 (`next`) continues
  at 20, where `drop` discards the sent value; 1 (`return`) runs
  `return_async` with the sent value.
- The top-level `for (const v of range(4))` is `for_of_start`, then
  `for_of_next 0` at the loop head and `if_false8` on `done`; after the loop,
  `drop` and `iterator_close`. Because `s` is a top-level `let`, `s += v` is
  `get_var` / `put_var` on the global binding, not a fused local.

### Example 16: shared mutable state, an object property

<!-- vm-example: 16-shared-state -->
```js
const counter = { hits: 0 };
function hit() {
  counter.hits++;
  return counter.hits;
}
hit();
console.log(hit());
```

```text
<script>: args 0, locals 1, closure vars 3, stack 3
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 hit
  closure vars:
    #0   counter  (global decl, const)
    #1   hit  (global decl)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 hit
      2  put_var_ref1                          ; hit
      3  proto_object_imm
      4  push_0                                ; 0
      5  proto_define_field_last               ; hits
     10  put_var_init              0           ; counter
     13  get_var                   1           ; hit
     16  call0                                 ; 0 args
     17  put_loc0                              ; <ret>
     18  get_var                   2           ; console
     21  get_field2                            ; log
     26  get_var                   1           ; hit
     29  call0                                 ; 0 args
     30  call_method               1           ; 1 arg
     33  set_loc0                              ; <ret>
     34  return

function 0 hit: args 0, locals 0, closure vars 1, stack 2, constructor
  closure vars:
    #0   counter  (global ref, const)
  code:
      0  get_var                   0           ; counter
      3  get_field2                            ; hits
      8  inc
      9  put_field                             ; hits
     14  get_var                   0           ; counter
     17  get_field                             ; hits
     22  return
```

- `counter.hits++` is `get_field2 hits; inc; put_field hits`: read, add, and
  write a new version of `counter`.
- `counter` is a top-level `const`: closure variable 0 of type `global ref,
  const`. Assigning to it would raise TypeError in `put_var`.
- The read and the write are separate instructions: the increment is not
  atomic with respect to other threads that write the same property.

### Example 17: spread and optional chaining

<!-- vm-example: 17-spread-optional -->
```js
function f(a, b, c) { return a + b + c; }
function g(o, xs) {
  const len = o?.items?.length ?? 0;
  return f(...xs) + len;
}
console.log(g({ items: [1, 2] }, [1, 2, 3]), g(null, [0, 0, 0]));
```

```text
<script>: args 0, locals 1, closure vars 3, stack 8
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 f
    #1   function  function 1 g
  closure vars:
    #0   f  (global decl)
    #1   g  (global decl)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 f
      2  put_var_ref0                          ; f
      3  fclosure8                 1           ; function 1 g
      5  put_var_ref1                          ; g
      6  get_var                   2           ; console
      9  get_field2                            ; log
     14  get_var                   1           ; g
     17  proto_object_imm
     18  push_1                                ; 1
     19  push_2                                ; 2
     20  array_from                2           ; 2 elements
     23  proto_define_field_last               ; items
     28  push_1                                ; 1
     29  push_2                                ; 2
     30  push_3                                ; 3
     31  array_from                3           ; 3 elements
     34  call2                                 ; 2 args
     35  get_var                   1           ; g
     38  null
     39  push_0                                ; 0
     40  push_0                                ; 0
     41  push_0                                ; 0
     42  array_from                3           ; 3 elements
     45  call2                                 ; 2 args
     46  call_method               2           ; 2 args
     49  set_loc0                              ; <ret>
     50  return

function 0 f: args 3, locals 0, closure vars 0, stack 2, constructor
  slots:
    arg 0   a
    arg 1   b
    arg 2   c
  code:
      0  get_arg0                              ; a
      1  get_arg1                              ; b
      2  add
      3  get_arg2                              ; c
      4  add
      5  return

function 1 g: args 2, locals 1, closure vars 1, stack 4, constructor
  slots:
    arg 0   o
    arg 1   xs
    loc 0   len
  closure vars:
    #0   f  (global ref)
  code:
      0  set_loc_uninitialized     0           ; len
      3  get_arg0                              ; o
      4  dup
      5  is_undefined_or_null
      6  if_false8                 5           ; -> 12
      8  drop
      9  undefined
     10  goto8                     15          ; -> 26
     12  get_field                             ; items
     17  dup
     18  is_undefined_or_null
     19  if_false8                 5           ; -> 25
     21  drop
     22  undefined
     23  goto8                     2           ; -> 26
     25  get_length
     26  dup
     27  is_undefined_or_null
     28  if_false8                 3           ; -> 32
     30  drop
     31  push_0                                ; 0
     32  put_loc0                              ; len
     33  get_var                   0           ; f
     36  array_from                0           ; 0 elements
     39  push_0                                ; 0
     40  get_arg1                              ; xs
     41  append
     42  drop
     43  undefined
     44  swap
     45  apply                     0           ; call
     48  get_loc_check             0           ; len
     51  add
     52  return
```

- `o?.items?.length ?? 0`: each `?.` is `dup; is_undefined_or_null;
  if_false8`, short-circuiting to `undefined`; `??` is the same test, taking
  `push_0` on `null` / `undefined`.
- `f(...xs)` builds the argument array (`array_from 0`, then `append` of
  `xs` at position 0), pushes `undefined` as `this`, and calls with
  `apply 0` (magic 0: a call, not a construction).

### Example 18: protoJS rewrites, object literals and write groups

<!-- vm-example: 18-object-literal-write-group -->
```js
function make(x, y) {
  return { x: x, y: y, sum: x + y };
}
function Vec(x, y) {
  this.x = x;
  this.y = y;
}
console.log(make(1, 2).sum, new Vec(3, 4).y);
```

```text
<script>: args 0, locals 1, closure vars 3, stack 7
  slots:
    loc 0   <ret>
  constants:
    #0   function  function 0 make
    #1   function  function 1 Vec
  closure vars:
    #0   make  (global decl)
    #1   Vec  (global decl)
    #2   console  (global)
  code:
      0  fclosure8                 0           ; function 0 make
      2  put_var_ref0                          ; make
      3  fclosure8                 1           ; function 1 Vec
      5  put_var_ref1                          ; Vec
      6  get_var                   2           ; console
      9  get_field2                            ; log
     14  get_var                   0           ; make
     17  push_1                                ; 1
     18  push_2                                ; 2
     19  call2                                 ; 2 args
     20  get_field                             ; sum
     25  get_var                   1           ; Vec
     28  dup
     29  push_3                                ; 3
     30  push_4                                ; 4
     31  call_constructor          2           ; 2 args
     34  get_field                             ; y
     39  call_method               2           ; 2 args
     42  set_loc0                              ; <ret>
     43  return

function 0 make: args 2, locals 0, closure vars 0, stack 3, constructor
  slots:
    arg 0   x
    arg 1   y
  code:
      0  proto_object_imm
      1  get_arg0                              ; x
      2  define_field                          ; x
      7  get_arg1                              ; y
      8  define_field                          ; y
     13  get_arg0                              ; x
     14  get_arg1                              ; y
     15  add
     16  proto_define_field_last               ; sum
     21  return

function 1 Vec: args 2, locals 1, closure vars 0, stack 3, constructor
  slots:
    arg 0   x
    arg 1   y
    loc 0   this
  code:
      0  push_this
      1  put_loc0                              ; this
      2  get_loc0                              ; this
      3  get_arg0                              ; x
      4  proto_put_field_group     0 0         ; x, write 1 of 2
      9  get_loc0                              ; this
     10  get_arg1                              ; y
     11  proto_put_field_group_end 0 1         ; y, write 2 of 2
     16  return_undef
```

- `make`'s literal `{x, y, sum}` starts as `proto_object_imm` (an immutable
  object), takes `x` and `y` with ordinary `define_field` by structural
  sharing, and the last field `sum` with `proto_define_field_last`, which
  makes it mutable with one clone (1.3).
- `Vec`'s `this.x = x; this.y = y;` is a write group: `proto_put_field_group
  0 0` checks the group once and keeps `x` on the stack;
  `proto_put_field_group_end 0 1` publishes both fields in one
  `setAttributes`.

## 4. How to add an opcode

QuickJS's opcodes are fixed: their numbering comes from
`quickjs-opcode.h`, and adding one means changing the vendored compiler. A
protoJS opcode is added the way 244 to 250 were: a free byte value (251 to
255 today), produced by a loader pass from QuickJS's output. The model here
is `proto_put_field_group` (249). Every place to touch, in order:

1. **The value**, in `src/runtime/BytecodeSpecialiser.h` (shared constants)
   or `BytecodeSpecialiser.cpp` (private to the specialiser), with a comment
   giving its operands and stack effect:
   ```cpp
   constexpr uint8_t OP_PROTO_PUT_FIELD_GROUP = 249;
   ```
2. **The size table**: `getOpcodeSizes` in `BytecodeSpecialiser.cpp`,
   which the loader and the disassembler share (`bytecodeOpcodeSizes`). A
   missing size (0) stops
   every walker at that byte: the loader stops resolving atoms, the
   specialiser leaves the function alone, and `forOfStartPcs` is cleared.
3. **The pass that emits it**, in `BytecodeSpecialiser.cpp`, called from
   `loadBytecodeRecursive`. A pass that keeps instruction sizes (as
   `markPutFieldGroups` does: `put_field` and the new opcode are both 5
   bytes) needs no jump fix-up. A pass that changes sizes must remap jumps
   as the specialiser does, including `catch`, `gosub` and the `with_*`
   labels (`jumpTarget` / `writeJumpTarget`). A pass must not match across
   a jump target; collect the targets with `jumpTarget` first.
4. **Atoms**: if the opcode carries an atom, add it to the list in
   `preResolveAllAtoms` (`ProtoBytecodeLoader.cpp`); otherwise the
   interpreter's atom lookup fails at run time (`resolveAtom: atom N not in
   cache`). Write groups keep their atoms in the module instead, and the
   loader resolves those separately.
5. **The VM**: an `X(value, L_OP_name)` entry in `PROTOJS_DISPATCH_TARGETS`
   and the handler label `L_OP_name:` in `runBytecodeImpl`, ending with
   `DISPATCH()`. When the handler starts, `pc` points at the first operand
   byte; advance it past the operands. Rooting: read operands from their
   stack slots and pop them only once the result is computed, or rely on
   popped slots staying rooted until the next push (1.4); never hold a
   `ProtoObject*` only in a C++ local across an allocation, a call into JS
   or a safepoint (protoCore EMBEDDER-CONFORMANCE rule 3). After anything
   that can run JS or grow the slot array, `REFRESH_INTERP_STATE()`. Keep
   cold paths in out-of-line helpers: the dispatch function's native frame
   size limits the JavaScript recursion depth on Windows
   (`docs/INSTALLATION.md`).
   ```cpp
   L_OP_proto_put_field_group: {
       if (pc + 4 > len || stackSize(pContext) < 2) return PROTO_NONE;
       const uint16_t gid = get_u16(buf + pc);
       const uint16_t gpos = get_u16(buf + pc + 2);
       pc += 4;
       ...
       DISPATCH();
   }
   ```
6. **Jumps**: if it is a jump, teach `jumpTarget` and `writeJumpTarget`
   (`BytecodeSpecialiser.cpp`) about it, and the debugger if it walks code.
7. **The disassembler**: an entry in `opTable` in
   `src/runtime/Disassembler.cpp`, with an operand format; add a format and
   its case in `Lister::decode` if none fits. An opcode it does not know is
   listed as `unknown opcode`, and the listing stops there.
8. **Tests**: an asserting JavaScript test under `tests/integration/basic/`
   that exercises the new path and its fallback (registered in the
   `foreach(js_test ...)` list of `tests/CMakeLists.txt`), and a
   `docs/vm-examples/NN-name.js` whose listing shows the opcode, with its
   marker in this document. Run `disasm_examples.py build/protojs --update`,
   review the diff, then `ctest -R docs/vm-examples`.
9. **This document**: the opcode's row in section 2.15 and the counts at the
   top of section 2.

## 5. How to add a native method

A built-in is a C++ function with protoCore's `ProtoMethod` signature,
installed as an attribute of the global object or of a prototype.

**The context rule.** A `ProtoContext` is created only when a method, that
is interpreted code with its own locals, is activated; a native method runs
in its caller's context. protoJS follows this rule today: no native-call
path in `ProtoInterpreter.cpp` constructs a context. `call_method`
(`ProtoInterpreter.cpp:15913`), `call` (16891) and `callJSFunction` (20219)
call `callNative(fn, pContext, this, args)` with the caller's context. The
contexts the interpreter creates are all bytecode activations: `call_method`
(15825), `call_constructor` (16287), `call` (16805), `callJSFunction`
(20265), the interpreter's own `valueOf` / `toString` calls (8116), and the
resumptions of generators (19131) and async functions (19297). A native
that calls back into JavaScript (`reduce`, a getter, a Proxy trap) does so
through `callJSFunction`, which creates the context for that activation.

1. **Write the function.** Model: `mathMax` in `src/MathBuiltin.cpp`:
   ```cpp
   static const proto::ProtoObject* mathMax(
       proto::ProtoContext* ctx, const proto::ProtoObject*,   // self: the receiver
       const proto::ParentLink*, const proto::ProtoArguments* args,
       const proto::ProtoKeywordArguments*)
   {
       proto::proto_ulong argc = args ? static_cast<proto::proto_ulong>(args->getSize(ctx)) : 0;
       ...
   }
   ```
   `self` is the JavaScript `this` (the receiver of `call_method`,
   `PROTO_NONE` for a plain call). `args` holds the positional arguments;
   `kwargs` is always null (JavaScript has no keyword arguments). `args` may
   be null when there are none.
2. **Raise errors** with `signalNativeException(makeNativeError(ctx,
   "TypeError", "message"))` or `throwJS(ctx, value)`
   (`ProtoInterpreter.h`); both throw a `proto::ProtoException` that a
   JavaScript `catch` receives. A `std::exception` escaping the native is
   converted into a JavaScript `Error` by `callNative`.
3. **Register it.** `ctx->fromMethod(nullptr, fn)` makes the method value.
   Built-ins that need `name` and `length` (all of the standard library) are
   wrapped in a function object that carries the method as `__native_fn__`,
   `name` and `length` with descriptor 0x2; see the `reg` lambda in
   `ensureMathObject`. Install the wrapper on the prototype or namespace
   object, and objects on the binding root with
   `*globalRoot = (*globalRoot)->setAttribute(ctx, key, value)`. Built-ins
   are installed once per binding root in `runBytecodeImpl`'s globals
   initialisation (guarded by `__protojs_globals_init__`); host modules in
   `src/modules/` register in their own install functions.
4. **Calling back into JavaScript**: `callJSFunction(ctx, fn, thisVal, args)`
   (`ProtoInterpreter.h`). It handles bytecode functions, natives, bound
   functions and Proxies. It returns `PROTO_NONE` for a value that is not
   callable, without raising: check callability first when the spec requires
   a TypeError.
5. **Rooting** (protoCore EMBEDDER-CONFORMANCE rule 3). The arguments are
   rooted by the caller (they are its operand-stack slots) for the whole
   call. A value you create and keep while you allocate again, or across a
   call into JavaScript, must sit in a traced slot: open a scope
   (`proto::ProtoContext scope(ctx->space, ctx); scope.resizeAutomaticLocals(n);`)
   and `scope.setAutomaticLocal(i, v)`. A scope context of this kind is a
   rooting device inside the native, not an activation. Return the result
   unrooted; the caller pushes it into its slot. For values that outlive the
   call (callbacks, timers, other threads) read `docs/GC_BRIDGING.md`.
6. **Test.** An asserting script under `tests/integration/basic/` (it checks
   its results and exits 1 on any failure), added to the `foreach(js_test
   ...)` list in `tests/CMakeLists.txt`. Cover the error cases.

## 6. Known issues and oddities

Found while writing this document, on `master` at `0dcefe1be` built with
protoCore 3.2.0. Every item marked *verified* was run on that build; the
expected value is the ECMA-262 (Node.js) result. The branch
`fix/vm-known-issues` fixed the unhandled opcodes, the specialiser, the
class, bound-call, equality, truthiness, TDZ and iterator-closing bugs found
then (CHANGELOG); what remains is listed here.

Decisions taken by the agent that fixed them, pending the maintainer's
review: dynamic `import()` raises a SyntaxError where it is reached rather
than returning a rejected promise (issue 1); `eval(...xs)` is a direct eval
as the specification says, although Node 22 treats it as indirect; direct
eval compilations are cached for the life of the process (issue 10); the
specialiser keeps the instructions it fuses after a guarded header instead
of replacing them (1.3).

### Unhandled opcodes

1. **Dynamic `import()` is not supported**: the `import` opcode has no
   handler, and reaching it raises `SyntaxError: protoJS does not support
   dynamic import()`. ES modules run on the QuickJS module evaluator
   (1.1), which the VM cannot hand a module namespace to yet. Static
   `import` declarations work in module mode.

### Verified VM bugs

2. **Typeof**: `typeof "Symbol(x)"` is `"symbol"` (a string whose text starts
   with `Symbol(` or `Symbol.`).
3. **Strict mode**: assigning to an undeclared name creates a global instead
   of throwing ReferenceError. In sloppy code `undefined = 5` throws instead
   of being ignored.
4. **Globals**: getters defined on `globalThis` read as `undefined` through
   a global name (`get_var` does not call accessors).
5. **`arguments`** is never mapped to the parameters and has no `callee`.
6. **Operators**: `1 ** Infinity` is 1 (expected `NaN`) and `**` converts
   its right operand first; `undefined < obj` does not call
   `obj.valueOf`; `'x' in undefined` is false and `delete null.x` is true
   (both should throw); `instanceof` ignores `Symbol.hasInstance` and bound
   functions.
7. **Calls**: a class constructor called through a native that calls back
   into JavaScript (`C.call(o)`, `Reflect.apply(C, ...)`) from inside
   another constructor runs, because that constructor's new.target is still
   published (`check_ctor` reads it); at any other place it throws as it
   should.
8. **Properties**: `null.length` / `undefined.length` give `undefined`
   (`get_length`); `proxy["m"]()` ignores the Proxy (`get_array_el2`); a
   derived constructor may return a Symbol; named capture groups are lost
   (`.groups` is `undefined`); `{..."ab"}` is `{}`; a function under a
   Symbol key in an object literal is named `Symbol(d)` instead of `[d]`.
9. **Iteration**: array-likes without `@@iterator` are iterated; `next` is
   read on every step and a getter `next` is not called; for-await accepts
   non-object results and only exact booleans as `done`; calling `it.next()`
   from inside the running generator recurses until RangeError instead of
   TypeError; for-in visits keys deleted during the loop and does not list
   integer keys first.
10. **Direct eval caches its compilations**: each distinct (calling
    function, scope, source text) is compiled once and kept for the life of
    the process (`JSContextWrapper::loadEvalModule`), so a program that
    evaluates an unbounded number of different strings grows without
    bound. Interned private-name keys (one per evaluation of a class with
    private members) are kept the same way.
11. **`with` and Proxies**: a Proxy used as a `with` object sees
    `Symbol.unscopables` as the string key `"Symbol.unscopables"` in its
    traps (protoJS carries well-known Symbols as strings), and an
    assignment does not repeat the HasProperty / GetOwnProperty steps of
    SetMutableBinding (test262 `language/statements/with/*-with-proxy-env`).
    There are no proper tail calls (`tco-*` tests exhaust the stack).

### Oddities (from reading the code)

- **Stale comments in `ProtoInterpreter.cpp`**: the `iterator_next` /
  `iterator_call` comments describe destructuring and rest collection, but
  QuickJS emits them only for `yield*`, and their slot-record paths look
  unreachable.
- **String length and indexing** count code points, not UTF-16 code units:
  `"\u{1F600}".length` is 1.
- **For-in key order** of named properties is protoCore's attribute order,
  a documented deviation (`src/runtime/PropertyDictionary.h`).
- **`fclosure8` vs `fclosure`**: only the long form stamps `__metadata__`
  on the closure.
- **Phase-1 opcode values overlap short opcodes** (178 to 196): a stray
  phase-1 opcode would execute as a short opcode instead of reaching
  `L_default`. QuickJS never leaves one in final bytecode.
