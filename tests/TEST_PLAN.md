# Test Plan: Conformity and Performance — Verification Contract for Agents

**Version:** 1.0  
**Audience:** Antigravity and other system architects / verification agents  
**Scope:** protoPython and protoJS on the immutable protoCore engine  
**Objective:** Achieve semantic parity with CPython 3.14 (and optionally ECMAScript/Test262) using exclusively the immutable protoCore engine. Correctness over structures that respect immutability is prioritised before throughput optimisation.

---

## Progress Status

Implementation status and run instructions are maintained in **CONFORMITY_PROGRESS.md** in this repo. Summary:

- **Phase 1.1 (Built-ins):** Implemented for protoPython and protoJS under `tests/conformity/builtins/`.
- **Phase 1.2 (Import):** Implemented for protoPython under `tests/conformity/import/`; protoJS to be added when module resolution is in scope.
- **Phase 1.3 (Bootstrap):** Manifests and runners in place (`cpython_bootstrap.txt`, `test262_bootstrap.txt`, `run_conformity.py`, `run_conformity.js`).
- **Immutability check:** `protoPython/tests/conformity/scripts/check_const_cast.sh` for forbidden `const_cast` in module paths.

Keep CONFORMITY_PROGRESS.md updated as tests are added or pass/fail status changes.

---

## Mission Context

You are acting as the **lead architect** for protoPython and protoJS. The runtime must reach parity with the reference implementation (CPython 3.14 / V8 or Test262) while **only** using the immutable object model of protoCore. This plan is a **two-phase verification contract**: first prove semantic correctness on immutable structures, then measure and improve performance.

---

## Golden Rule of protoCore (Invariant)

**Any "write" operation (e.g. `setAttribute`, `OP_STORE`, dict/list update) is a pure function that returns a new root of an AVL (or equivalent persistent) structure.**

- If the **returned root is not propagated** to the logical owner (e.g. the wrapper in `sys.modules`, the SharedModuleCache, the frame’s locals, or the space’s `mutableRoot`), this is a **critical design failure**.
- **Known violation:** The "modWrapper bug": updating the module’s content via `mod = mod->setAttribute(...)` then mutating the wrapper with `const_cast<...>(modWrapper)->setAttribute(ctx, "val", mod)` instead of (a) propagating the new wrapper to the cache/owner and (b) updating the cached module root so future resolution sees the new graph.
- **Immutability assertion (backend):** No use of `const_cast` to perform in-place mutation on objects that participate in shared state (module wrappers, cached roots, globals). Every such update must go through the pure `setAttribute`-returns-new-root pattern and the new root must be stored in the appropriate owner (space, cache, or wrapper holder).

---

## Phase 1: Semantic Conformity (Correctness)

Correctness is validated **before** any performance milestone. All Phase 1 tests must pass with the immutability assertions below enforced.

### 1.1 Built-in Type Isolation

**Goal:** Ensure that base types (`int`, `str`, `list`, `dict` in protoPython; `Number`, `String`, `Array`, `Object` in protoJS) are implemented as immutable AVL/persistent structures in protoCore, and that the language layer’s "mutable" API is implemented by always propagating new roots.

| # | Test area | Description | Immutability assertion |
|---|-----------|-------------|-------------------------|
| 1.1.1 | int / Number | Arithmetic, coercion, identity (e.g. small int cache) | No in-place mutation of numeric objects; operations return new ProtoObject roots. |
| 1.1.2 | str / String | Concatenation, slicing, methods (e.g. `strip`, `split`) | All operations return new roots; no `const_cast` on string buffers. |
| 1.1.3 | list / Array | Append, extend, index assign, slice | `ProtoList` (or equivalent) updates use structural sharing; result is a new root and is stored back into the owning scope/cache. |
| 1.1.4 | dict / Object | Get/set/delete key, iteration | Sparse list / map updates return new root; the new root is propagated to the object’s owner (e.g. frame, module, global dict). |

**Deliverable:** A dedicated test suite (e.g. `tests/conformity/builtins/`) that exercises the above for both protoPython and protoJS, with a single shared checklist that can be run by an agent to confirm "no illegal mutation" (see § Assertion of Immutability below).

---

### 1.2 Cross-Module Visibility and Identity

