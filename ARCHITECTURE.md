# protoJS Technical Architecture

**Last reviewed:** 2026-09-15

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     JavaScript source                       │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│               QuickJS frontend (deps/quickjs)               │
│        Parser and bytecode compiler (compile-only mode)     │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                   protoJS runtime layer                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ Bytecode     │  │ Proto        │  │ Built-ins    │       │
│  │ loader       │  │ Interpreter  │  │ and modules  │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ TypeBridge   │  │ GCBridge     │  │ EventLoop,   │       │
│  │ (boundary)   │  │ (boundary)   │  │ thread pools │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                     protoCore runtime                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ ProtoSpace   │  │ ProtoContext │  │ ProtoThread  │       │
│  │ (GC, memory) │  │ (execution)  │  │ (concurrency)│       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ ProtoObject  │  │ Collections  │  │ Immutability │       │
│  │ (prototypes) │  │ (List, Set…) │  │ (structural) │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
└─────────────────────────────────────────────────────────────┘
```

QuickJS is vendored under `deps/quickjs`. protoCore is linked as the shared library `libprotoCore`, located either through `PROTO_CORE_PREFIX` or in a sibling `../protoCore` checkout (`CMakeLists.txt`).

---

## Main Components

### 1. JSContextWrapper

**Responsibility:** Owns the QuickJS runtime and context together with the protoCore space for one JavaScript environment (`src/JSContext.h`, `src/JSContext.cpp`).

**Structure (abridged):**
```cpp
class JSContextWrapper {
    JSRuntime* rt;                  // QuickJS runtime (compiler frontend, ES modules, fallback)
    JSContext* ctx;                 // QuickJS context
    proto::ProtoSpace pSpace;       // protoCore space (GC, memory)
    proto::ProtoContext* pContext;  // root context (pSpace.rootContext)
    JSPrototypes jsPrototypes_;     // base JS prototypes (Object, Array, Arguments, RegExp)
    // ...
};
```

**Design decisions:**
- Each wrapper owns exactly one `ProtoSpace` and one QuickJS runtime.
- The root `ProtoContext` (`pSpace.rootContext`) is used for bootstrap and global roots.
- Execution on protoCore uses **stack-allocated (RAII) contexts**:
  - Each top-level `eval()` creates a `ProtoContext` on the C++ stack, chained to the root context.
  - Each call to a bytecode function in `ProtoInterpreter` creates its own child `ProtoContext`, chained to the caller's.
  - When a `ProtoContext` is destroyed, its destructor anchors `returnValue` (if set) as a `ReturnReference` in the parent context and then submits its young generation to the GC.
- The constructor runs `BootstrapJSPrototypes`, which creates the Object, Array, Arguments and RegExp prototypes as mutable children of `ProtoSpace::objectPrototype` and stores them in `JSPrototypes`. Later bootstrap steps install the fully populated prototypes (see the comments on the prototype accessors in `src/JSContext.h`).
- `getRootSet()` exposes a `ProtoRootSet` named `"protojs-async"`, created on first use and destroyed by the wrapper destructor. It pins objects that must survive asynchronous hops (see `docs/GC_BRIDGING.md`).

### 1.1 ProtoObject-Based Object Model and Attribute Keys

**Object model:** JavaScript objects are `ProtoObject`s. Objects that JavaScript semantics require to be mutable are created with `prototype->newChild(ctx, true)`, and properties are read and written directly with `getAttribute` / `setAttribute`.

**Arrays:** An array is a mutable child of the Array prototype. Dense elements are stored as a single `ProtoList` (protoCore's persistent AVL list) under the `__elements__` attribute, and a `length` attribute is kept in sync (`src/ArrayElementsStorage.h`). Writes past the end pad with `PROTO_NONE` up to a sparse-fallback threshold.

**Strings and symbols:** protoCore distinguishes plain strings from interned symbols. `ProtoString::createSymbol` returns a canonical pointer per content, and `setAttribute` auto-interns string keys; strings produced by `fromUTF8String` and similar constructors are not interned. Strong symbols are never collected. protoJS exposes frequently used keys (for example `"length"`, `"prototype"`, `"constructor"` and `"__elements__"`) through getters in `JSSymbols` (`src/JSSymbols.h`). Each getter initializes a function-local static with `createSymbol` on first call, so the pointer is globally unique and can be reused across contexts without per-context caching.

### 1.2 Numbers: ProtoInteger, ProtoDouble, and the Number Prototype

**Integers:** Integer-valued JavaScript numbers are protoCore integers: a tagged **SmallInteger** (54-bit signed value stored in the pointer) or a **LargeInteger** when the value exceeds that range. They are created with `context->fromInteger` or `context->fromLong`. In the interpreter's addition opcode, integer results outside the SmallInteger range are delegated to protoCore, which promotes them to LargeInteger instead of converting them to a double.

**Doubles:** Non-integer numbers, and negative zero, are protoCore doubles created with `context->fromDouble`.

**Number prototype:** `BuildNumberPrototype` (`src/NumberPrototype.cpp`) builds a shared prototype with native `valueOf`, `toString`, `toFixed`, `toExponential`, `toPrecision` and `toLocaleString`, and assigns it to `space->smallIntegerPrototype`, `space->largeIntegerPrototype` and `space->doublePrototype`. Method lookup on a number therefore resolves through protoCore's attribute lookup, with the number as the receiver.

### 1.3 Object Creation and Inheritance (newChild, addParent)

**Single inheritance:** Plain objects and arrays are created with `prototype->newChild(ctx, true)`, which makes them mutable and inherit from the given prototype.

**Multiple parents:** protoCore's object model supports more than one parent through `obj->addParent(ctx, otherProto)`. protoJS uses it internally, for example to attach marker prototypes for non-extensible objects (`src/ObjectPrototype.cpp`).

### 1.3a Local Variables in ProtoContext

protoCore's `ProtoContext` offers two kinds of local storage:

- **Automatic locals:** an index-addressed slot array (`automaticLocals`) sized through the constructor's `totalSlots` / `externalSlots` parameters and discarded when the context is destroyed.
- **`closureLocals`:** a `ProtoSparseList` owned by the context.

The protoJS interpreter lays out each frame in the automatic-locals array: local and argument slots first, followed by the operand stack. The stack base and top indices are tracked in a thread-local frame record that holds only the context pointer and integer indices. When an inner function captures a local variable, the interpreter promotes that local to a closure cell and reads it through closure-variable slots (`OP_close_loc`, `OP_get_var_ref`).

**Interpreter rule (absolute):** `ProtoInterpreter` must **not** keep `ProtoObject*` execution state (locals, arguments, operand stack) in `std::vector` or any other C++ container, because the GC does not trace them. All such references live in `ProtoContext` slots. See the "Absolute rule" section of [src/runtime/README.md](src/runtime/README.md).

### 1.4 Eval Path: Compile, Load, Run

**Default path:** Script execution goes through protoCore.

1. **`eval()`** compiles the source with the QuickJS frontend in compile-only mode (`compileToBytecodeWithFlags`, `src/runtime/ProtoCompileOnly.cpp`). `loadBytecode()` copies the bytecode and its constant pool into a `ProtoBytecodeModule` (`src/runtime/ProtoBytecodeLoader.cpp`), and `runBytecode()` executes it in `ProtoInterpreter`. Operands and locals are `ProtoObject*`; the QuickJS interpreter does not run. The result is converted to a `JSValue` only at the boundary, with `TypeBridge::toJS`.
2. **CommonJS modules:** `CommonJSLoader` compiles, loads and runs module bodies through the same pipeline.
3. **REPL and debugger evaluate:** `src/repl/REPL.cpp` and `src/debugging/IntegratedDebugger.cpp` call the wrapper's `eval()`.

**Cases that QuickJS executes:**
- **ES module mode** (`--input-type=module`): QuickJS compiles and evaluates the module itself (`JS_EvalFunction`), because protoCore does not implement module linking.
- **Compile fallback:** if compile-only compilation fails, `eval()` prints `compile failed, fallback to QuickJS eval` and runs the source with `JS_Eval`, unless `PROTOJS_NO_FALLBACK=1` is set.

**Bridges:** `QuickJSArrayBridge` consists of no-op stubs that `quickjs.c` still references. `ExecutionEngine` keeps legacy QuickJS interception hooks (`opGetProperty`, `opSetProperty`, `opCall`, …) that the interpreter path does not invoke; the wrapper calls only its `initialize` and `cleanup` functions. `TypeBridge` and `GCBridge` operate at the boundary with QuickJS values. Built-ins (Array, String, JSON, Math, RegExp, …) are protoCore-native prototypes and functions (`src/*Prototype.cpp`, `src/*Builtin.cpp`, wrapped with `wrapNativeFunction`) installed on the protoCore-native global; the interpreter does not call back into QuickJS to run them.

**Native global:** `getNativeGlobal()` creates the global object as a mutable child of the JS Object prototype and adds `Infinity`, `NaN` and `undefined`; `main.cpp` and the modules then register their bindings onto it explicitly. `runBytecode` receives a pointer to the current global root and updates it when `put_field` or `define_field` on the global produce a new root, so subsequent reads see the new object.

**Worker threads:** `worker_threads` runs each `Worker` on a `std::thread` with its own `JSContextWrapper`, and therefore its own `ProtoSpace`. The worker script runs through the same compile → load → run path. Because objects from one space cannot be referenced from another, messages are JSON-serialized and delivered through the `EventLoop` (`src/modules/worker_threads/WorkerThreadsModule.h`).

**Deferred and ProtoThreads:** see [section 4](#4-deferred-and-threading-model).

**Unsupported opcodes:** an opcode the interpreter does not implement is reported on stderr as `[ProtoInterpreter] unsupported opcode 0x.. at byte offset N`, and execution of that function stops.

Test262 conformance on this path is covered in [TESTING_STRATEGY.md](TESTING_STRATEGY.md) and [CONFORMANCE_JS.md](CONFORMANCE_JS.md).

### 2. TypeBridge

**Responsibility:** Converts between QuickJS `JSValue`s and protoCore objects wherever QuickJS values still appear: eval results, code executed by QuickJS, and module bindings (`src/TypeBridge.h`, `src/TypeBridge.cpp`).

**`fromJS` mapping:**

| JavaScript value | protoCore representation | Notes |
|------------------|--------------------------|-------|
| `null` | Null sentinel | `PROTO_NONE` if the interpreter sentinel is not initialized |
| `undefined` | Undefined sentinel | `PROTO_NONE` if the interpreter sentinel is not initialized |
| `boolean` | protoCore boolean | `context->fromBoolean` |
| `number` (integer-valued, not `-0`) | SmallInteger or LargeInteger | `context->fromInteger` |
| `number` (other, including `-0`) | Double | `context->fromDouble` |
| `bigint` | protoCore integer in a BigInt wrapper | |
| `string` | `ProtoString` | |
| `Array` | Mutable child of the Array prototype | Elements in a `ProtoList` under `__elements__`, plus `length` |
| `function` | Mutable `ProtoObject` placeholder | Mapped back to the `JSValue` through `GCBridge` |
| `RegExp` | `ProtoObject` with pattern and flags | Mutable (for `lastIndex`) |
| `Map` | `ProtoSparseList` | |
| `Set` | `ProtoSet` | |
| TypedArray, `ArrayBuffer` | `ProtoList` | |
| `Symbol` | `ProtoObject` with the description | |
| Other objects | Mutable child of the JS Object prototype | Own properties converted recursively |
| `Date` | — | No dedicated conversion |

Objects already registered in `GCBridge` are returned from the mapping instead of being converted again.

**`toJS`** converts booleans, BigInts, integers, doubles and strings to their JavaScript equivalents, and `ProtoList`, `ProtoTuple` and `ProtoSet` to JavaScript arrays. Other protoCore objects become a placeholder JavaScript object tagged `_type: "ProtoObject"` and registered in `GCBridge`.

**Signatures:**

```cpp
static const proto::ProtoObject* TypeBridge::fromJS(
    JSContext* ctx, JSValue val, proto::ProtoContext* pContext);

static JSValue TypeBridge::toJS(
    JSContext* ctx, const proto::ProtoObject* obj, proto::ProtoContext* pContext);
```

### 3. Execution Engine

**Responsibility:** Execute QuickJS bytecode on protoCore. This role belongs to `ProtoInterpreter` (`src/runtime/ProtoInterpreter.cpp`); the older `ExecutionEngine` class is legacy (see [section 1.4](#14-eval-path-compile-load-run)).

**Execution flow:**

```
1. QuickJS compiles JS → bytecode (compile-only)
2. ProtoBytecodeLoader copies bytecode and constants into a ProtoBytecodeModule
3. ProtoInterpreter dispatches each opcode through a computed-goto table,
   operating on ProtoObject* values held in ProtoContext slots
4. Calls to bytecode functions run in child ProtoContexts; native
   functions are invoked through protoCore
5. The top-level result is converted to a JSValue at the boundary
```

**Implementation notes:**
- Opcode numbering mirrors QuickJS (`src/runtime/QuickJSOpcodeEnum.h`).
- Hot opcodes have inline SmallInteger fast paths; other cases delegate to protoCore operations.
- `BytecodeSpecialiser` (`src/runtime/BytecodeSpecialiser.h`) is an optional post-codegen pass that fuses common loop sequences into single opcodes. It is selected with `PROTOJS_SPECIALISER=off|nop|compact` and defaults to `compact` (`src/runtime/BytecodeSpecialiser.cpp`); `PROTOJS_SPECIALISER=off` disables it.

### 4. Deferred and Threading Model

**Responsibility:** Run asynchronous and parallel work while keeping every `ProtoObject*` visible to the protoCore GC.

**Threads in a protoJS process:**

```
┌──────────────────────────────────────────┐
│ Main thread                              │
│  - ProtoInterpreter (synchronous JS)     │
│  - EventLoop callbacks                   │
└──────────────┬───────────────────────────┘
               │
   ┌───────────┼──────────────┬──────────────────┬─────────────────┐
   ▼           ▼              ▼                  ▼                 ▼
┌────────┐ ┌────────┐ ┌──────────────┐ ┌──────────────────┐ ┌────────────┐
│CPU pool│ │I/O pool│ │ ProtoThreads │ │ Worker threads   │ │ protoCore  │
│N=cores │ │ceil(N× │ │ newThread()  │ │ std::thread +    │ │ GC thread  │
│        │ │factor) │ │ shared space │ │ own ProtoSpace   │ │            │
└────────┘ └────────┘ └──────────────┘ └──────────────────┘ └────────────┘
```

- **EventLoop** (`src/EventLoop.h`) runs all callbacks on the main thread. `main.cpp` keeps processing callbacks while any are pending or while Deferreds, workers, or HTTP/net handles are active.
- **CPUThreadPool** defaults to `std::thread::hardware_concurrency()` threads (`--cpu-threads`). **IOThreadPool** defaults to `ceil(cores × factor)` threads with a factor of 3.0 (`--io-threads`, `--io-threads-factor`) and serves the fs, dns and io modules.
- **ProtoThreads** are created with `ProtoSpace::newThread` and share the creating space.
- **Worker threads** are described in [section 1.4](#14-eval-path-compile-load-run).

**`Deferred` on the protoCore-native global** (`src/ProtoDeferred.cpp`):

1. `new Deferred(workerFn)` creates a pending instance and schedules `workerFn` on the next event-loop turn, on the main thread.
2. The function's return value fulfils the Deferred; a thrown exception rejects it.
3. `.then(cb)` and `.catch(cb)` register callbacks, which are drained through the EventLoop.
4. `workerFn` and the instance are pinned in the wrapper's root set until the callback runs.

**`protoCore.runInThread(workerName, args)`** (`src/ProtoCoreNativeBindings.cpp`):

1. Looks up a registered native C++ worker (for example `cpuChunk`).
2. Starts it on a ProtoThread in the shared `ProtoSpace` and returns a pending Deferred.
3. Pins the Deferred and the arguments in the wrapper's root set.
4. A CPU-pool task joins the thread and enqueues the resolution on the EventLoop.

**QuickJS-side Deferred** (`src/Deferred.cpp`): installed on the QuickJS global, so it is visible only to code that QuickJS executes. Each task runs on a ProtoThread with its own `JSContextWrapper`; if the wrapper or `newThread` is unavailable, the Deferred is rejected.

### 5. GC Bridge

**Responsibility:** Track the correspondence between QuickJS `JSValue`s and protoCore objects at the boundary (`src/GCBridge.h`, `src/GCBridge.cpp`).

**Problem:**
- QuickJS and protoCore each have their own garbage collector.
- A value that crosses the boundary must stay reachable on both sides for as long as it is in use.

**Solution:**
1. **Mappings stored in protoCore objects:** per-`JSContext` mappings are kept in a `ProtoSparseList` keyed by the hash of a string key derived from the `JSValue`. The structure is anchored through a `ProtoRootSet` handle, so the protoCore GC traces it. No STL maps hold `ProtoObject*`.
2. **Lookup:** `registerMapping`, `getProtoObject` and `getJSValue` are used by `TypeBridge`, the CommonJS loader and the interpreter to avoid converting the same object twice and to preserve identity.
3. **Root and weak flags:** `registerRoot` / `unregisterRoot` and `registerWeakRef` / `unregisterWeakRef` mark mapping entries.
4. **Diagnostics:** `detectLeaks`, `reportLeaks` and `getMemoryStats`.

Map updates are serialized with a recursive mutex.

**Asynchronous callbacks:** a `ProtoObject*` captured by a C++ lambda for the EventLoop or a thread pool is pinned in the wrapper's `"protojs-async"` root set, or is a perpetual allocation such as a symbol. The rules are documented in `docs/GC_BRIDGING.md`.

### 6. protoCore Module

**Responsibility:** Expose protoCore-specific capabilities to JavaScript.

**On the protoCore-native global** (`src/ProtoCoreNativeBindings.cpp`), the `protoCore` object currently provides only `runInThread`:

```javascript
// Run the registered native worker "cpuChunk" on a ProtoThread.
// Returns a Deferred that resolves with the worker's result.
const d = protoCore.runInThread('cpuChunk', [200000]);
d.then(result => console.log(result));
```

**QuickJS-side module** (`src/modules/ProtoCoreModule.cpp`), installed on the QuickJS global and reachable only from code that QuickJS executes, provides:

```javascript
// Collections
const set = new protoCore.Set([1, 2, 3]);
const multiset = new protoCore.Multiset([1, 1, 2, 3]);
const sparseList = new protoCore.SparseList();
const tuple = protoCore.Tuple([1, 2, 3]);

// Mutability control
const immutable = protoCore.ImmutableObject({a: 1});
const mutable = protoCore.MutableObject({a: 1});
protoCore.isImmutable(obj);
protoCore.makeImmutable(obj);
protoCore.makeMutable(obj);

// Native worker on a ProtoThread
protoCore.runInThread('cpuChunk', [200000]);
```

Porting the collection and mutability APIs to the native global is not yet done.

---

## End-to-End Execution Flow

### Running a Simple Script

```
1. User runs: protojs script.js

2. main.cpp:
   - Parses options (--cpu-threads, --io-threads, --input-type=module, ...)
   - Creates JSContextWrapper(cpuThreads, ioThreads, ioFactor)
   - Installs console, JSON, timers, event-loop bindings, Deferred,
     protoCore, process, io and other modules on the native global
   - Calls wrapper.eval(code, filename, inputTypeModule)

3. JSContextWrapper::eval():
   - Creates a stack ProtoContext chained to the root context
   - QuickJS compiles the source to bytecode (compile-only)
   - loadBytecode() builds a ProtoBytecodeModule
   - runBytecode() executes it in ProtoInterpreter

4. main.cpp drains the EventLoop while callbacks are pending or
   Deferreds, workers or HTTP/net handles are active

5. Shutdown:
   - ~JSContextWrapper releases the async root set, GCBridge mappings,
     thread pools, and the QuickJS context and runtime
```

### Running with Deferred and runInThread

```
1. Script calls: new Deferred(fn)
   - ProtoDeferred creates a pending instance, pins fn and the instance,
     and enqueues fn on the EventLoop
   - On the next turn the main thread calls fn; its return value
     fulfils the Deferred, an exception rejects it
   - .then/.catch callbacks run on the main thread through the EventLoop

2. Script calls: protoCore.runInThread('cpuChunk', args)
   - A ProtoThread is created in the shared ProtoSpace and runs the
     native worker
   - A CPU-pool task joins the thread
   - The resolution is enqueued on the EventLoop; the Deferred's .then
     callbacks run on the main thread
```

---

## Key Design Decisions

### 1. Why keep QuickJS?

**Decision:** Use QuickJS as the parser and bytecode compiler; execute the bytecode on protoCore.

**Rationale:**
- QuickJS has a complete, well-tested parser and compiler.
- Reimplementing them is not needed to evaluate protoCore as the runtime.
- Objects, memory and garbage collection on the default path belong to protoCore.

**Implementation:**
- Compile-only QuickJS compilation, then `ProtoBytecodeLoader` and `ProtoInterpreter`.
- QuickJS still executes ES modules and the compile-failure fallback ([section 1.4](#14-eval-path-compile-load-run)).

### 2. How is mutability handled?

**Decision:** JavaScript objects and arrays are mutable protoCore objects; protoCore's immutable collections are used as internal storage.

**Rationale:**
- JavaScript semantics require mutable objects by default.
- protoCore's persistent collections give cheap snapshots and structural sharing.

**Implementation:**
- Objects and arrays are created with `newChild(ctx, true)`.
- A mutable object update installs a new immutable snapshot with a compare-and-swap into a sharded `mutableRoot` table in `ProtoSpace`.
- Array elements are stored in a persistent `ProtoList`.

### 3. How are objects shared between threads?

**Decision:** Threads that share a `ProtoSpace` share objects directly; isolated JavaScript environments exchange serialized messages.

**Rationale:**
- Immutable protoCore values can be read from any thread in the same space without copying.
- Mutable objects are updated through protoCore's compare-and-swap snapshot mechanism rather than protoJS-level locks.

**Implementation:**
- `runInThread` workers run in the creating space.
- `worker_threads` workers own a separate space and communicate through JSON and the EventLoop.

### 4. How is garbage collection handled?

**Decision:** protoCore's GC owns all runtime objects on the default path; QuickJS values are confined to the boundary.

**Rationale:**
- It avoids two collectors competing for the same objects.
- It lets execution state be traced precisely.

**Implementation:**
- Interpreter state lives in `ProtoContext` slots.
- Objects crossing asynchronous boundaries are pinned in a `ProtoRootSet`.
- `GCBridge` keeps boundary mappings in protoCore structures.

---

## Directory Structure

```
protoJS/
├── src/
│   ├── main.cpp                    # CLI entry point
│   ├── JSContext.h/.cpp            # JSContextWrapper (QuickJS + protoCore)
│   ├── TypeBridge.h/.cpp           # JSValue ↔ protoCore conversion
│   ├── GCBridge.h/.cpp             # Boundary mappings
│   ├── ExecutionEngine.h/.cpp      # Legacy QuickJS interception hooks
│   ├── ProtoDeferred.h/.cpp        # Deferred on the native global
│   ├── Deferred.h/.cpp             # QuickJS-side Deferred
│   ├── ProtoCoreNativeBindings.*   # protoCore object on the native global
│   ├── EventLoop*.h/.cpp           # Main-thread callback queue and bindings
│   ├── CPUThreadPool.*, IOThreadPool.*, ThreadPoolExecutor.*
│   ├── JSPrototypes.*, JSSymbols.*, ArrayElementsStorage.h
│   ├── *Prototype.h/.cpp, *Builtin.h/.cpp   # Built-ins
│   ├── runtime/                    # Compile-only frontend, loader, interpreter, specialiser
│   ├── modules/                    # CommonJS/ES loaders and Node-style modules
│   ├── native/                     # Native addon loading
│   ├── npm/                        # Package resolution and installation
│   ├── repl/, debugging/, profiling/, memory/, monitoring/, logging/
│   └── benchmarking/, testing/     # Benchmark and test runner components
├── tests/                          # See TESTING_STRATEGY.md
├── deps/
│   └── quickjs/                    # Vendored QuickJS sources
├── docs/                           # User and developer documentation
├── packaging/                      # Packaging scripts and templates
├── CMakeLists.txt
├── README.md
├── CONFORMANCE_JS.md
└── ARCHITECTURE.md                 # This document
```

---

## Performance Considerations

### Key Optimizations

1. **Tagged pointers:**
   - SmallIntegers (54-bit) are stored in the pointer and need no allocation.
   - The interpreter has inline SmallInteger fast paths for hot opcodes.

2. **Structural sharing:**
   - protoCore collections are persistent, so array element updates share structure with the previous version.
   - Immutable values can be shared between threads of the same space without copying.

3. **Per-thread allocation:**
   - protoCore allocates cells from per-thread free lists, avoiding locks on the common path.

4. **Attribute lookup caching:**
   - protoCore keeps a per-thread attribute cache.
   - protoJS uses interned `JSSymbols` keys so lookups compare pointers instead of building strings on each access.

5. **Bytecode specialisation (on by default):**
   - Fused opcodes for common loop shapes are emitted in `compact` mode unless `PROTOJS_SPECIALISER` selects `nop` or `off`.

Measured results and the standard benchmark suite are described in [README.md](README.md) and [tests/benchmarks/standard/README.md](tests/benchmarks/standard/README.md).

---

## Known Limitations

- ES modules (`--input-type=module`) are executed by QuickJS, not by `ProtoInterpreter`.
- A source that the compile-only frontend rejects falls back to QuickJS evaluation unless `PROTOJS_NO_FALLBACK=1` is set.
- `new Deferred(fn)` runs `fn` on the main thread. Parallel CPU work uses `protoCore.runInThread` with a registered native worker, or `worker_threads`.
- The protoCore collection and mutability APIs (`Set`, `Multiset`, `SparseList`, `Tuple`, `ImmutableObject`, …) exist only in the QuickJS-side module.
- `TypeBridge` has no `Date` conversion.
- The `ExecutionEngine` interception hooks remain in the source but are not used on the default path.
