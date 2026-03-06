# Technical Audit: protoJS

**Date:** March 2026  
**Version:** 1.0  
**Status:** Current state — Option B runtime complete through Phase 9  
**Scope:** Architecture, protoCore interpreter, eval paths, testing, and documentation

---

## 1. Executive Summary

protoJS is a JavaScript runtime that uses **QuickJS** only for parsing and compilation and **protoCore** for execution, object representation, and memory management. The project implements the **Option B (Reuse Parser, Full protoCore Runtime)** path: bytecode is compiled by QuickJS and executed by a native **ProtoInterpreter** over `ProtoContext` and `ProtoObject`, with no QuickJS interpreter or heap during script execution on the protoCore path.

**Current state (March 2026):**

- **Single eval path:** All script execution uses protoCore (compile → load → run via ProtoInterpreter). No legacy `JS_Eval` for main script, REPL, debugger, modules, **workers**, or **Deferred**. Workers and Deferred are implemented as **ProtoThread** (protoCore); each has a JSContextWrapper and runs script via the same path, enabling native multithreading for JavaScript.
- **Phase 3 full interpreter:** Stack, locals, properties, arrays, control flow, calls, constructors, and operators are implemented; execution state lives only in `ProtoContext` (no `std::vector`). Unknown opcodes are logged to stderr (opcode + byte offset).
- **Testing:** Directed smoke test (`node tests/test262/runner/proto_eval_smoke.js`) and Test262 runner with protoCore path (`TEST262_USE_PROTO_EVAL=1`). Conformance tracked in `CONFORMANCE_JS.md` (e.g. `built-ins/Array/isArray`: 29/29 on protoCore).
- **Documentation:** Architecture, runtime flow, and conformance are documented in `ARCHITECTURE.md`, `src/runtime/README.md`, and `CONFORMANCE_JS.md`.

**Overall assessment:** The protoCore execution path is implemented and testable; workers and Deferred run as ProtoThreads on the same path, providing native multithreading for JavaScript. Conformance and coverage are documented; Phase 6 (optional native global) remains as a next step.

---

## 2. Architecture Overview

### 2.1 High-Level Flow

```
JavaScript source
       │
       ▼
QuickJS (parser/compiler only)
       │
       └── Single path: compileToBytecode() → loadBytecode() → ProtoInterpreter::runBytecode()
                                    │
                                    ▼
                            ProtoContext + ProtoObject only
                            (stack & locals in closureLocals; result → TypeBridge::toJS at boundary)
```

### 2.2 Key Components

| Component | Role |
|-----------|------|
| **JSContextWrapper** | Owns JSRuntime, JSContext, ProtoSpace, root ProtoContext. Single eval path: compile → load → run (no legacy JS_Eval). |
| **ProtoCompileOnly / ProtoBytecodeLoader** | Compile-only QuickJS → bytecode; load into ProtoBytecodeModule (buffer, constant pool, nested function placeholders). |
| **ProtoInterpreter** | Executes bytecode: stack and locals in ProtoContext::closureLocals (ProtoList stack, slot-indexed locals); dispatches opcodes; calls ProtoMethod and nested bytecode; no QuickJS runtime. |
| **TypeBridge** | Converts JSValue ↔ ProtoObject at boundaries (script result, global object for interpreter). |
| **GCBridge** | Maps JSValue ↔ ProtoObject for legacy path; on protoCore path used only at boundary. |

### 2.3 Absolute Rule: No std::vector for Execution State

Local variables and the operand stack **must not** use `std::vector` or any C++ container outside protoCore; the GC does not trace them. All interpreter state uses **ProtoContext** (e.g. `closureLocals`: slot keys by index, reserved key for stack as ProtoList). See `src/runtime/README.md` and `ARCHITECTURE.md` § 1.3a.

---

## 3. ProtoInterpreter (Phase 3) — Opcode Coverage

The interpreter implements the main QuickJS opcode groups used on the protoCore path:

- **Stack and constants:** `push_*`, `dup*`, `swap*`, `rot*`, constant pool, atom-based loads.
- **Locals and arguments:** `get/put/set_loc*`, `get/put/set_arg*`, `put_arg0`–`set_arg3`, var-ref and scope opcodes; slots in `closureLocals` by index.
- **Properties and arrays:** `get/put_field*`, `define_field`, `get/put_array_el*` via ProtoObject attributes and interned keys (e.g. `"length"`, numeric indices).
- **Control flow:** `goto*`, `if_true*`, `if_false*` using `toBool`; `return`/`return_undef`.
- **Calls:** `OP_call`, `OP_call_method`, `OP_tail_call_method` (bytecode and ProtoMethod); `this` and arguments bound via child ProtoContext and `setSlot`.
- **Constructors:** `OP_call_constructor` (new object, call with `this`, constructor return value semantics).
- **Operators:** Comparison (`eq`, `neq`, `strict_eq`, `strict_neq`, `lt`, `lte`, `gt`, `gte`), logical (`and`, `or`), `typeof`, `instanceof`, `in`, `delete`; arithmetic already present.