**Goal:** Verify that the import system exposes a single, up-to-date view of each module: resolution returns the **latest** version of the immutable graph (no stale wrapper or cached module root after a module is "executed" or updated).

| # | Test area | Description | Immutability assertion |
|---|-----------|-------------|-------------------------|
| 1.2.1 | Module identity | After `import M` and `M.x = value`, a second `import M` (or `getAttribute(sys.modules, "M")`) must see `M.x` as `value`. | The module object updated by execution is the one stored in the cache / `sys.modules` (or equivalent). No `const_cast` on the module wrapper; cache or moduleRoots must hold the new module root after execution. |
| 1.2.2 | Wrapper vs content | The wrapper returned by `getImportModule(ctx, logicalPath, "val")` (or "exports") must have its `"val"` (or "exports") attribute pointing to the **current** module root after any execution that mutates that module. | Either (a) the cache stores the module root and is updated with the new root when the module is executed, and the wrapper is built from that, or (b) the wrapper is updated via a pure setAttribute and the new wrapper is what is exposed (no in-place mutation of the wrapper). |
| 1.2.3 | Re-import and reload | Re-importing or reloading a module must not leave dangling references to old roots. | All references to the previous module root are replaced by the new root in the cache and in any scope that holds the module. |

**Deliverable:** Tests in `tests/conformity/import/` (or equivalent) that assert identity and attribute visibility across imports and execution, plus a script or CI step that greps the codebase for `const_cast` in import/module paths and fails if any remain that mutate shared wrappers or cached roots.

---

### 1.3 Bootstrap Conformity Suite

**Goal:** Define the **minimum** set of tests from the reference test suites that must pass before claiming "object model and core semantics" conformity.

| Runtime | Reference suite | Minimal bootstrap set | Criterion |
|---------|-----------------|------------------------|-----------|
| protoPython | CPython `Lib/test/` | Subset of tests that cover: object model, types (int, str, list, dict), import, namespaces, basic control flow. | No attempt to run the full standard library until this bootstrap set passes. |
| protoJS | Test262 (or equivalent) | Subset of tests for: built-in types, strict mode, modules (import/export), basic execution. | Same: bootstrap subset passes before broader compatibility. |

**Deliverable:** A single manifest file (e.g. `tests/conformity/bootstrap/cpython_bootstrap.txt` and `tests/conformity/bootstrap/test262_bootstrap.txt`) listing the exact test paths or names, plus a runner that executes them and reports pass/fail. Each entry in the manifest must have a short justification (e.g. "object identity", "dict update", "import visibility").

---

## Phase 2: Performance and Real-Time Latency

Phase 2 is only meaningful after Phase 1 is green. All Phase 2 tests assume immutability assertions hold.

### 2.1 Micro-Benchmarks: Cost of Structural Sharing

**Goal:** Quantify the cost of persistent updates (no in-place mutation) under heavy update load.

| # | Benchmark | Description | Metric |
|---|-----------|-------------|--------|
| 2.1.1 | Dict update storm | Repeated key set/delete on a single logical "dict" (protoCore sparse list / map). | Time and memory per N updates; compare to CPython/V8 for same N. |
| 2.1.2 | List append storm | Append N elements to a list (each append returns new root). | Throughput (ops/s) and memory growth; confirm structural sharing (e.g. O(log n) copy cost). |
| 2.1.3 | Nested structure update | Deep path update (e.g. `obj.a.b.c = value`) requiring multiple new roots along the path. | Latency per update and total allocations. |

**Immutability assertion:** Benchmarks must use only the public API (setAttribute / typed collections); no `const_cast` or internal mutation. The same code paths as production must be exercised.

---

### 2.2 GC and Pause Time ("Zero-Pause" Validation)

**Goal:** Ensure that the garbage collector does not introduce pauses above an acceptable threshold (e.g. 1 ms) under read-heavy, multi-threaded load.

| # | Test | Description | Success criterion |
|---|------|-------------|--------------------|
| 2.2.1 | Concurrent read load | Multiple threads perform sustained read-only operations (getAttribute, iteration, numeric ops). | No GC pause exceeding 1 ms (configurable threshold) over a fixed duration (e.g. 10 s). |
| 2.2.2 | Read + occasional write | One or more writer threads perform root-producing updates; readers observe the new roots. | Same pause threshold; no data races (readers never see torn state). |

