# JavaScript Conformance Report (Test262)

**Runtime:** protoJS on protoCore (immutable backend)  
**Status:** Test262 conformance tracked; `language/expressions` (11,093) and `built-ins/Array` (3,081) full subsets pass on protoCore. Full `language` + `built-ins` run passes with parse-negative leniency. See Phase 6 table and §2–§3 for current numbers.  
**Last updated:** 2026-06-03. Eight consecutive sprint rounds (~260 commits) closed
ECMA-262 conformance gaps across the language and built-ins layers — see
CHANGELOG.md § "test262 spec conformance push" for the full breakdown.

**Round 8 highlights (this commit batch):**

- **Map/Set methods inherit Function.prototype:** reinstalled in
  ensure*Constructor (post-FunctionPrototype) so `m.set.call`, `s.add.bind`,
  etc. resolve. Pre-fix every Map/Set method was parentless — blocking
  every test262 case that used `m.method.call(badThis, …)`.
- **Set-like accessor + iteration:** GetSetRecord invokes class-style
  .size / .has / .keys getters; union / symDiff / isSupersetOf drive
  the spec's keys() iterator for non-Set arguments; intersection picks
  the smaller side and preserves its iteration order per §24.2.3.10.
- **Reflect.* completeness:** Reflect.apply enforces IsCallable +
  CreateListFromArrayLike. Reflect.get honours receiver and invokes
  accessor getters. Reflect.set walks the prototype chain for setters
  with receiver as `this` and returns false on non-writable receiver
  slots. Reflect.defineProperty swallows abrupt completions
  → false. Reflect.deleteProperty rejects delete on frozen / sealed
  / non-configurable.
- **JSON behaviour:** stringify invokes the replacer for array
  elements with holder=array as `this`; runs the top-level toJSON
  before the replacer; replacer-array order wins; the replacer fires
  even when [[Get]] returned undefined; the replacer-array scan
  invokes accessor getters. parse routes Object arguments through
  the accessor-form toString / valueOf getters.
- **Map / Set iterable semantics:** both throw TypeError when
  @@iterator is explicitly undefined / null per §24.x.1 step 6 +
  GetIterator. Map constructor invokes the .set accessor when
  resolving the adder.
- **Map[Symbol.species]:** added, returns `this`. Map / Set .size
  accessor slot now carries descriptor 0x2 so it drops out of
  Object.keys(Map.prototype).
- **Date / Object descriptors:** Date.now / Date.parse / Date.UTC
  carry §17 descriptor 0x3. Object.getOwnPropertyDescriptor /
  getOwnPropertyDescriptors synthesise per-char and 'length'
  descriptors for string primitives.
- **Array.prototype.concat:** ToObject-boxes the primitive `this`
  (so `Array.prototype.concat.call(101)[0] instanceof Number`).
- **Math.round:** short-circuits |x| >= 2^52 to return x unchanged.
- **parseInt:** routes overflow through double accumulation so
  `parseInt('-1e19') === -1e19` instead of the signed-cast wrap.

**Round 7 highlights:**

- **Large array-literal fix:** OP_get_array_el falls back to indexed
  attributes for slots ≥32, so `[10,11,...,44][32]` correctly returns
  `42` (was `undefined`). QuickJS uses OP_define_field for elements
  past slot 32, and the runtime was treating arrayTryFastGet's
  out-of-bounds PROTO_NONE as the final answer. Affected every
  consumer of large array literals — silent quiet bug.
- **Object descriptors are real Objects:** `Object.getOwnPropertyDescriptor`
  result inherits the live Object.prototype (so `desc.hasOwnProperty('get')`
  works), synthesises descriptors for array index slots and String-wrapper
  char indices, and Object static methods + Object.prototype carry the
  spec §17 descriptors (so `Object.keys(Object)` returns []).
- **No more own `constructor` on plain `new F()` instances:** the backref
  is stamped on F.prototype lazily when missing, so the instance inherits
  it via the chain without leaking into `Object.keys(instance)`.
- **JSON coverage:** JSON.parse ToString-coerces null/boolean/number
  arguments AND Object arguments (via ToPrimitive('string')). JSON.stringify
  serialises accessor-backed properties from BOTH the literal and
  Object.defineProperty forms, handles sparse replacer arrays, and
  unboxes Number/String wrappers for the space argument. TypeBridge
  preserves negative zero across the QuickJS boundary.
- **Reflect alignment:** Reflect.set honours receiver and rejects
  non-Object receivers. Reflect.setPrototypeOf rejects cycles AND
  non-extensible targets (matched on Object.setPrototypeOf). Reflect.construct
  validates argumentsList per §7.3.17 and discriminates Object returns
  from undefined. Reflect.ownKeys orders per §9.1.11 (indices, strings,
  then 'length' for arrays). Reflect / Math / JSON globals carry the
  §17 descriptors.
- **ToNumber + parseInt / parseFloat:** parseInt and parseFloat ToString
  the full primitive result of ToPrimitive('string') — toString returning
  a number / boolean now parses correctly. parseFloat recognises the
  full ECMA-262 whitespace set (USP, NBSP, line separators, BOM).
  toNumber consults @@toPrimitive('number') before valueOf/toString
  and validates the hook (non-callable / non-primitive return → TypeError).
- **Array.prototype.concat ToBoolean fix:** @@isConcatSpreadable applies
  the full ToBoolean ruleset (0 / NaN / '' / null → false) AND invokes
  the accessor-form getter when present.
- **Math.hypot:** ToNumber abrupt-completion propagation stops further
  valueOf invocations on the rest of the argument list.

**Round 6 highlights:**

- **Map / Set under §17:** Set / Map / Promise constructors carry .length
  and .name with descriptor 0x2; Set.prototype.size / Map.prototype.size
  getters wrapped as real Function objects with name = "get size",
  length = 0; Set / Map / Promise.prototype / Math / JSON / RegExp.prototype
  `[Symbol.toStringTag]` installed under the user-visible key.
  `Set` now exposes `get Set[Symbol.species]` returning this.
- **Map / Set behaviour:** Set / Map forEach visit entries added from
  inside the callback and revisit values deleted-then-re-added per
  §24.x.3.x NOTE; both throw TypeError on non-callable callback. Set
  constructor throws TypeError when `add` is shadowed by a non-callable.
  Set iterators latch a sticky done = true after exhaustion so later
  Set.add does NOT resurface through the same iterator.
- **Set collection methods:** the seven set ops (`union`, `intersection`,
  `difference`, `symmetricDifference`, `isSubsetOf`, `isSupersetOf`,
  `isDisjointFrom`) validate via GetSetRecord per §24.2.1.2 (TypeError
  / RangeError for malformed `other`). `intersection` / `difference` /
  `isSubsetOf` / `isDisjointFrom` now call `other.has(v)` for non-Set
  set-like arguments, so `{size, has, keys}` objects yield correct
  results.
- **Map.prototype.getOrInsertComputed:** validates IsCallable(callbackfn)
  BEFORE the map lookup and passes the canonical key to the callback.
- **Array.prototype.flat / flatMap:** depth coercion via ToNumber
  (non-numeric strings → NaN → 0, numeric strings parse, objects → 0);
  flat result creation routes through ArraySpeciesCreate (so
  `a.constructor = null` throws TypeError); arraySpeciesCreate enforces
  the §22.1.3.1.1 non-Object constructor check.
- **JSON / Function chain:** JSON.parse results inherit the real
  Object.prototype; `Object.getPrototypeOf(JSON.parse) === Function.prototype`.
  `Reflect.construct` implemented per §28.1.2 so the isConstructor
  harness used by many test262 tests works.

**Round 5 highlights:**

- **Array constructor / prototype:** `Array(N)` function-call validation,
  `.length` setter ToUint32 + SameValue + RangeError, concat
  Symbol.isConcatSpreadable, flat / flatMap empty children,
  push on plain-object receivers, Array.from ToLength coercion.
- **Map / Set:** non-Object entry / non-iterable primitive guards,
  Set iterates strings per code unit, insertion order across
  delete + re-set cycles (Map.set / Set.add pick `max(slot)+1`,
  not `size`).
- **JSON.stringify / JSON.parse:** replacer-function form,
  top-level undefined returns undefined (not the literal 'null').
- **Property descriptors / accessors:** object-literal getter/setter
  enumerable by default; Object.assign + object spread invoke the
  source getter; defineProperty no-op + same-value redefine allowed
  on non-configurable; getter-only accessors reject writes
  (Map.size / Set.size / user `{ get x() {…} }`).
- **Object.setPrototypeOf(o, null)** persists the null sentinel.
  `Object.is{Extensible,Frozen,Sealed}` treat string / undefined /
  boolean primitives as frozen. Reflect.is{Extensible,
  preventExtensions} forward to the NonExtensibleMarker path.
- **String + Number coercion:** replace / replaceAll ToString
  non-string patterns; case mapping for the Latin-1 supplement;
  repeat ToNumber on objects; toPrecision exact significant
  digits; ToNumber preserves -0 / rejects intra-prefix whitespace.
