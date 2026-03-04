# Conformity Test Suite — Progress Status

This document tracks implementation status of the **TEST_PLAN.md** (Phase 1: Semantic Conformity). Update it as tests are added, run, or fixed.

**Last updated:** 2026-03-03

---

## Phase 1.1: Built-in Type Isolation

| ID    | Area        | protoPython | protoJS | Notes |
|-------|-------------|-------------|---------|-------|
| 1.1.1 | int/Number  | ✅ Implemented | ✅ Implemented | `test_int_conformity.py`, `test_number_conformity.js` |
| 1.1.2 | str/String  | ✅ Implemented | ✅ Implemented | `test_str_conformity.py`, `test_string_conformity.js` |
| 1.1.3 | list/Array  | ✅ Implemented | ✅ Implemented | `test_list_conformity.py`, `test_array_conformity.js` |
| 1.1.4 | dict/Object | ✅ Implemented | ✅ Implemented | `test_dict_conformity.py`, `test_object_conformity.js` |

**Run:**  
- protoPython: `python tests/conformity/run_conformity.py` (or run each under `tests/conformity/builtins/`).  
- protoJS: `node tests/conformity/run_conformity.js` (or run each under `tests/conformity/builtins/`).

---

## Phase 1.2: Cross-Module Visibility and Identity

| ID    | Area              | protoPython | protoJS | Notes |
|-------|-------------------|-------------|---------|-------|
| 1.2.1 | Module identity   | ✅ Verified    | ⚠️ Failing | `test_module_identity.py` (protopy), `tests/conformity/import/test_module_identity.js` (protoJS — malloc crash, indicates loader bug) |
| 1.2.2 | Wrapper vs content| ✅ Verified    | ⏳ N/A* | `test_wrapper_content.py` (protopy) |
| 1.2.3 | Re-import         | ✅ Verified    | ⏳ N/A* | `test_reimport.py` (protopy) |

\* protoJS: wrapper/content and re-import tests will be added when UMD/module resolution is exercised at the JS layer; current identity test already stresses CommonJSLoader/module cache.

**const_cast check:**  
- protoPython: `tests/conformity/scripts/check_const_cast.sh` (run from protoPython root). It greps for `const_cast<...ProtoObject...>` in `PythonEnvironment.cpp`, `ExecutionEngine.cpp`, `SysModule.cpp`. **Current state:** script fails (many hits); the Assertion of Immutability requires *no illegal mutation of shared state* (module wrappers, cache). Passing the contract means either removing forbidden uses or documenting allowed ones; the script is a reminder to review.  
- protoJS: reuse same idea in C++ paths that touch module cache/wrappers when implemented.

---

## Phase 1.3: Bootstrap Conformity Suite

| Item | protoPython | protoJS | Notes |
|------|-------------|---------|-------|
| Manifest | ✅ `tests/conformity/bootstrap/cpython_bootstrap.txt` | ✅ `tests/conformity/bootstrap/test262_bootstrap.txt` | Paths + rationale in comments |
| Runner   | ✅ `tests/conformity/run_conformity.py` | ✅ `tests/conformity/run_conformity.js` | Uses manifest or discovers builtins/import |

---

## Immutability Assertion Checklist

Before marking a milestone complete:

- [ ] **No illegal mutation:** `check_const_cast.sh` passes (protoPython); no `const_cast` on shared module/cache roots.
- [ ] **Root propagation:** Code review confirms `setAttribute` return values are propagated to owner (frame, cache, space).
- [ ] **Cache consistency:** Phase 1.2 tests pass (module identity and wrapper content).

---

## How to Update This File

- When adding a new conformity test: add a row or update the table; set status to ✅ Implemented and the file path.
- When a test is fixed and green: keep ✅; add a short note if relevant.
- When Phase 2 (performance) work starts: add a new section "Phase 2" with the same table style.