**Immutability assertion:** Writers only install new roots via the prescribed APIs; no mutable shared state that would require locks during GC.

---

### 2.3 Throughput Comparison (proto vs Reference)

**Goal:** Provide a repeatable comparison against CPython and V8 (or Node) for a small set of workloads, to measure the benefit of removing the GIL and leveraging immutability for parallelism.

| # | Workload | Description | Metric |
|---|----------|-------------|--------|
| 2.3.1 | CPU-bound parallel | Embarrassingly parallel numeric or symbol work (e.g. map over a large list). | Speedup vs single-thread; comparison with CPython (GIL) and optional multiprocessing. |
| 2.3.2 | Mixed read/write | Concurrent readers and one writer updating a shared structure (new root propagation). | Throughput (ops/s) and fairness; no unbounded growth of old roots if GC is effective. |

**Deliverable:** A small benchmark harness (scripts or a single binary) that runs the above and outputs a short report (CSV or markdown) suitable for regression tracking.

---

## Assertion of Immutability (Verification for Agents)

Every milestone in this plan must satisfy the following **contract** before being marked complete:

1. **No illegal mutation:**  
   There is no use of `const_cast` (or equivalent) to modify an object that is (or could be) shared: module wrappers, cached module roots, `sys.modules` (or equivalent), globals, or any object reachable from `ProtoSpace::moduleRoots` or the SharedModuleCache.  
   **Verification:** Grep (or static check) for `const_cast` in:
   - protoPython: `src/library/` (especially `PythonEnvironment.cpp`, `ExecutionEngine.cpp`, `SysModule.cpp`) and any path that handles `getImportModule` or module execution.
   - protoJS: equivalent paths that resolve or update modules.
   - protoCore: only allow `const_cast` where explicitly documented and isolated (e.g. internal GC or legacy compatibility), not for "convenience" updates to shared state.

2. **Root propagation:**  
   For every call that performs a logical "write" (e.g. `setAttribute`, store op, dict set, list append), the **returned** root is either:
   - used as the new value for a local or temporary, and that value is then used in a subsequent write to the owner, or
   - written directly into the owner (e.g. frame locals, global dict, module cache, space’s mutableRoot via the documented CAS pattern).  
   **Verification:** Code review or lightweight static analysis that traces `setAttribute` return values and confirms they are not dropped when the target is shared state.

3. **Cache consistency:**  
   When a module is executed or its attributes are updated, the SharedModuleCache (and any `moduleRoots` or language-level `sys.modules`) is updated with the **new** module root so that the next resolution sees the updated graph.  
   **Verification:** Phase 1.2 tests (cross-module visibility) must pass; plus no pattern where the cache is written only on first load and never updated after execution.

---

## Deliverables Summary

| Item | Location / form |
|------|------------------|
| Phase 1.1 | Suite: `tests/conformity/builtins/` (protoPython + protoJS) |
| Phase 1.2 | Suite: `tests/conformity/import/` (or equivalent); plus grep/CI for `const_cast` in module paths |
| Phase 1.3 | Manifest: `tests/conformity/bootstrap/cpython_bootstrap.txt`, `test262_bootstrap.txt`; runner and rationale per test |
| Phase 2.1 | Micro-benchmarks: dict/list/nested update scripts or binary |
| Phase 2.2 | GC pause test: multi-thread read (and optional write) with pause threshold check |
| Phase 2.3 | Throughput harness and report format |
| Immutability | Checklist run before each release: (1) no illegal `const_cast`, (2) root propagation, (3) cache consistency |

---

## How to Use This Document (Agent Instructions)

- **Before implementing features:** Ensure any new "write" path follows the Golden Rule and the Assertion of Immutability; add a test to Phase 1 or 2 as appropriate.
- **Before marking a milestone done:** Run the corresponding Phase 1 or Phase 2 tests and the immutability verification (grep + root propagation review).
- **When fixing bugs (e.g. modWrapper):** Remove the illegal mutation and implement propagation of the new root to the cache/owner; add or extend a Phase 1.2 test to prevent regression.

This file is the **Verification Contract for Agents**: satisfying it is the condition for claiming conformity and performance of protoPython/protoJS on the immutable protoCore engine.