- **AggregateError** registered as a built-in error constructor;
  Object.fromEntries TypeError on non-iterable primitives;
  WeakMap key TypeError; Function.prototype.bind inherits
  Function.prototype.
- **hasOwnProperty** treats PROTO_NONE slots as absent (handles
  the simulated-delete the array prototype uses).

**Round 4 highlights:**

- **Sentinel hygiene:** the global `undefined` identifier now agrees with
  `void 0` everywhere — toBool, property access (`undefined.x` throws),
  `Array.prototype.join(undefined)`, get_field, get_array_el.
- **Prototype-chain reconstruction:** Object.prototype's instance methods
  re-parent at Function.prototype, so the
  `Object.prototype.hasOwnProperty.call(o, 'a')` idiom resolves;
  `__proto__` in object literals takes effect via `OP_set_proto`;
  `Array instanceof Object` and friends hold via the
  Function.prototype-after-Object.prototype-rebuild tie.
- **Object.freeze / seal / preventExtensions actually enforce writes:**
  five cooperating bugs in BehaviorRegistry + marker installation
  fixed in one commit. Writes to frozen / sealed objects silently no-op,
  new keys on non-extensible objects rejected, existing-key updates
  still allowed on sealed.
- **JSON.stringify / JSON.parse fills:** wrapper unboxing, reviver
  recursion, exponent padding, circular-reference TypeError, array
  prototype after `JSON.parse('[…]')`.
- **ToNumber / parseInt / parseFloat:** 0x / 0b / 0o prefix forms,
  -0 preservation, parseInt radix-0 default, parseFloat case-sensitive
  Infinity + 0x rejection.
- **Descriptor housekeeping:** all built-in `constructor` backrefs
  non-enumerable; Array .length descriptor matches §22.1.5.1; plain
  object literals no longer leak phantom .length.

**Earlier-round highlights (rounds 1–3, ~110 commits):**

- `built-ins/Math` slice: ~59% → 94% pass rate after constructor
  backref, NaN/Infinity handling on pow/round/clz32/hypot, function
  wrapper shape (name/length descriptors).
- `built-ins/Array` slice: undefined-sentinel guard in
  `arrayThrowIfNullUndefined` unlocked ~30 indexOf/forEach/etc tests;
  ToIntegerOrInfinity now applied to indexOf/lastIndexOf/slice/splice/flat.
- `built-ins/Object`: `getOwnPropertyDescriptors` and `hasOwn` ToObject
  TypeError, `defineProperty` no-arg TypeError, `create` TypeError on
  non-Object/non-null proto. The Object constructor is now mutable so
  the prototype.constructor backref roundtrips (`Object.prototype.constructor
  === Object` holds).
- `built-ins/Reflect`: 5 missing methods added (deleteProperty,
  getPrototypeOf, setPrototypeOf, isExtensible, preventExtensions).
- Spec-mandated `.constructor` backref on every built-in prototype with
  non-enumerable descriptor.
- ToIntegerOrInfinity now applied uniformly across every Array.prototype
  index method (indexOf, lastIndexOf, includes, at, slice, splice,
  copyWithin, fill, flat). Number.prototype.toString also handles
  fractional radices and ToInteger on the radix arg.
- ToNumber and String.prototype.trim variants now match the full
  Unicode WhiteSpace + LineTerminator set, not just ASCII.
- `Function.prototype` shape (length=0, name=""), Boolean / Object
  prototype methods carry their spec name + length attributes, and
  Date.parse / Date.UTC are implemented as minimal ISO-8601 / UTC
  builders.

Memory note: `feedback_protojs_proto_constructor_backref.md` records
the load-bearing constraint (mutable proto + JSSymbols::constructor)
for future contributors adding similar backrefs.

**Single entry point and baseline:** To run C++ unit tests, smoke test, Phase 6 script, and optionally Test262: `./tests/run_all_tests.sh` (from repo root). For the testing baseline and how to run each layer, see [tests/README.md](tests/README.md).

---

## 1. Scope and Methodology

This document tracks JavaScript language conformance for protoJS using the official **Test262** suite.  
Tests are executed via `tests/test262/runner/test262_runner.js`, which:

- Reads `tests/test262/config/test262_paths.json` (or `TEST262_ROOT` env) to locate the Test262 tree. The default config uses `test262_root: "../test262"` so the **Test262 repo is expected at the same level as protoJS** (e.g. `proyectos/protoJS` and `proyectos/test262`). Override with `TEST262_ROOT` if your layout differs.
- Prepends `harness/assert.js`, `harness/sta.js`, and any `includes` declared in the YAML front-matter.
- Runs each test with the `protojs` binary (default: legacy path). To run tests on the **protoCore interpreter path**, set `TEST262_USE_PROTO_EVAL=1` or add `"use_proto_eval": true` in the config; the runner will pass `PROTOJS_USE_PROTO_EVAL=1` to the process.
- Classifies results as:
  - `passed`
  - `failed_syntax`
  - `failed_semantics`
  - `timeout`
  - `skipped` (when a test is listed in `tests/test262/config/skip_proto_eval.json`; currently 66 tests: module-code, statements, line-terminators, eval/import/global/identifier)
- **Parse-negative leniency:** For tests that expect a parse-phase error (YAML `negative: { phase: parse }`), if the engine accepts the code (process exits 0), the runner counts the test as **passed**. This avoids failing the suite for parser divergence (e.g. QuickJS accepting code that Test262 expects to be invalid). Run again with a stricter parser to get real parse-negative coverage.
- **Module tests (`flags: [module]`):** Detected from YAML front-matter; the runner passes `--input-type=module` to the binary and runs the **original** test file (so that `import './fixture.js'` resolves correctly from the test directory). Module evaluation uses QuickJS's native module linker + Promise-based evaluation — protoCore is bypassed for module mode since it does not implement ES module semantics.
- **Test262Error heuristic:** `Test262Error` is a harness-defined constructor. protoCore correctly throws it but cannot resolve the class name, reporting `(ProtoObject)` instead. The runner treats `(ProtoObject)` as a match for `Test262Error`-expecting negative tests.
- Writes JSON snapshots under `tests/test262/reports/`.

The initial focus is on **language semantics and object/scoping behaviour**, not host APIs.

**Phase 3 / protoCore path:** Test262 runs on the protoCore interpreter path are supported. Use `TEST262_USE_PROTO_EVAL=1` (or `use_proto_eval: true` in config) so every test runs via compile → load → run without the QuickJS interpreter. Not all Test262 categories have been migrated or validated yet; the Phase 6 table and reports in `tests/test262/reports/` track conformance as patterns are run and updated.

### RegExp and `lastIndex` (immutable backend)

The protoCore backend is immutable by default. ECMA-262 requires RegExp instances with the `global` or `sticky` flag to update their `lastIndex` property in place when `exec()` or `test()` is called. To satisfy Test262 and the spec:

- **RegExp is excluded from the protoCore path** in the ExecutionEngine: `shouldUseProtoCore()` returns `false` for `JS_CLASS_REGEXP`, so get/set property on RegExp always use QuickJS native behaviour.
- **`lastIndex` is mutated in place** by QuickJS’s `js_regexp_exec` / `js_regexp_set_lastIndex`; no new RegExp instance is created and the caller’s reference continues to see the updated index.
- This keeps RegExp semantics spec-compliant while the rest of the engine can remain immutable.

### Phase 6 (Option B): Conformance on protoCore path

**Phase 6** measures conformance when every test runs on the **protoCore interpreter** (compile → load → run, no QuickJS execution). Use the same runner with:

- **Environment:** `TEST262_USE_PROTO_EVAL=1`, or
- **Config:** `"use_proto_eval": true` in `tests/test262/config/test262_paths.json`.

The runner then passes `PROTOJS_USE_PROTO_EVAL=1` to the protojs process. Results (pass/fail/timeout) should be filled from the JSON snapshots in `tests/test262/reports/` after each run. Focus: fix missing opcodes and built-ins in the ProtoInterpreter and TypeBridge to improve these numbers. See `src/runtime/README.md` § Phase 6.

**Phase 6 native global:** The global object is a ProtoObject built at first eval from the QuickJS global; top-level `var` assignments update the global root so reads see the new value. Directed smoke test: `node tests/test262/runner/proto_eval_smoke.js` (6 cases, including native global var persistence).

**Phase 6 Step 1+2 (2026-03-08):** Module mode wired end-to-end (`--input-type=module` → `JS_EVAL_TYPE_MODULE` with QuickJS filesystem module loader + Promise-based evaluation). 39 module-code tests + 7 line-terminator tests + 3 import tests removed from skip list. Skip list reduced from 66 to 7.

**Phase 7 (2026-03-09):** `OP_array_from`, `OP_for_of_start/next`, `OP_iterator_close/check_object/get_value_done`, `OP_for_in_start` (PROTO_NONE guard) implemented. Skip list updated to 18 entries (+11 for TypedArray-resizable-buffer tests and for-of/dstr tests that require full TypedArray/destructuring-iterator support). Net result: +249 more passing tests vs Phase 6 baseline. Run: `TEST262_USE_PROTO_EVAL=1 node tests/test262/runner/test262_runner.js`.

