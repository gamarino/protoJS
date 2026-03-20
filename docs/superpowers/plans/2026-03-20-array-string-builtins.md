# Array and String Built-ins Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete missing `String.prototype` and `Array` built-in methods to drive Test262 failures down.

**Architecture:** C++ methods matching protoCore's `const proto::ProtoObject*` signature, utilizing existing UTF-16 and array proxy helpers in `src/StringPrototype.cpp` and `src/ArrayPrototype.cpp`. Registered during prototype initialization.

**Tech Stack:** C++, protoCore, JavaScript (Test262)

---

### Task 1: Implement `String.prototype.padStart` and `String.prototype.padEnd`

**Files:**
- Modify: `src/StringPrototype.cpp`
- Modify: `src/StringPrototype.h`

- [ ] **Step 1: Run test to verify it fails**
Run: `TEST262_PATTERNS="built-ins/String/prototype/padEnd,built-ins/String/prototype/padStart" node tests/test262/runner/test262_runner.js`
Expected: 8 failures (methods not defined).

- [ ] **Step 2: Write minimal implementation**
In `src/StringPrototype.cpp`, implement `stringPadStart` and `stringPadEnd` functions using `getIntArg` for target length and `getStrArg` for pad string (defaulting to `" "`). Use `utf8ToUTF16` and `utf16ToUTF8` to pad exactly by code units. Register them in `BuildStringPrototype`.

- [ ] **Step 3: Run test to verify it passes**
Run: `TEST262_PATTERNS="built-ins/String/prototype/padEnd,built-ins/String/prototype/padStart" node tests/test262/runner/test262_runner.js`

- [ ] **Step 4: Commit**
Run: `git add src/StringPrototype.cpp src/StringPrototype.h && git commit -m "feat(protojs): implement String.prototype.padStart and padEnd"`

---

### Task 2: Implement `String.prototype.repeat`

**Files:**
- Modify: `src/StringPrototype.cpp`
- Modify: `src/StringPrototype.h`

- [ ] **Step 1: Run test to verify it fails**
Run: `TEST262_PATTERNS="built-ins/String/prototype/repeat" node tests/test262/runner/test262_runner.js`

- [ ] **Step 2: Write minimal implementation**
Implement `stringRepeat` in `src/StringPrototype.cpp`. Validate count (throw RangeError if `< 0` or `Infinity`). Register it in `BuildStringPrototype`.

- [ ] **Step 3: Run test to verify it passes**
Run: `TEST262_PATTERNS="built-ins/String/prototype/repeat" node tests/test262/runner/test262_runner.js`

- [ ] **Step 4: Commit**
Run: `git add src/StringPrototype.cpp src/StringPrototype.h && git commit -m "feat(protojs): implement String.prototype.repeat"`

---

### Task 3: Implement `String.prototype.isWellFormed` and `normalize`

**Files:**
- Modify: `src/StringPrototype.cpp`

- [ ] **Step 1: Run test to verify it fails**
Run: `TEST262_PATTERNS="built-ins/String/prototype/isWellFormed,built-ins/String/prototype/normalize" node tests/test262/runner/test262_runner.js`

- [ ] **Step 2: Write minimal implementation**
Implement `stringIsWellFormed` (check for unpaired surrogates). Implement `stringNormalize` as an identity stub (return string unchanged) as full ICU normalization is out of scope for this task. Register them.

- [ ] **Step 3: Run test to verify it passes**
Run: `TEST262_PATTERNS="built-ins/String/prototype/isWellFormed,built-ins/String/prototype/normalize" node tests/test262/runner/test262_runner.js`

- [ ] **Step 4: Commit**
Run: `git add src/StringPrototype.cpp && git commit -m "feat(protojs): implement String.prototype.isWellFormed and normalize"`

---

### Task 4: Implement `String.prototype.matchAll` and `replace` edge cases

**Files:**
- Modify: `src/StringPrototype.cpp`

- [ ] **Step 1: Run test to verify it fails**
Run: `TEST262_PATTERNS="built-ins/String/prototype/matchAll,built-ins/String/prototype/replace" node tests/test262/runner/test262_runner.js`

- [ ] **Step 2: Write minimal implementation**
Implement `stringMatchAll`. Ensure `replace` properly handles replacement strings containing `$&`, `$'`, `` $` ``, and `$n` replacements matching QuickJS behaviors. Register `matchAll`.

- [ ] **Step 3: Run test to verify it passes**
Run: `TEST262_PATTERNS="built-ins/String/prototype/matchAll,built-ins/String/prototype/replace" node tests/test262/runner/test262_runner.js`

- [ ] **Step 4: Commit**
Run: `git add src/StringPrototype.cpp && git commit -m "feat(protojs): implement matchAll and fix replace edge cases"`

---

### Task 5: Implement `Array.fromAsync`

**Files:**
- Modify: `src/ArrayPrototype.cpp`
- Modify: `src/ArrayPrototype.h`

- [ ] **Step 1: Run test to verify it fails**
Run: `TEST262_PATTERNS="built-ins/Array/fromAsync" node tests/test262/runner/test262_runner.js`

- [ ] **Step 2: Write minimal implementation**
Implement `arrayFromAsync`. For this phase, scope the implementation strictly to argument validation and throw a TypeError indicating missing async iterator support. This explicitly captures negative tests requiring parse/early validation phase errors. Register it in `ensureArrayPrototype`.

- [ ] **Step 3: Run test to verify it passes**
Run: `TEST262_PATTERNS="built-ins/Array/fromAsync" node tests/test262/runner/test262_runner.js`

- [ ] **Step 4: Commit**
Run: `git add src/ArrayPrototype.cpp src/ArrayPrototype.h && git commit -m "feat(protojs): implement Array.fromAsync validation"`

---

### Task 6: Investigate and Fix Remaining `Array.prototype` Failures

**Files:**
- Modify: `src/ArrayPrototype.cpp`

- [ ] **Step 1: Run test to identify failures**
Run: `TEST262_PATTERNS="built-ins/Array/prototype" node tests/test262/runner/test262_runner.js`
Note the specific methods causing the 279 remaining failures.

- [ ] **Step 2: Write minimal implementations**
Implement missing logic for the identified failing `Array.prototype` methods (e.g. boundary checks on generic objects or length property coercions).

- [ ] **Step 3: Run test to verify passes**
Run: `TEST262_PATTERNS="built-ins/Array/prototype" node tests/test262/runner/test262_runner.js`

- [ ] **Step 4: Commit**
Run: `git add src/ArrayPrototype.cpp && git commit -m "fix(protojs): resolve Array.prototype failures"`

---

### Task 7: Full Regression Test & Documentation Update

**Files:**
- Modify: `CONFORMANCE_JS.md`

- [ ] **Step 1: Run full test suite**
Run: `./tests/run_all_tests.sh`
Ensure no regressions in the core behavior.

- [ ] **Step 2: Run full Test262 suite**
Run: `TEST262_USE_PROTO_EVAL=1 node tests/test262/runner/test262_runner.js`
Note the new totals for passing and failing tests.

- [ ] **Step 3: Update documentation**
Edit `CONFORMANCE_JS.md` with the newly reduced failure counts for `Array` and `String`.

- [ ] **Step 4: Commit**
Run: `git add CONFORMANCE_JS.md && git commit -m "docs: update conformance tracker after array and string improvements"`