**Unknown opcodes:** The `default` case logs `[ProtoInterpreter] unsupported opcode 0xNN at byte offset N` to stderr and returns; no silent fallback.

---

## 4. Eval Path and Configuration

- **Single path:** All script execution uses compile → load → run (protoCore). No legacy `JS_Eval()` for main script, REPL, or debugger eval. **Workers** and **Deferred** are implemented as **ProtoThread** (protoCore): each gets a JSContextWrapper and runs script via the same compile → load → run path, enabling native multithreading for JavaScript. TypeBridge/GCBridge and host function bridge at boundary.
- **Test262:** Runner in `tests/test262/runner/test262_runner.js`; use protoCore path via config or `PROTOJS_USE_PROTO_EVAL=1`.

---

## 5. Testing and Conformance

- **Directed smoke:** `node tests/test262/runner/proto_eval_smoke.js` runs protojs with protoCore on a short list of expressions (arithmetic, typeof, comparison, Array.isArray); expects exit code 0.
- **Test262:** Runner in `tests/test262/runner/test262_runner.js`; config in `tests/test262/config/test262_paths.json`. Default `test262_root: "../test262"` (sibling repo); override with `TEST262_ROOT`. With `use_proto_eval`, every test runs on the protoCore path; snapshots in `tests/test262/reports/`.
- **Conformance report:** `CONFORMANCE_JS.md` documents scope, Phase 6 (protoCore) table (e.g. built-ins/Array/isArray 29/29), and Phase 3 / protoCore path support. RegExp `lastIndex` is kept on QuickJS for spec compliance.

---

## 6. Build and Dependencies

- **Build:** CMake; C++20; depends on protoCore (shared library). protoCore built first; protoJS links to it (RPATH set for sibling build).
- **Key directories:** `src/` (runtime, modules, npm, profiling, etc.), `src/runtime/` (ProtoInterpreter, bytecode load/compile), `tests/`, `docs/`.

---

## 7. Documentation Map

| Document | Purpose |
|----------|---------|
| **README.md** | Overview, build, usage, CLI options. |
| **ARCHITECTURE.md** | System architecture, Option B, eval paths, TypeBridge, ExecutionEngine, Phase 4–9. |
| **src/runtime/README.md** | ProtoCore runtime (Option B): flow, no-std::vector rule, Phase 3–9, enabling protoCore path. |
| **CONFORMANCE_JS.md** | Test262 methodology, Phase 6 table (protoCore), RegExp, language conformance notes. |
| **DOCUMENTATION_INDEX.md** | Index of all project documentation. |

---

## 8. Risks and Limitations

- **Not production-ready:** Project is open for community review; edge cases and performance may require further work.
- **Conformance:** Only selected Test262 patterns have been run on the protoCore path; full suite not yet validated.
- **Missing opcodes:** Any bytecode not yet implemented in ProtoInterpreter will trigger the unknown-opcode log and return; adding opcodes or falling back to legacy path for specific features may be needed.
- **RegExp:** Intentionally left on QuickJS for correct `lastIndex` semantics on the legacy path.
- **Workers and Deferred:** Both run as ProtoThread (protoCore); each task/worker has a JSContextWrapper and uses compile → load → run. Native multithreading for JavaScript is provided by protoCore’s ProtoThread.

---

## 9. Recommendations and Next Steps

1. **Expand Test262 on protoCore:** Run additional patterns (e.g. language/expressions, language/statements), record results in `CONFORMANCE_JS.md`, and address missing opcodes or built-ins.
2. **Phase 6 native global:** Implemented: global is a ProtoObject built on first eval from the QuickJS global; `runBytecode` takes `pGlobalRoot` and updates it on put_field/define_field so top-level var persists. Directed test: `node tests/test262/runner/proto_eval_smoke.js` (6 cases).
3. **Further Test262 conformance:** Run additional patterns and fix missing opcodes/built-ins; document in `CONFORMANCE_JS.md`.
4. **Stability:** Run smoke and selected Test262 regularly after interpreter or loader changes; fix regressions and document new opcodes.

---

**Audit complete.** For detailed design and API, see `ARCHITECTURE.md` and `src/runtime/README.md`.