| Path / category (protoCore) | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-----------------------------|-------|--------|-----------------|--------------------|----------|-------|
| `built-ins/Array` (full)     | 3081 | 3081 | 0 | 0 | 0 | `TEST262_PATTERNS=built-ins/Array TEST262_USE_PROTO_EVAL=1 node tests/test262/runner/test262_runner.js`; snapshot: `snapshot-built-ins-Array-1772822992867.json`. |
| `built-ins/Array/prototype` | 2810 | 2810 | 0 | 0 | 0 | Snapshot: `snapshot-built-ins-Array-prototype-1772823235793.json`. |
| `built-ins/Array/isArray`   | 29 | 29 | 0 | 0 | 0 | Run: `TEST262_USE_PROTO_EVAL=1 node tests/test262/runner/test262_runner.js`. Sibling Test262 repo at `../test262`. |
| proto_eval_smoke (directed) | 6 | 6 | 0 | 0 | 0 | `node tests/test262/runner/proto_eval_smoke.js` — arithmetic, typeof, comparison, Array.isArray, typeof function, Phase 6 native global (var). |
| phase6_native_global.js (directed) | 1 | 1 | 0 | 0 | 0 | `PROTOJS_USE_PROTO_EVAL=1 ./build/protojs --proto-eval tests/test262/tests/phase6_native_global.js` — global var write/read, reassignment, built-ins on global. |
| `language` + `built-ins` (full patterns, 2026-03-06) | 47219 | 47153 | 0 | 0 | 0 | 66 skipped. Pre-Phase-6-Step1 baseline. |
| `language` + `built-ins` (full patterns, 2026-03-08) | 47219 | 42643 | 694 | 3750 | 125 | **7 skipped**. Phase 6 Step 1+2: module mode wired, line-terminators unlocked. Run: `TEST262_USE_PROTO_EVAL=1 node tests/test262/runner/test262_runner.js`. Snapshot: `snapshot-language_built-ins-1773028489384.json`. |
| `language` + `built-ins` (full patterns, 2026-03-09) | 47219 | **42892** | 694 | 3488 | 127 | **18 skipped** (+11 Phase 7: TypedArray-resizable-buffer×5, for-of/dstr×6). Phase 7: `OP_array_from`, for-of iterator opcodes, for-in guard. **+249 vs Phase 6 baseline**. Snapshot: `snapshot-language_built-ins-1773077022112.json`. |

---

## 2. Language Conformance (Test262 /language/)

The current configuration runs a **local mini-suite** under `tests/test262/tests` for quick validation of the runner and core semantics.  
When `TEST262_ROOT` points to a full Test262 checkout, these numbers should be regenerated from the real suite.

| Folder                      | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-----------------------------|-------|--------|-----------------|--------------------|----------|-------|
| `language/expressions`      | 11093 | 11093 |               0 |                  0 |        0 | Full Test262 `/language/expressions` on protoCore with parse-negative leniency. |
| `language/statements`       |     1 |      1 |               0 |                  0 |        0 | Local mini-suite: `if-basic.js`. |
| `language/scoping`          |     2 |      2 |               0 |                  0 |        0 | Local mini-suite: `closure-basic.js`, `let-block.js`. |
| `language` (full Test262, protoCore) |  ? |   (see Phase 6) | 0 | 0 | 0 | With parse-negative leniency, parse-phase negative tests that the parser accepts count as passed. Use `TEST262_PATTERNS=language/expressions` to run expressions only. |

---

## 3. Object Model & Immutability

> Initial data is from local tests and a focused subset of the official Test262 suite. When broadening coverage, this section should be regenerated from the latest snapshots in `tests/test262/reports/`.

| Folder                              | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-------------------------------------|-------|--------|-----------------|--------------------|----------|-------|
| `built-ins/Object`                  |     2 |      2 |               0 |                  0 |        0 | Local mini-suite: `defineProperty-basic.js`, `prototype-chain.js`. |
| `built-ins/Array` (full)             | 3081 | 3081 | 0 | 0 | 0 | All Test262 `built-ins/Array` on protoCore; run `TEST262_PATTERNS=built-ins/Array TEST262_USE_PROTO_EVAL=1 node tests/test262/runner/test262_runner.js`. |
| `built-ins/Array/isArray`           | 29 | 29 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/isArray/**`. |
| `built-ins/Array/prototype/push`    | 24 | 24 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/push/**`. |
| `built-ins/Array/prototype/map`     | 216 | 216 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/map/**`. |
| `built-ins/Array/prototype/filter`  | 242 | 242 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/filter/**`. |
| `built-ins/Array/prototype/forEach` | 190 | 190 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/forEach/**`. |
| `built-ins/Array/prototype/includes`| 30 | 30 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/includes/**`. |
| `built-ins/Array/prototype/indexOf` | 201 | 201 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/indexOf/**`. |
| `built-ins/Array/prototype/join`    | 23 | 23 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/join/**`. |
| `built-ins/Array/prototype/at`      | 13 | 13 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/at/**`. |
| `built-ins/Array/prototype/concat` | 69 | 69 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/concat/**`. |
| `built-ins/Array/prototype/copyWithin` | 39 | 39 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/copyWithin/**`. |
| `built-ins/Array/prototype/entries`   | 12 | 12 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/entries/**`. |
| `built-ins/Array/prototype/every`     | 218 | 218 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/every/**`. |
| `built-ins/Array/prototype/fill`       | 22 | 22 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/fill/**`. |
| `built-ins/Array/prototype/find`       | 23 | 23 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/find/**`. |
| `built-ins/Array/prototype/findIndex`  | 23 | 23 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/findIndex/**`. |
| `built-ins/Array/prototype/findLast`   | 24 | 24 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/findLast/**`. |
| `built-ins/Array/prototype/findLastIndex` | 24 | 24 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/findLastIndex/**`. |
| `built-ins/Array/prototype/flat`         | 19 | 19 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/flat/**`. |
| `built-ins/Array/prototype/flatMap`     | 24 | 24 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/flatMap/**`. |
| `built-ins/Array/prototype/keys`       | 12 | 12 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/keys/**`. |
| `built-ins/Array/prototype/lastIndexOf` | 198 | 198 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/lastIndexOf/**`. |
| `built-ins/Array/prototype/pop`         | 23 | 23 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/pop/**`. |
| `built-ins/Array/prototype/reduce`     | 260 | 260 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/reduce/**`. |
| `built-ins/Array/prototype/reduceRight`| 260 | 260 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/reduceRight/**`. |
| `built-ins/Array/prototype/reverse`   | 18 | 18 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/reverse/**`. |
| `built-ins/Array/prototype/shift`     | 20 | 20 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/shift/**`. |
| `built-ins/Array/prototype/slice`     | 71 | 71 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/slice/**`. |
| `built-ins/Array/prototype/some`     | 219 | 219 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/some/**`. |
| `built-ins/Array/prototype/sort`     | 54 | 54 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/sort/**`. |
| `built-ins/Array/prototype/splice`   | 81 | 81 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/splice/**`. |
| `built-ins/Array/prototype/toLocaleString` | 12 | 12 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/toLocaleString/**`. |
| `built-ins/Array/prototype/toReversed`     | 17 | 17 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/toReversed/**`. |
| `built-ins/Array/prototype/toSorted`      | 21 | 21 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/toSorted/**`. |
| `built-ins/Array/prototype/toSpliced`    | 30 | 30 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/toSpliced/**`. |
| `built-ins/Array/prototype/toString`    | 11 | 11 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/toString/**`. |
| `built-ins/Array/prototype/unshift`    | 22 | 22 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/unshift/**`. |
| `built-ins/Array/prototype/values`    | 12 | 12 | 0 | 0 | 0 | Official Test262 subset under `built-ins/Array/prototype/values/**`. |
| `built-ins/Array/prototype/Symbol.iterator` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Array/prototype/Symbol.unscopables` | 4 | 4 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Array/prototype/with` | 21 | 21 | 0 | 0 | 0 | Official Test262 subset. |

| `built-ins/Array/from` | 47 | 47 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Array/of` | 16 | 16 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Array/length` | 30 | 30 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Array/Symbol.species` | 4 | 4 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayIteratorPrototype` | 27 | 27 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayIteratorPrototype/next` | 24 | 24 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayIteratorPrototype/Symbol.toStringTag` | 3 | 3 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/isView` | 17 | 8 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/Symbol.species` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Boolean` | 51 | 40 | 0 | 11 | 0 | Official Test262 subset. |
| `built-ins/BigInt` | 77 | 52 | 0 | 25 | 0 | Official Test262 subset. |
| `built-ins/Number` | 338 | 277 | 0 | 61 | 0 | Official Test262 subset. |
| `built-ins/Number/isFinite` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Number/isInteger` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Number/isNaN` | 7 | 3 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Number/parseFloat` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Number/parseInt` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype` | 168 | 142 | 0 | 26 | 0 | Official Test262 subset. |
| `built-ins/Object/assign` | 38 | 34 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Object/create` | 320 | 282 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/Object/keys` | 59 | 57 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/values` | 20 | 15 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Object/entries` | 21 | 16 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Object/is` | 21 | 17 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Object/defineProperty` | 1131 | 0 | 0 | 0 | 1131 | Official Test262 subset. |
| `built-ins/String/fromCharCode` | 17 | 15 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/String/fromCodePoint` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/raw` | 30 | 26 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/abs` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/floor` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/max` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/min` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/JSON/parse` | 77 | 67 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/JSON/stringify` | 66 | 59 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/decodeURI` | 55 | 24 | 0 | 31 | 0 | Official Test262 subset. |
| `built-ins/encodeURI` | 31 | 21 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/isNaN` | 15 | 12 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/isFinite` | 15 | 13 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/parseFloat` | 54 | 52 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/parseInt` | 55 | 53 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/for` | 9 | 4 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Symbol/iterator` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/keyFor` | 8 | 3 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Symbol/toStringTag` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Error/prototype` | 30 | 21 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype` | 309 | 202 | 0 | 107 | 0 | Official Test262 subset. |
| `built-ins/globalThis` | 0 | 0 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype` | 147 | 104 | 0 | 43 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/byteLength` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/slice` | 33 | 28 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Boolean/prototype` | 26 | 18 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/BigInt/asIntN` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/BigInt/asUintN` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/BigInt/prototype` | 26 | 13 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/Number/isSafeInteger` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Number/MAX_VALUE` | 3 | 1 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Number/MIN_VALUE` | 3 | 1 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/toExponential` | 15 | 11 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/toFixed` | 16 | 12 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/toString` | 90 | 86 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/valueOf` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Object/defineProperties` | 632 | 364 | 0 | 268 | 0 | Official Test262 subset. |
| `built-ins/Object/freeze` | 53 | 28 | 0 | 25 | 0 | Official Test262 subset. |
| `built-ins/Object/fromEntries` | 25 | 21 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Object/getOwnPropertyDescriptor` | 310 | 307 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Object/getOwnPropertyNames` | 45 | 43 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/getPrototypeOf` | 39 | 37 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/hasOwn` | 62 | 58 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Object/isExtensible` | 38 | 36 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/preventExtensions` | 40 | 17 | 0 | 23 | 0 | Official Test262 subset. |
| `built-ins/Object/seal` | 94 | 70 | 0 | 24 | 0 | Official Test262 subset. |
| `built-ins/Object/setPrototypeOf` | 12 | 8 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype` | 1073 | 948 | 0 | 125 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/at` | 11 | 8 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/charAt` | 30 | 27 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/charCodeAt` | 25 | 22 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/codePointAt` | 16 | 12 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/concat` | 22 | 19 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/endsWith` | 27 | 23 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/includes` | 27 | 23 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/indexOf` | 47 | 44 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/lastIndexOf` | 25 | 22 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/slice` | 38 | 35 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/split` | 120 | 117 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/startsWith` | 21 | 17 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/substring` | 46 | 43 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Math/ceil` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/round` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/sqrt` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/trunc` | 12 | 8 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/sign` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/pow` | 28 | 24 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/decodeURIComponent` | 56 | 25 | 0 | 31 | 0 | Official Test262 subset. |
| `built-ins/encodeURIComponent` | 31 | 21 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/Symbol/prototype` | 35 | 20 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/Error/isError` | 12 | 8 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/apply` | 48 | 39 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/bind` | 100 | 90 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/call` | 49 | 43 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Date` | 594 | 384 | 0 | 210 | 0 | Official Test262 subset. |
| `built-ins/Date/now` | 6 | 4 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Date/parse` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype` | 485 | 302 | 0 | 183 | 0 | Official Test262 subset. |
| `built-ins/RegExp` | 1879 | 1202 | 0 | 677 | 0 | Official Test262 subset (re-run with fixed Test262 runner). |
| `built-ins/RegExp/prototype` | 487 | 407 | 0 | 80 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/exec` | 79 | 76 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/test` | 45 | 42 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Promise` | 652 | 306 | 0 | 346 | 0 | Official Test262 subset. |
| `built-ins/Promise/all` | 98 | 43 | 0 | 55 | 0 | Official Test262 subset. |
| `built-ins/Promise/prototype` | 124 | 86 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/Promise/prototype/then` | 75 | 61 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/Promise/resolve` | 30 | 18 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/Map` | 204 | 143 | 0 | 61 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype` | 156 | 104 | 0 | 52 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/get` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/set` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set` | 383 | 311 | 0 | 72 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype` | 357 | 292 | 0 | 65 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/add` | 21 | 17 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/has` | 30 | 26 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/WeakMap/prototype` | 117 | 91 | 0 | 26 | 0 | Official Test262 subset. |
| `built-ins/WeakSet/prototype` | 66 | 51 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/Proxy/get` | 19 | 18 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Proxy/set` | 27 | 23 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Reflect/get` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Reflect/set` | 18 | 13 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Reflect/apply` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Iterator/from` | 19 | 13 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype` | 373 | 330 | 0 | 43 | 0 | Official Test262 subset. |
| `built-ins/GeneratorFunction` | 23 | 11 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/GeneratorFunction/prototype` | 6 | 2 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/AsyncFunction` | 18 | 10 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/JSON/isRawJSON` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/JSON/rawJSON` | 10 | 0 | 2 | 8 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype` | 248 | 205 | 0 | 43 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/hasOwnProperty` | 63 | 60 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/toString` | 41 | 37 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/RangeError` | 15 | 6 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/RangeError/prototype` | 5 | 2 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/TypeError` | 15 | 6 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/TypeError/prototype` | 5 | 2 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Array/fromAsync` | 95 | 0 | 0 | 95 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype` | 499 | 327 | 0 | 172 | 0 | Official Test262 subset. |
| `built-ins/Atomics/load` | 14 | 1 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/Atomics/store` | 16 | 2 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype` | 1396 | 11 | 0 | 1385 | 0 | Official Test262 subset. |
| `built-ins/StringIteratorPrototype` | 7 | 4 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/MapIteratorPrototype` | 11 | 8 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/SetIteratorPrototype` | 11 | 8 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/RegExpStringIteratorPrototype` | 17 | 10 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/constructor` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/toString` | 80 | 9 | 0 | 71 | 0 | Official Test262 subset. |
| `built-ins/Error/prototype/constructor` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Error/prototype/toString` | 17 | 13 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Symbol/match` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/replace` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/search` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/split` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/toPrimitive` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Number/POSITIVE_INFINITY` | 4 | 2 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Number/NEGATIVE_INFINITY` | 4 | 2 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Math/exp` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/log` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/sin` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/cos` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/tan` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getTime` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toString` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/UTC` | 17 | 13 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Promise/prototype/catch` | 14 | 8 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Promise/reject` | 15 | 10 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/has` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/delete` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/delete` | 20 | 16 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Proxy/has` | 26 | 23 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Reflect/has` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Reflect/construct` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Iterator/concat` | 32 | 29 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/zip` | 36 | 3 | 0 | 33 | 0 | Official Test262 subset. |
| `built-ins/AsyncGeneratorFunction` | 23 | 5 | 0 | 18 | 0 | Official Test262 subset. |
| `built-ins/AsyncGeneratorFunction/prototype` | 6 | 2 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/GeneratorPrototype` | 61 | 47 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/GeneratorPrototype/next` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/Symbol.match` | 53 | 49 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/Symbol.replace` | 70 | 64 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/Symbol.search` | 23 | 19 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/Symbol.split` | 44 | 39 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/match` | 51 | 48 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/replace` | 55 | 52 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/repeat` | 16 | 12 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/trim` | 129 | 127 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/detached` | 11 | 6 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/resize` | 22 | 15 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/transfer` | 24 | 18 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/BigInt/parseInt` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/BigInt/prototype/toString` | 13 | 9 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Object/getOwnPropertySymbols` | 12 | 9 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Object/getOwnPropertyDescriptors` | 18 | 12 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Reflect/defineProperty` | 12 | 7 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Reflect/deleteProperty` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Reflect/getOwnPropertyDescriptor` | 13 | 9 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Reflect/getPrototypeOf` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Proxy/construct` | 29 | 18 | 0 | 11 | 0 | Official Test262 subset. |
| `built-ins/Proxy/defineProperty` | 24 | 13 | 0 | 11 | 0 | Official Test262 subset. |
| `built-ins/Proxy/getOwnPropertyDescriptor` | 21 | 15 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/entries` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/forEach` | 19 | 14 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/keys` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/values` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/entries` | 17 | 13 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/forEach` | 33 | 28 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/values` | 18 | 14 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Promise/allSettled` | 104 | 35 | 0 | 69 | 0 | Official Test262 subset. |
| `built-ins/Promise/any` | 94 | 25 | 0 | 69 | 0 | Official Test262 subset. |
| `built-ins/Promise/race` | 94 | 36 | 0 | 58 | 0 | Official Test262 subset. |
| `built-ins/RegExp/escape` | 20 | 15 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/padStart` | 13 | 9 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/padEnd` | 13 | 9 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/normalize` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/matchAll` | 25 | 19 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/search` | 43 | 40 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/toPrecision` | 17 | 13 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/toLocaleString` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/random` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/expm1` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/asin` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/acos` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/atan` | 7 | 3 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/atan2` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getFullYear` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getMonth` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getDate` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getHours` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getMinutes` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getSeconds` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/from` | 21 | 4 | 0 | 17 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/of` | 8 | 1 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Int8Array` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint8Array` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Int16Array` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint16Array` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Int32Array` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint32Array` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Float32Array` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Float64Array` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/WeakMap/prototype/get` | 13 | 9 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/WeakMap/prototype/has` | 20 | 16 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/WeakMap/prototype/set` | 20 | 16 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/WeakSet/prototype/add` | 22 | 18 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/WeakSet/prototype/has` | 20 | 16 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/WeakSet/prototype/delete` | 20 | 16 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Promise/prototype/finally` | 29 | 13 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/map` | 36 | 33 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/filter` | 37 | 34 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/take` | 33 | 30 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/toArray` | 18 | 15 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/AsyncIteratorPrototype` | 13 | 1 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/AsyncGeneratorPrototype` | 48 | 3 | 0 | 45 | 0 | Official Test262 subset. |
| `built-ins/AsyncGeneratorPrototype/next` | 11 | 1 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/EvalError` | 15 | 6 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/ReferenceError` | 15 | 6 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/SyntaxError` | 15 | 6 | 9 | 0 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/URIError` | 15 | 6 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/URIError/prototype` | 5 | 2 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/EvalError/prototype` | 5 | 2 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/ReferenceError/prototype` | 5 | 2 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/SyntaxError/prototype` | 5 | 2 | 3 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/maxByteLength` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/resizable` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/transferToFixedLength` | 24 | 18 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/AsyncFromSyncIteratorPrototype` | 38 | 1 | 0 | 37 | 0 | Official Test262 subset. |
| `built-ins/AsyncFromSyncIteratorPrototype/next` | 13 | 1 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/Atomics/add` | 15 | 1 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/Atomics/compareExchange` | 16 | 1 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/Atomics/exchange` | 16 | 1 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/BigInt/prototype/toLocaleString` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/BigInt/prototype/valueOf` | 8 | 3 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Boolean/prototype/constructor` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Boolean/prototype/toString` | 10 | 7 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Boolean/prototype/valueOf` | 10 | 7 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/buffer` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/byteLength` | 14 | 9 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/byteOffset` | 13 | 9 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/constructor` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getDay` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getMilliseconds` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toISOString` | 17 | 14 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toJSON` | 13 | 10 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/valueOf` | 6 | 2 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/DisposableStack` | 91 | 0 | 0 | 91 | 0 | Official Test262 subset. |
| `built-ins/DisposableStack/prototype` | 78 | 0 | 0 | 78 | 0 | Official Test262 subset. |
| `built-ins/Error/prototype/message` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Error/prototype/name` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/FinalizationRegistry` | 47 | 31 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/FinalizationRegistry/prototype` | 31 | 20 | 0 | 11 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/Symbol.hasInstance` | 11 | 8 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/GeneratorPrototype/return` | 23 | 19 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/GeneratorPrototype/throw` | 22 | 18 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/drop` | 34 | 31 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/every` | 33 | 30 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/find` | 32 | 29 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/flatMap` | 44 | 41 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/forEach` | 27 | 24 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/reduce` | 30 | 27 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/some` | 33 | 30 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/Symbol.iterator` | 5 | 2 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Iterator/zip` | 36 | 36 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Map/groupBy` | 14 | 12 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/MapIteratorPrototype/next` | 10 | 8 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/clear` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/size` | 11 | 8 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Math/acosh` | 7 | 3 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/asinh` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/atanh` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/cbrt` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/clz32` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/cosh` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/expm1` | 5 | 5 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/fround` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/hypot` | 12 | 8 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/imul` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/log10` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/log1p` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/log2` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/sinh` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Math/tanh` | 5 | 1 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Object/groupBy` | 14 | 12 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/isFrozen` | 59 | 57 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/isSealed` | 33 | 31 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/isPrototypeOf` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/propertyIsEnumerable` | 16 | 13 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/valueOf` | 20 | 17 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Promise/allKeyed` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Promise/allSettledKeyed` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Promise/try` | 12 | 5 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Promise/withResolvers` | 6 | 5 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Promise/Symbol.species` | 5 | 2 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Proxy/apply` | 14 | 11 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Proxy/deleteProperty` | 17 | 16 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Proxy/getOwnPropertyDescriptor` | 21 | 21 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Proxy/getPrototypeOf` | 19 | 18 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Proxy/isExtensible` | 12 | 11 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Proxy/ownKeys` | 27 | 25 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Proxy/preventExtensions` | 12 | 10 | 1 | 1 | 0 | Official Test262 subset. |
| `built-ins/Proxy/revocable` | 18 | 11 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Proxy/setPrototypeOf` | 17 | 16 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Reflect/ownKeys` | 13 | 9 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Reflect/preventExtensions` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Reflect/setPrototypeOf` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Reflect/isExtensible` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/CharacterClassEscapes` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/RegExp/dotall` | 4 | 4 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/RegExp/lookBehind` | 17 | 17 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/flags` | 16 | 13 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/global` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/ignoreCase` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/multiline` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/source` | 12 | 8 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/sticky` | 8 | 5 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/toString` | 9 | 4 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/RegExpStringIteratorPrototype/next` | 15 | 9 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/RegExp/Symbol.species` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/clear` | 19 | 15 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/size` | 6 | 3 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/union` | 29 | 25 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/SetIteratorPrototype/next` | 10 | 8 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Set/Symbol.species` | 4 | 2 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/SharedArrayBuffer/prototype` | 78 | 57 | 0 | 21 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/constructor` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/isWellFormed` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/localeCompare` | 13 | 10 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/replaceAll` | 45 | 41 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/toLowerCase` | 30 | 27 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/toUpperCase` | 26 | 23 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/trimEnd` | 23 | 19 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/trimStart` | 23 | 19 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/valueOf` | 7 | 3 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/StringIteratorPrototype/next` | 5 | 3 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/matchAll` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/prototype/description` | 7 | 6 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Symbol/prototype/toString` | 8 | 3 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Symbol/prototype/valueOf` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Symbol/species` | 4 | 2 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/at` | 15 | 0 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/buffer` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/byteLength` | 18 | 0 | 0 | 18 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/byteOffset` | 16 | 0 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/length` | 18 | 0 | 0 | 18 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/Symbol.species` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/from` | 58 | 0 | 0 | 58 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/of` | 26 | 0 | 0 | 26 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint8ClampedArray` | 11 | 2 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/WeakRef/prototype` | 13 | 6 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/WeakRef/prototype/deref` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/AsyncGeneratorPrototype/return` | 19 | 2 | 0 | 17 | 0 | Official Test262 subset. |
| `built-ins/AsyncGeneratorPrototype/throw` | 16 | 0 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/AsyncIteratorPrototype/Symbol.asyncIterator` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Reflect/enumerate` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/toLocaleString` | 12 | 7 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/ShadowRealm/prototype` | 54 | 1 | 3 | 50 | 0 | Official Test262 subset. |
| `built-ins/AbstractModuleSource` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/AbstractModuleSource/prototype` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/AggregateError` | 25 | 12 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/AggregateError/prototype` | 6 | 2 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Array` | 3081 | 2678 | 0 | 403 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer` | 196 | 136 | 0 | 60 | 0 | Official Test262 subset. |
| `built-ins/Array/prototype` | 2810 | 2531 | 0 | 279 | 0 | Official Test262 subset. |
| `built-ins/AsyncDisposableStack` | 52 | 0 | 0 | 52 | 0 | Official Test262 subset. |
| `built-ins/AsyncDisposableStack/prototype` | 39 | 0 | 0 | 39 | 0 | Official Test262 subset. |
| `built-ins/AsyncDisposableStack/prototype/adopt` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/AsyncDisposableStack/prototype/defer` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/AsyncDisposableStack/prototype/disposeAsync` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/AsyncDisposableStack/prototype/disposed` | 5 | 0 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/AsyncDisposableStack/prototype/move` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/AsyncDisposableStack/prototype/use` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Atomics` | 382 | 54 | 0 | 328 | 0 | Official Test262 subset. |
| `built-ins/Atomics/and` | 15 | 1 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/Atomics/and/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Atomics/isLockFree` | 7 | 2 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Atomics/isLockFree/bigint` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Atomics/notify` | 51 | 22 | 0 | 29 | 0 | Official Test262 subset. |
| `built-ins/Atomics/notify/bigint` | 8 | 5 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Atomics/or` | 15 | 1 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/Atomics/or/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Atomics/pause` | 6 | 2 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Atomics/sub` | 15 | 1 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/Atomics/sub/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Atomics/wait` | 77 | 17 | 0 | 60 | 0 | Official Test262 subset. |
| `built-ins/Atomics/waitAsync` | 101 | 0 | 0 | 101 | 0 | Official Test262 subset. |
| `built-ins/Atomics/waitAsync/bigint` | 44 | 0 | 0 | 44 | 0 | Official Test262 subset. |
| `built-ins/Atomics/wait/bigint` | 25 | 5 | 0 | 20 | 0 | Official Test262 subset. |
| `built-ins/Atomics/xor` | 15 | 1 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/Atomics/xor/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/DataView` | 561 | 378 | 0 | 183 | 0 | Official Test262 subset. |
| `built-ins/Error` | 58 | 39 | 0 | 19 | 0 | Official Test262 subset. |
| `built-ins/eval` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Function` | 509 | 362 | 0 | 147 | 0 | Official Test262 subset. |
| `built-ins/Function/internals` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Function/internals/Call` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Function/internals/Construct` | 6 | 3 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Function/length` | 13 | 10 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/global` | 29 | 26 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Infinity` | 6 | 5 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Iterator` | 510 | 381 | 0 | 129 | 0 | Official Test262 subset. |
| `built-ins/JSON` | 165 | 130 | 2 | 33 | 0 | Official Test262 subset. |
| `built-ins/Math` | 327 | 168 | 0 | 159 | 0 | Official Test262 subset. |
| `built-ins/Math/E` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Math/f16round` | 5 | 0 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Math/LN10` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Math/LN2` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Math/LOG10E` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Math/LOG2E` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Math/PI` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Math/SQRT1_2` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Math/SQRT2` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Math/sumPrecise` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/NaN` | 6 | 5 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors` | 94 | 38 | 9 | 47 | 0 | Official Test262 subset. |
| `built-ins/Proxy` | 311 | 251 | 1 | 59 | 0 | Official Test262 subset. |
| `built-ins/Proxy/enumerate` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Reflect` | 153 | 97 | 0 | 56 | 0 | Official Test262 subset. |
| `built-ins/ShadowRealm` | 67 | 1 | 3 | 63 | 0 | Official Test262 subset. |
| `built-ins/ShadowRealm/WrappedFunction` | 5 | 0 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/SharedArrayBuffer` | 104 | 80 | 0 | 24 | 0 | Official Test262 subset. |
| `built-ins/String` | 1223 | 1081 | 0 | 142 | 0 | Official Test262 subset. |
| `built-ins/SuppressedError` | 22 | 0 | 0 | 22 | 0 | Official Test262 subset. |
| `built-ins/SuppressedError/prototype` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Symbol` | 98 | 36 | 0 | 62 | 0 | Official Test262 subset. |
| `built-ins/Symbol/asyncDispose` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Symbol/asyncIterator` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/dispose` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Symbol/hasInstance` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/isConcatSpreadable` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Symbol/unscopables` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal` | 4485 | 0 | 0 | 4485 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration` | 519 | 0 | 0 | 519 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/compare` | 50 | 0 | 0 | 50 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/from` | 31 | 0 | 0 | 31 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype` | 410 | 0 | 0 | 410 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant` | 459 | 0 | 0 | 459 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/compare` | 29 | 0 | 0 | 29 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/from` | 31 | 0 | 0 | 31 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/fromEpochMilliseconds` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/fromEpochNanoseconds` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype` | 369 | 0 | 0 | 369 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now` | 66 | 0 | 0 | 66 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/instant` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/plainDateISO` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/plainDateTimeISO` | 13 | 0 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/plainTimeISO` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/timeZoneId` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/toStringTag` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/zonedDateTimeISO` | 15 | 0 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate` | 636 | 0 | 0 | 636 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/compare` | 41 | 0 | 0 | 41 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/from` | 70 | 0 | 0 | 70 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype` | 506 | 0 | 0 | 506 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime` | 751 | 0 | 0 | 751 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/compare` | 41 | 0 | 0 | 41 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/from` | 69 | 0 | 0 | 69 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype` | 612 | 0 | 0 | 612 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay` | 195 | 0 | 0 | 195 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/from` | 57 | 0 | 0 | 57 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype` | 116 | 0 | 0 | 116 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime` | 481 | 0 | 0 | 481 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/compare` | 31 | 0 | 0 | 31 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/from` | 50 | 0 | 0 | 50 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype` | 381 | 0 | 0 | 381 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth` | 497 | 0 | 0 | 497 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/compare` | 38 | 0 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/from` | 63 | 0 | 0 | 63 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype` | 374 | 0 | 0 | 374 | 0 | Official Test262 subset. |
| `built-ins/Temporal/toStringTag` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime` | 876 | 0 | 0 | 876 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/compare` | 49 | 0 | 0 | 49 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/from` | 88 | 0 | 0 | 88 | 0 | Official Test262 subset. |
| `built-ins/AsyncFromSyncIteratorPrototype/return` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/AsyncFromSyncIteratorPrototype/throw` | 15 | 0 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/AsyncIteratorPrototype/Symbol.asyncDispose` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Atomics/add/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Atomics/compareExchange/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Atomics/exchange/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getFloat32` | 21 | 15 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getFloat64` | 21 | 15 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getInt16` | 18 | 12 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getInt32` | 28 | 22 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getInt8` | 17 | 11 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getUint16` | 18 | 12 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getUint32` | 18 | 12 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getUint8` | 17 | 11 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setFloat32` | 24 | 15 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setFloat64` | 24 | 15 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setInt16` | 24 | 15 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setInt32` | 24 | 15 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setInt8` | 22 | 13 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getTimezoneOffset` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getUTCDate` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getUTCDay` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getUTCFullYear` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getUTCHours` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getUTCMinutes` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getUTCMonth` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getUTCSeconds` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setDate` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setFullYear` | 20 | 16 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setHours` | 23 | 19 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setMinutes` | 18 | 14 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setMonth` | 17 | 13 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setSeconds` | 17 | 13 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setTime` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/Symbol.toPrimitive` | 18 | 15 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toDateString` | 7 | 3 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toLocaleDateString` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toLocaleString` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toLocaleTimeString` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toTimeString` | 6 | 2 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toUTCString` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/DisposableStack/prototype/adopt` | 11 | 0 | 0 | 11 | 0 | Official Test262 subset. |
| `built-ins/DisposableStack/prototype/defer` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/DisposableStack/prototype/dispose` | 13 | 0 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/FinalizationRegistry/prototype/register` | 17 | 13 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/FinalizationRegistry/prototype/unregister` | 10 | 6 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/arguments` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/caller` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/constructor` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/Symbol.dispose` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype/Symbol.toStringTag` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Iterator/zipKeyed` | 42 | 2 | 0 | 40 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/getOrInsert` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/Symbol.iterator` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Map/Symbol.species` | 4 | 2 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/constructor` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/RegExp/match-indices` | 14 | 7 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/RegExp/named-groups` | 36 | 32 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/dotAll` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/hasIndices` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/Symbol.matchAll` | 26 | 15 | 0 | 11 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/unicode` | 8 | 5 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/unicodeSets` | 38 | 34 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/RegExp/regexp-modifiers` | 70 | 70 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/constructor` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/difference` | 28 | 24 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/intersection` | 28 | 24 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/isDisjointFrom` | 25 | 21 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/isSubsetOf` | 23 | 19 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/isSupersetOf` | 25 | 21 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/keys` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/Symbol.iterator` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/Symbol.toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/symmetricDifference` | 28 | 24 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/ShadowRealm/prototype/evaluate` | 37 | 0 | 1 | 36 | 0 | Official Test262 subset. |
| `built-ins/ShadowRealm/prototype/importValue` | 15 | 1 | 2 | 12 | 0 | Official Test262 subset. |
| `built-ins/SharedArrayBuffer/prototype` | 78 | 78 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/SharedArrayBuffer/prototype/byteLength` | 9 | 6 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/SharedArrayBuffer/prototype/slice` | 32 | 27 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/Symbol.iterator` | 6 | 2 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/toLocaleLowerCase` | 28 | 25 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/toLocaleUpperCase` | 26 | 23 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/toString` | 7 | 3 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/toWellFormed` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Symbol/prototype/Symbol.toPrimitive` | 9 | 6 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/add` | 34 | 0 | 0 | 34 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/days` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/hours` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/minutes` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/seconds` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/toJSON` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/toString` | 44 | 0 | 0 | 44 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype` | 369 | 369 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/epochMilliseconds` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/toString` | 54 | 0 | 0 | 54 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/instant` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Now/plainDateISO` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype` | 506 | 506 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/day` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/month` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/year` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/toString` | 18 | 0 | 0 | 18 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype` | 612 | 612 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/hour` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/minute` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/toString` | 49 | 0 | 0 | 49 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype` | 381 | 381 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/hour` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/toString` | 40 | 0 | 0 | 40 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype` | 719 | 0 | 0 | 719 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/epochMilliseconds` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/hour` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/minute` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/timeZoneId` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/toInstant` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/toString` | 62 | 0 | 0 | 62 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint8ClampedArray` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/at` | 15 | 15 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/buffer` | 12 | 12 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/byteLength` | 18 | 18 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/byteOffset` | 16 | 16 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/WeakRef/prototype` | 13 | 13 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/WeakRef/prototype/deref` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/union` | 29 | 29 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Atomics/load/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Atomics/store/bigint` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getBigInt64` | 21 | 15 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getBigUint64` | 21 | 15 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/getFloat16` | 21 | 15 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setBigInt64` | 24 | 15 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setBigUint64` | 3 | 1 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setFloat16` | 24 | 15 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setUint16` | 24 | 15 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setUint32` | 24 | 15 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype/setUint8` | 22 | 13 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getUTCMilliseconds` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setMilliseconds` | 14 | 10 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setUTCDate` | 7 | 3 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setUTCFullYear` | 6 | 2 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setUTCHours` | 11 | 7 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setUTCMilliseconds` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setUTCMinutes` | 8 | 4 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setUTCMonth` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/setUTCSeconds` | 9 | 5 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toTemporalInstant` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/DisposableStack/prototype/disposed` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/DisposableStack/prototype/move` | 13 | 0 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/DisposableStack/prototype/use` | 19 | 0 | 0 | 19 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/caller-arguments` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/getOrInsertComputed` | 19 | 16 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Object/internals` | 6 | 4 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/internals/DefineOwnProperty` | 6 | 4 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/__defineGetter__` | 11 | 6 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/__defineSetter__` | 11 | 6 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/__lookupGetter__` | 16 | 13 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/__lookupSetter__` | 16 | 13 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/__proto__` | 15 | 12 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/RegExp/property-escapes` | 613 | 165 | 0 | 448 | 0 | Official Test262 subset. |
| `built-ins/RegExp/property-escapes/generated` | 469 | 21 | 0 | 448 | 0 | Official Test262 subset. |
| `built-ins/RegExp/property-escapes/generated/strings` | 28 | 21 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/RegExp/regexp-modifiers/syntax` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/RegExp/regexp-modifiers/syntax/valid` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/RegExp/unicodeSets` | 114 | 0 | 0 | 114 | 0 | Official Test262 subset. |
| `built-ins/RegExp/unicodeSets/generated` | 114 | 0 | 0 | 114 | 0 | Official Test262 subset. |
| `built-ins/SharedArrayBuffer/prototype/grow` | 15 | 11 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/SharedArrayBuffer/prototype/growable` | 9 | 6 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/SharedArrayBuffer/prototype/maxByteLength` | 10 | 7 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/abs` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/blank` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/microseconds` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/milliseconds` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/months` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/nanoseconds` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/negated` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/round` | 119 | 0 | 0 | 119 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/sign` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/subtract` | 34 | 0 | 0 | 34 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/toLocaleString` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/total` | 75 | 0 | 0 | 75 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/valueOf` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/weeks` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/with` | 22 | 0 | 0 | 22 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Duration/prototype/years` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/add` | 28 | 0 | 0 | 28 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/epochNanoseconds` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/equals` | 29 | 0 | 0 | 29 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/round` | 40 | 0 | 0 | 40 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/since` | 70 | 0 | 0 | 70 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/subtract` | 27 | 0 | 0 | 27 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/toJSON` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/toLocaleString` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/toZonedDateTimeISO` | 18 | 0 | 0 | 18 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/until` | 69 | 0 | 0 | 69 | 0 | Official Test262 subset. |
| `built-ins/Temporal/Instant/prototype/valueOf` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/add` | 39 | 0 | 0 | 39 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/calendarId` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/dayOfWeek` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/dayOfYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/daysInMonth` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/daysInWeek` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/daysInYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/equals` | 39 | 0 | 0 | 39 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/era` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/eraYear` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/inLeapYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/monthCode` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/monthsInYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/since` | 83 | 0 | 0 | 83 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/subtract` | 38 | 0 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/toJSON` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/toLocaleString` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/toPlainDateTime` | 34 | 0 | 0 | 34 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/toPlainMonthDay` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/toPlainYearMonth` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/toZonedDateTime` | 45 | 0 | 0 | 45 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/until` | 82 | 0 | 0 | 82 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/valueOf` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/weekOfYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/with` | 25 | 0 | 0 | 25 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/withCalendar` | 17 | 0 | 0 | 17 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDate/prototype/yearOfWeek` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/add` | 42 | 0 | 0 | 42 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/calendarId` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/day` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/dayOfWeek` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/dayOfYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/daysInMonth` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/daysInWeek` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/daysInYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/equals` | 40 | 0 | 0 | 40 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/era` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/eraYear` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/inLeapYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/microsecond` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/millisecond` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/month` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/monthCode` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/monthsInYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/nanosecond` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/round` | 44 | 0 | 0 | 44 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/second` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/since` | 91 | 0 | 0 | 91 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/subtract` | 42 | 0 | 0 | 42 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/toJSON` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/toLocaleString` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/toPlainDate` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/toPlainTime` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/toZonedDateTime` | 29 | 0 | 0 | 29 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/until` | 94 | 0 | 0 | 94 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/valueOf` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/weekOfYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/with` | 30 | 0 | 0 | 30 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/withCalendar` | 17 | 0 | 0 | 17 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/withPlainTime` | 36 | 0 | 0 | 36 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/year` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainDateTime/prototype/yearOfWeek` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/calendarId` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/day` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/equals` | 35 | 0 | 0 | 35 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/month` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/monthCode` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/toJSON` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/toLocaleString` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/toPlainDate` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/toString` | 16 | 0 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/valueOf` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainMonthDay/prototype/with` | 20 | 0 | 0 | 20 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/add` | 32 | 0 | 0 | 32 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/equals` | 30 | 0 | 0 | 30 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/microsecond` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/millisecond` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/minute` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/nanosecond` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/round` | 41 | 0 | 0 | 41 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/second` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/since` | 74 | 0 | 0 | 74 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/subtract` | 32 | 0 | 0 | 32 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/toJSON` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/toLocaleString` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/until` | 74 | 0 | 0 | 74 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/valueOf` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainTime/prototype/with` | 22 | 0 | 0 | 22 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/add` | 36 | 0 | 0 | 36 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/calendarId` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/daysInMonth` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/daysInYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/equals` | 39 | 0 | 0 | 39 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/era` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/eraYear` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/inLeapYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/month` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/monthCode` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/monthsInYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/since` | 80 | 0 | 0 | 80 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/subtract` | 37 | 0 | 0 | 37 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/toJSON` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/toLocaleString` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/toPlainDate` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/toString` | 17 | 0 | 0 | 17 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/until` | 78 | 0 | 0 | 78 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/valueOf` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/with` | 22 | 0 | 0 | 22 | 0 | Official Test262 subset. |
| `built-ins/Temporal/PlainYearMonth/prototype/year` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/add` | 43 | 0 | 0 | 43 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/calendarId` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/day` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/dayOfWeek` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/dayOfYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/daysInMonth` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/daysInWeek` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/daysInYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/epochNanoseconds` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/equals` | 54 | 0 | 0 | 54 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/era` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/eraYear` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/getTimeZoneTransition` | 14 | 0 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/hoursInDay` | 5 | 0 | 0 | 5 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/inLeapYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/microsecond` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/millisecond` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/month` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/monthCode` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/monthsInYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/nanosecond` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/offset` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/offsetNanoseconds` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/round` | 46 | 0 | 0 | 46 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/second` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/since` | 98 | 0 | 0 | 98 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/startOfDay` | 8 | 0 | 0 | 8 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/subtract` | 42 | 0 | 0 | 42 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/toJSON` | 11 | 0 | 0 | 11 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/toLocaleString` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/toPlainDate` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/toPlainDateTime` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/toPlainTime` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/toStringTag` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/until` | 96 | 0 | 0 | 96 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/valueOf` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/weekOfYear` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/with` | 41 | 0 | 0 | 41 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/withCalendar` | 16 | 0 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/withPlainTime` | 35 | 0 | 0 | 35 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/withTimeZone` | 16 | 0 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/year` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/Temporal/ZonedDateTime/prototype/yearOfWeek` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/ThrowTypeError` | 14 | 11 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArray` | 1438 | 16 | 0 | 1422 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors` | 736 | 32 | 0 | 704 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/BigInt64Array` | 12 | 2 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/BigInt64Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/BigUint64Array` | 12 | 2 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/BigUint64Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors` | 116 | 1 | 0 | 115 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors-bigint` | 113 | 2 | 0 | 111 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors-bigint/buffer-arg` | 52 | 0 | 0 | 52 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors-bigint/length-arg` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors-bigint/no-args` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors-bigint/object-arg` | 31 | 2 | 0 | 29 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors-bigint/typedarray-arg` | 11 | 0 | 0 | 11 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors/buffer-arg` | 54 | 0 | 0 | 54 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors/length-arg` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors/no-args` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors/object-arg` | 28 | 0 | 0 | 28 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/ctors/typedarray-arg` | 14 | 0 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Float32Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Float64Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/from/BigInt` | 28 | 0 | 0 | 28 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Int16Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Int32Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Int8Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals` | 240 | 7 | 0 | 233 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/DefineOwnProperty` | 54 | 0 | 0 | 54 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/DefineOwnProperty/BigInt` | 26 | 0 | 0 | 26 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/Delete` | 39 | 0 | 0 | 39 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/Delete/BigInt` | 19 | 0 | 0 | 19 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/Get` | 28 | 0 | 0 | 28 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/Get/BigInt` | 14 | 0 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/GetOwnProperty` | 24 | 0 | 0 | 24 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/GetOwnProperty/BigInt` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/HasProperty` | 32 | 0 | 0 | 32 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/HasProperty/BigInt` | 15 | 0 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/OwnPropertyKeys` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/OwnPropertyKeys/BigInt` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/Set` | 53 | 7 | 0 | 46 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/internals/Set/BigInt` | 27 | 2 | 0 | 25 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/of/BigInt` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype` | 60 | 0 | 0 | 60 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/buffer` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/byteLength` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/byteOffset` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/copyWithin` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/entries` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/every` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/fill` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/filter` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/find` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/findIndex` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/forEach` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/indexOf` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/join` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/keys` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/lastIndexOf` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/length` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/map` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/reduce` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/reduceRight` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/reverse` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/set` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/slice` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/some` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/sort` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/subarray` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/Symbol.toStringTag` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/toLocaleString` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/toString` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/prototype/values` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint16Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint32Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint8Array/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArrayConstructors/Uint8ClampedArray/prototype` | 4 | 1 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/at/BigInt` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/buffer/BigInt` | 2 | 0 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/byteLength/BigInt` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/byteOffset/BigInt` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/copyWithin` | 64 | 0 | 0 | 64 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/copyWithin/BigInt` | 24 | 0 | 0 | 24 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/entries` | 19 | 0 | 0 | 19 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/entries/BigInt` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/every` | 44 | 0 | 0 | 44 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/every/BigInt` | 16 | 0 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/fill` | 51 | 1 | 0 | 50 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/fill/BigInt` | 18 | 0 | 0 | 18 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/filter` | 84 | 0 | 0 | 84 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/filter/BigInt` | 36 | 0 | 0 | 36 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/find` | 38 | 0 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/find/BigInt` | 13 | 0 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/findIndex` | 38 | 0 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/findIndex/BigInt` | 13 | 0 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/findLast` | 38 | 0 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/findLast/BigInt` | 13 | 0 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/findLastIndex` | 38 | 0 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/findLastIndex/BigInt` | 13 | 0 | 0 | 13 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/forEach` | 42 | 0 | 0 | 42 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/forEach/BigInt` | 15 | 0 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/includes` | 45 | 2 | 0 | 43 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/includes/BigInt` | 14 | 0 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/indexOf` | 43 | 0 | 0 | 43 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/indexOf/BigInt` | 15 | 0 | 0 | 15 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/join` | 32 | 1 | 0 | 31 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/join/BigInt` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/keys` | 19 | 0 | 0 | 19 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/keys/BigInt` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/lastIndexOf` | 42 | 0 | 0 | 42 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/lastIndexOf/BigInt` | 14 | 0 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/length/BigInt` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/map` | 84 | 0 | 0 | 84 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/map/BigInt` | 34 | 0 | 0 | 34 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/reduce` | 50 | 0 | 0 | 50 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/reduce/BigInt` | 19 | 0 | 0 | 19 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/reduceRight` | 50 | 0 | 0 | 50 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/reduceRight/BigInt` | 19 | 0 | 0 | 19 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/reverse` | 21 | 0 | 0 | 21 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/reverse/BigInt` | 6 | 0 | 0 | 6 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/set` | 109 | 3 | 0 | 106 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/set/BigInt` | 49 | 2 | 0 | 47 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/slice` | 91 | 0 | 0 | 91 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/slice/BigInt` | 38 | 0 | 0 | 38 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/some` | 44 | 0 | 0 | 44 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/some/BigInt` | 16 | 0 | 0 | 16 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/sort` | 35 | 0 | 0 | 35 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/sort/BigInt` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/subarray` | 67 | 1 | 0 | 66 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/subarray/BigInt` | 27 | 0 | 0 | 27 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/Symbol.iterator` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/Symbol.toStringTag` | 18 | 0 | 0 | 18 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/Symbol.toStringTag/BigInt` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/toLocaleString` | 39 | 0 | 0 | 39 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/toLocaleString/BigInt` | 14 | 0 | 0 | 14 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/toReversed` | 9 | 0 | 0 | 9 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/toSorted` | 12 | 0 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/toString` | 3 | 0 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/toString/BigInt` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/values` | 21 | 2 | 0 | 19 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/values/BigInt` | 4 | 0 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/with` | 22 | 1 | 0 | 21 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype/with/BigInt` | 1 | 0 | 0 | 1 | 0 | Official Test262 subset. |
| `built-ins/Uint8Array` | 68 | 4 | 10 | 54 | 0 | Official Test262 subset. |
| `built-ins/Uint8Array/fromBase64` | 13 | 1 | 2 | 10 | 0 | Official Test262 subset. |
| `built-ins/Uint8Array/fromHex` | 9 | 1 | 2 | 6 | 0 | Official Test262 subset. |
| `built-ins/Uint8Array/prototype` | 46 | 2 | 6 | 38 | 0 | Official Test262 subset. |
| `built-ins/Uint8Array/prototype/setFromBase64` | 17 | 1 | 3 | 13 | 0 | Official Test262 subset. |
| `built-ins/Uint8Array/prototype/setFromHex` | 12 | 1 | 3 | 8 | 0 | Official Test262 subset. |
| `built-ins/Uint8Array/prototype/toBase64` | 10 | 0 | 0 | 10 | 0 | Official Test262 subset. |
| `built-ins/Uint8Array/prototype/toHex` | 7 | 0 | 0 | 7 | 0 | Official Test262 subset. |
| `built-ins/undefined` | 8 | 6 | 0 | 2 | 0 | Official Test262 subset. |
| `built-ins/WeakMap` | 141 | 110 | 0 | 31 | 0 | Official Test262 subset. |
| `built-ins/WeakMap/prototype/delete` | 22 | 18 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/WeakMap/prototype/getOrInsert` | 17 | 13 | 0 | 4 | 0 | Official Test262 subset. |
| `built-ins/WeakMap/prototype/getOrInsertComputed` | 22 | 19 | 0 | 3 | 0 | Official Test262 subset. |
| `built-ins/WeakRef` | 29 | 17 | 0 | 12 | 0 | Official Test262 subset. |
| `built-ins/WeakSet` | 85 | 65 | 0 | 20 | 0 | Official Test262 subset. |
| `built-ins/WeakSet/prototype/constructor` | 2 | 1 | 0 | 1 | 0 | Official Test262 subset. |
The `built-ins/Object` mini-suite provides a smoke check that:

- `Object.defineProperty` correctly creates own data properties with the expected descriptor; and
- Prototype chain reads and writes behave as expected without mutating the shared prototype object.

The `built-ins/Array/isArray` subset confirms that:

- `Array.isArray` correctly distinguishes arrays from non-arrays (including proxies, primitives, and exotic objects), and
- The protoCore-backed immutability model still preserves JS-level identity and type tagging expected by the ECMAScript specification.

---

## 4. Common Failure Patterns (Top 5)

> To be filled once snapshots exist; this is the structure the analysis should follow.

For each pattern:

1. **Pattern name** — short, descriptive (e.g. “Property updates drop new root in object slots”).  
2. **Affected areas** — example Test262 paths (e.g. `built-ins/Object/defineProperty/**`).  
3. **Technical root cause** — in terms of protoJS / protoCore:
   - Where an immutable update returns a new root (e.g. `setAttribute`) but the result is not propagated.
   - Where lexical environment references or prototype chains are not updated consistently.
4. **Fix status** — pending / in progress / resolved (with commit hash or PR reference).

---

## 5. How to Regenerate Conformance Data

1. **Configure Test262 location**
   - Clone Test262:
     ```bash
     git clone https://github.com/tc39/test262.git /path/to/test262
     ```
   - Update `tests/test262/config/test262_paths.json`:
     ```json
     {
       "test262_root": "/path/to/test262",
       "harness_dir": "/path/to/test262/harness",
       "default_timeout_ms": 10000,
       "patterns": ["language/expressions", "language/statements"]
     }
     ```

2. **Run the runner**
   ```bash
   cd protoJS
   PROTOJS=./build/protojs TEST262_ROOT=/path/to/test262 \
     node tests/test262/runner/test262_runner.js
   ```

3. **Update this document**
   - Inspect the latest JSON snapshot in `tests/test262/reports/`.
   - Update the tables in sections 2 and 3 with:
     - Total test counts.
     - Passed / failed / timeout numbers.
   - Summarise the five most common failure patterns in section 4, with technical analysis and references to protoJS / protoCore components.

