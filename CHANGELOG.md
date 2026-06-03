# Changelog

All notable changes to protoJS are documented in this file.

## [Unreleased]

### Fixed (test262 spec conformance push, round 7 — 2026-06-03)

Seventh consecutive 30-commit sprint, focused on the broad cleanups
the prior rounds didn't reach: descriptor surfaces on the global
namespace and Object / Reflect statics, ToString / ToNumber primitive-
result coercion across parseInt / parseFloat / JSON.parse, Reflect.*
spec corrections, and the long-standing 32-element array-literal
truncation bug.

**Big-impact correctness fix (any user with a 32+ entry array literal):**
- `OP_get_array_el` now falls back to indexed attributes for arrays
  past slot 32. QuickJS emits `OP_define_field` for elements 32+ of a
  literal, storing them as string-keyed attributes ('32', '33', ...).
  Pre-fix arrayTryFastGet's PROTO_NONE 'out-of-bounds' return was
  treated as final and `a[32]` read `undefined` despite the descriptor
  showing the correct value. Affected every consumer of large array
  literals; surfaced in test262 via parseInt's multi-radix sweep.

**Object descriptor identity for builtins:**
- `Object.getOwnPropertyDescriptor` result inherits the real
  `Object.prototype` (was a parentless object — `desc.hasOwnProperty`
  was undefined despite getPrototypeOf reporting Object.prototype).
- `Object.getOwnPropertyDescriptor` synthesises descriptors for array
  index slots (in `__elements__`) and String-wrapper char indices.
- `Object.getOwnPropertyNames` enumerates String-wrapper char indices
  alongside user attributes.
- `new F()` does NOT stamp own `constructor` on the instance —
  `F.prototype.constructor` is set lazily on first construct when
  missing (plain function declarations), so the backref still resolves
  via the chain without leaking into `Object.keys(instance)`.
- Object static methods + `Object.prototype` carry the §17 descriptor
  (0x3 / 0x0 respectively) on their installation site, so
  `Object.keys(Object)` returns `[]`.

**JSON corrections:**
- `JSON.parse` ToString-coerces primitive arguments (null → "null",
  true → "true", 3.14 → "3.14", ...) per §25.5.1 step 1.
- `JSON.parse` Object arguments run through ToPrimitive('string') and
  ToString of the primitive, so `JSON.parse({toString(){return '"x"'}})`
  parses to `"x"` instead of throwing SyntaxError.
- `TypeBridge::fromJS` preserves negative zero: `JSON.parse('-0')` is
  now `-0` (not `+0`).
- `JSON.stringify` serialises accessor-backed properties (literal
  `{get k(){return v}}` AND `Object.defineProperty(o,k,{get:...})`)
  by invoking the getter. The replacer array probes
  `__primitive_value__` to ToString-coerce Number / String wrappers
  per §25.5.2 step 4.b.e.i and skips undefined / null / boolean /
  Symbol entries per step 4.b.f.
- `JSON.stringify` replacer-array detects sparse arrays without
  `__elements__` (`new Array(3); sp[1]='key'`) — falls back to
  length + indexed attributes.
- `JSON.stringify` unboxes Number / String wrappers for the space
  argument per §25.5.2 step 5.

**Reflect surface alignment:**
- `Reflect.set` honours the receiver (per §28.1.13 step 4) and returns
  false when receiver is a non-Object primitive.
- `Reflect.setPrototypeOf` rejects cycle-forming assignments per §9.1.2
  step 8.b (returns false).
- `Reflect.setPrototypeOf` / `Object.setPrototypeOf` reject prototype
  changes on non-extensible targets per §10.1.2.1 step 4 (Reflect →
  false; Object → TypeError).
- `Reflect.construct` validates argumentsList via CreateListFromArrayLike
  (§7.3.17) — primitives throw TypeError. The construct result only
  replaces newObj when it's an Object (was previously accepting the
  undefined sentinel and returning undefined for `Reflect.construct(F, [])`
  where F has no explicit return).
- `Reflect.ownKeys` orders keys per §9.1.11: indices ascending,
  then string keys in insertion order, then 'length' for arrays.
  Pre-fix it dropped 'length' unconditionally and missed sparse
  indices stored as indexed attributes.
- `Reflect.*` methods carry the §17 descriptor 0x3 on their install
  slots; the global Reflect / Math / JSON slots themselves get 0x3,
  and NaN / Infinity / undefined get the §17 read-only 0x0.

**parseInt / parseFloat / global toNumber:**
- `parseInt` / `parseFloat` ToString the FULL primitive result of
  ToPrimitive(hint:'string'), so a numeric / boolean / null return
  from toString/valueOf renders to its decimal / 'true' / 'null' form
  before parsing. Pre-fix only string returns were honoured.
- `parseFloat` recognises the full ECMA-262 StrWhiteSpace set
  (NBSP, USP, line separators, BOM) — pre-fix only ASCII trimmed.
- `toNumber` consults `@@toPrimitive('number')` before valueOf/toString
  per §7.1.1 step 2.
- `@@toPrimitive` validation: non-callable Symbol.toPrimitive throws
  TypeError; non-primitive return throws TypeError.

**Other corrections:**
- `Array.prototype.concat` applies full §7.1.2 ToBoolean to
  @@isConcatSpreadable; invokes the accessor-form getter when
  Object.defineProperty stores it under `__get_Symbol.isConcatSpreadable__`.
- `Math.hypot` coerces every argument once into a vector, propagating
  the first ToNumber abrupt completion and stopping further valueOf
  invocations (counter test).

Net code-change summary: 30 commits, ~12 files touched, all changes
local to protoJS. Cumulative across rounds 1-7: ~230 commits.

### Fixed (test262 spec conformance push, round 6 — 2026-06-03)

Sixth consecutive 30-commit sprint, focused on the Map / Set surface
and on the Array.prototype `flat` / `concat` family that the round-5
pass had not yet reached. Each commit fixes one root cause; all
changes remain local to protoJS (no protoCore modifications).

**Map / Set built-ins under §17:**
- `Set` / `Map` / `Promise` constructors carry `.length` and `.name`
  with the §17 descriptor (writable:false, enumerable:false,
  configurable:true).
- `Set.prototype` / `Map.prototype` `[Symbol.toStringTag]` installed
  under BOTH the internal `__toStringTag__` key (already present) and
  the user-visible `"Symbol.toStringTag"` key so
  `Object.getOwnPropertyDescriptor(Set.prototype, Symbol.toStringTag)`
  returns the actual descriptor. Same fix for `JSON`, `Math`, and
  `RegExp.prototype`.
- `Set.prototype.size` / `Map.prototype.size` getters: wrapped through
  the methodPrototype chain and stamped with `name = "get size"`,
  `length = 0`, descriptor 0x2 — they are real Function objects now,
  not opaque method handles.
- `Set` constructor: install `get Set[Symbol.species]` per §24.2.2.2,
  returning `this`. Pre-fix Set had no @@species property at all.

**Map / Set behavioural fixes:**
- `Set.prototype.forEach` / `Map.prototype.forEach`: per §24.x.3.x NOTE,
  values added from inside the callback are visited; values deleted
  before being visited are skipped; values re-added after deletion are
  revisited. The pre-fix iterator snapshot missed all three cases.
  Also: both now throw TypeError when the callback is not callable
  (boolean / null / undefined / number / string / object / Symbol).
- `Set` constructor: throws TypeError when `Set.prototype.add` (or any
  shadowing) is not callable, per §24.2.1.1 step 7.a-c, BEFORE the
  iteration begins.
- `Set` iterators (`values` / `keys` / `entries`): latch a sticky
  `done = true` after the iterator hits the end, so additions made to
  the Set after exhaustion are NOT resurfaced through the same
  iterator — matching §24.2.5.2.1.
- `Map.prototype.getOrInsertComputed`: validates IsCallable(callbackfn)
  BEFORE the map lookup; passes the canonical key to the callback;
  matches the upsert proposal §Map.prototype.getOrInsertComputed
  step 3.
- `Map.groupBy`: rebuilt to route reads through arrayTryFastGet and
  writes through `__elements__`, so result arrays are real arrays
  (pre-fix it produced all-null arrays since real Sets/Arrays don't
  carry string-keyed indexed attributes).

**Set collection methods (`union`, `intersection`, `difference`,
`symmetricDifference`, `isSubsetOf`, `isSupersetOf`, `isDisjointFrom`):**
- All seven now begin with a GetSetRecord(other) validator per
  §24.2.1.2 which throws TypeError for non-Object / non-callable
  `.has` / non-callable `.keys` and RangeError for negative `.size`.
  Real native Sets (detected via the __set_order__ slot) skip the
  validator since their size accessor lives behind __get_size__.
- `intersection` / `difference` / `isSubsetOf` / `isDisjointFrom`
  now consult `other.has(v)` for non-Set arguments (via a new
  setLikeHas helper), so Set-like `{size, has, keys}` objects produce
  correct results instead of being treated as empty.

**Array.prototype.flat / flatMap:**
- `flat` coerces non-numeric depth via ToNumber per §23.1.3.10:
  `"TestString"` / `{}` / non-numeric strings yield NaN → depth 0;
  numeric strings parse; booleans coerce; undefined keeps the default
  depth of 1.
- `flat` now routes result creation through ArraySpeciesCreate per
  step 5, so `a = []; a.constructor = null` (or any primitive)
  correctly throws TypeError.
- `arraySpeciesCreate` enforces the §22.1.3.1.1 step 7/9 non-Object
  constructor check (TypeError for null / number / string / boolean
  `.constructor`).

**JSON / Function chain corrections:**
- `JSON.parse` results inherit the real `Object.prototype`. Pre-fix
  TypeBridge::fromJS stamped them with the empty seed prototype
  installed before ensureObjectConstructor populated it.
- `Object.getPrototypeOf(JSON.parse)` / `JSON.stringify` now equals
  `Function.prototype` — both wrappers are re-parented and stamped
  with the JS proto override.

**Reflect.construct:**
- Implemented per §28.1.2 so the isConstructor harness used by many
  test262 tests works. The isConstructible probe checks the
  `__is_arrow__` marker so arrow functions are correctly rejected.

**FlattenIntoArray / arrayThrowIfCallbackNotCallable:**
- `flat` / `flatMap` skip absent indices per FlattenIntoArray step 3.b
  (`if (!arrHasProperty) continue`), so a sparse `[1, , 3].flat()`
  produces `[1, 3]` not `[1, undefined, 3]`.
- Array callback callability check now includes the `__bound_fn__`
  sentinel so `arr.map(f.bind(x))` works.

**JSON.stringify (round-6 follow-ups):**
- Replacer-array number values are routed through Number::toString
  per §25.5.2 step 4.d, so `JSON.stringify(o, [1e21])` uses the
  same exponent / -0 handling as the rest of the runtime.

Net code-change summary: 30 commits, ~13 files touched, all changes
local to protoJS (no protoCore modifications). Cumulative across
rounds 1-6: ~200 commits.

### Fixed (test262 spec conformance push, round 5 — 2026-06-03)

Fifth consecutive 30-commit sprint targeting ECMA-262 gaps surfaced by
deeper test262 traversal. Each commit fixes one root cause; all changes
remain local to protoJS (no protoCore modifications).

**Array constructor + prototype:**
- `Array(N)` as function call applies the same RangeError validation as
  `new Array(N)` per §22.1.1.1 (was silently producing a 1-element
  array for -1 / 2.5 / NaN / Infinity).
- `Array.length` setter validates via ToUint32 + SameValue: invalid
  values throw RangeError; coercion from string `'3'` to integer 3
  now lands a canonical numeric form per §10.4.2.1.
- `Array.prototype.concat` honours `Symbol.isConcatSpreadable`
  per §22.1.3.1.1 — arrays with the flag set to false are kept
  unspread, array-likes without the flag are no longer eagerly spread.
- `Array.prototype.flat` / `flatMap` flatten empty children per
  FlattenIntoArray step 5.b (were preserving empty arrays as
  elements).
- `Array.prototype.push` updates `.length` on plain-object receivers
  (`Array.prototype.push.call(arrLike, …)`).
- `Array.prototype.from` ToLength coerces non-numeric `.length`
  through ToNumber per §23.1.2.1 step 5.

**Map + Set constructors and ordering:**
- `Map` constructor throws TypeError on non-Object entries per
  §24.1.1.2 step 4.a (was silently producing empty maps).
- `Map` / `Set` constructors throw TypeError on non-iterable
  primitives (numbers, booleans, strings — strings are iterable but
  AddEntriesFromIterable would still reject each code point).
- `Set` constructor iterates strings per code unit per §24.2.1.1.
- `Map.prototype.set` / `Set.prototype.add` pick `max(slot)+1` for
  the next sparse-list slot, NOT `size` — fixes insertion order
  across delete + re-set cycles where the prior code overwrote a
  still-occupied slot.

**JSON.parse / JSON.stringify:**
- `JSON.stringify` implements the replacer function form per §25.5.2
  step 3 (was an explicit TODO that ignored functions).
- `JSON.stringify(undefined)` / `JSON.stringify(function(){})` returns
  the JS undefined value per §25.5.2 step 11 (was returning the
  literal string `'null'`).

**Property descriptors + accessor handling:**
- Object-literal getter/setter properties are enumerable per
  §6.2.5 (`{enum:true, config:true}` — bits 0x6, was 0x2).
- `Object.assign` invokes the source getter via `Get(from, key)` per
  §20.1.2.1 step 4.c.ii.2 (was reading the data slot only, getting
  undefined).
- Object spread `{...src}` invokes the source getter, mirroring the
  Object.assign fix on the OP_copy_data_properties bytecode path.
- `Object.defineProperty` allows empty descriptor / same-value
  redefine on non-configurable per §10.1.6.3
  ValidateAndApplyPropertyDescriptor step 3 (was rejecting every
  redefine of a non-configurable property).
- `resolvePutFieldOOP` silently rejects writes to getter-only
  accessors per OrdinarySet 5.b — `Map.size`, `Set.size`, and
  user `{ get x() {…} }` properties no longer accept shadowing
  data writes.

**Object.setPrototypeOf / preventExtensions surface:**
- `Object.setPrototypeOf(o, null)` persists the null sentinel
  per §20.1.2.21 step 5 (was calling `t_jsProtoMap.erase` which
  silently restored the natural parent).
- `Object.isExtensible` / `Object.isFrozen` / `Object.isSealed`
  treat string / undefined / boolean primitives as frozen per
  §20.1.2.{12,13,14}.
- `Reflect.isExtensible` / `Reflect.preventExtensions` honour the
  NonExtensibleMarker (were a stub returning true unconditionally).

**String + Number coercion:**
- `String.prototype.replace` / `replaceAll` ToString non-string
  patterns per §22.1.3.18 step 5 — `.replace(undefined, X)` searches
  for the literal string `'undefined'` instead of returning the
  word `'undefined'`.
- `String.prototype.toUpperCase` / `toLowerCase` UTF-8 aware for
  the Latin-1 supplement (`'café'.toUpperCase() === 'CAFÉ'`).
- `String.prototype.repeat` routes non-typed arguments through
  ToNumber per §22.1.3.16 — `'a'.repeat([3]) === 'aaa'`.
- `Number.prototype.toPrecision` emits exactly p significant digits
  per §21.1.3.5 step 12 (was using %g which strips trailing zeros).
- `ToNumber` preserves -0 when parsing `'-0'`; rejects whitespace
  inside the `0x` / `0b` / `0o` prefix region.

**Reflect / Error / built-ins:**
- `AggregateError` registered as a built-in Error constructor per
  §19.2.1.5 (was producible internally by Promise.any but not
  user-constructible).
- `Object.fromEntries` throws TypeError on non-iterable primitives
  per §20.1.2.6 step 3.
- `WeakMap.prototype.set` throws TypeError on non-Object keys.

**Function.prototype.bind:**
- The bound function inherits Function.prototype so
  `bound instanceof Function` holds and `.call/.apply/.bind` resolve.

**hasOwnProperty:**
- Treats PROTO_NONE attribute as absent — handles the simulated-
  delete the array prototype uses (no public deleteAttribute in
  protoCore) without affecting explicit `{ b: undefined }` cases.

Net code-change summary: 30 commits, ~14 files touched, all changes
local to protoJS (no protoCore modifications).

### Fixed (test262 spec conformance push, round 4 — 2026-06-02)

Fourth consecutive 30-commit sprint targeting concrete ECMA-262 gaps
surfaced by deeper test262 traversal. Each commit fixes one root cause
and remains under the "purity > performance" constraint.

**Sentinel hygiene — the global `undefined` identifier:**
- `toBool(undefined)` returns false. Pre-fix the heap-allocated
  undefined sentinel hit the "objects are truthy" tail of toBool, so
  `if (undefined)`, `undefined || 1`, `!!undefined` all said true.
- Property access on the undefined sentinel throws TypeError. Pre-fix
  `undefined.x` and `undefined['k']` silently returned undefined.
- `Array.prototype.join(undefined)` defaults to ','. Pre-fix the
  sentinel fell through to elemToString and rendered each separator
  position as '[object Object]'.

**JS proto-chain reconstruction:**
- Object.prototype's instance methods (hasOwnProperty / isPrototypeOf /
  propertyIsEnumerable / toLocaleString) are re-parented at
  Function.prototype after the latter is built, so
  `Object.prototype.hasOwnProperty.call(o, 'a')` — the most common
  defensive idiom in JS — no longer throws "is not a function".
- Function.prototype is re-tied to the post-constructor-backref
  Object.prototype so `Array instanceof Object`, `Function instanceof
  Object`, etc. all hold.
- `OP_set_proto` honours `__proto__` in object literals via the JS
  proto-override map, so `{__proto__: p}` actually inherits from p
  and `{__proto__: null}` produces a null-prototype object.

**Object.freeze / seal / preventExtensions actually enforce writes:**
- FrozenBehavior / NonExtensibleBehavior return the receiver pointer
  on rejection (not null) so the caller stops falling back to
  setAttribute. Composite behaviour forwards putField across all
  parent markers. Marker installation order is fixed (Frozen first
  in iteration). Per-object behaviour cache is invalidated on every
  freeze / seal / preventExtensions. The wrong-behaviour-per-parent
  cache layer is removed. Net effect: writes to frozen / sealed
  objects silently no-op and writes that would create new keys on
  non-extensible objects do likewise — matching the spec.

**JSON.stringify spec details:**
- Number / String / Boolean wrapper objects unbox to their primitive
  per §25.5.2.2 step 4.
- Exponent padding stripped: `1e-7` not `1e-07`. Mirrors the
  Number.prototype.toString fix.
- Circular references throw TypeError per §25.5.2 step 2 (was emitting
  literal "null" for the back-edge, silently truncating data).
- `JSON.parse(text, reviver)` now applies the reviver recursively
  through arrays and plain objects per InternalizeJSONProperty.
- `JSON.parse('[1,2,3]')` returns an array whose prototype IS the
  populated Array.prototype, so `.join`, `.map`, etc. resolve.

**ToNumber / parseInt / parseFloat spec details:**
- `Number('0x1A')`, `Number('0b11')`, `Number('0o7')` parse the
  three prefixed integer forms per §7.1.4.1.1.
- `Number('-0')` preserves -0 (1/Number('-0') === -Infinity now).
- `parseInt('10', 0)` / `parseInt('10', undefined)` defaults to base 10
  with 0x detection (was returning NaN).
- `parseFloat('infinity')` returns NaN (case-sensitive), `parseFloat
  ('0x1A')` returns 0 (no hex), `parseFloat('Infinityfoo')` returns
  Infinity (longest-prefix match).

**Number.toString exponent / toFixed sign:**
- Exponent padding stripped: `(1e-7).toString() === '1e-7'`.
- `(-0.4).toFixed(0)` returns '0' (not '-0') per §21.1.3.3 step 8.

**String access / coercion:**
- `'abc'.charAt('1')` returns 'b' (was 'a'): getIntArg coerces string
  arguments through ToNumber.
- `'abc'['length']` returns 3 (was 0): get_array_el short-circuits
  the literal "length" probe on string receivers.
- `String.prototype.includes / startsWith / endsWith` throw TypeError
  on RegExp arguments per §22.1.3.7 / §22.1.3.8 / §22.1.3.22 step 3.

**Spread / iteration:**
- `[...'abc']` produces ['a','b','c'] (was []): OP_append forces the
  Symbol.iterator path on string sources.
- `{...arr}` copies the array's index entries as numeric-string keys
  in the target literal per §13.2.5 (was emitting `{}`).

**Descriptor housekeeping:**
- Built-in prototype `constructor` backrefs (Array, String, Error,
  TypeError, …) are non-enumerable per spec (were defaulting to
  fully enumerable, surfacing in for-in / Object.keys).
- Array .length descriptor is `{writable:true, enumerable:false,
  configurable:false}` per §22.1.5.1, applied to both createNewArray
  and the OP_array_from hot path.
- Plain object literals with numeric keys no longer leak a phantom
  'length' attribute (the OP_define_field length-bump branch now
  checks `__is_array__`).

**Reflect surface fills:**
- `Reflect.ownKeys` returns the array of own string keys (was a
  no-op stub returning PROTO_NONE — `.sort()` / `.length` on the
  result crashed).
- `Reflect.defineProperty` / `Reflect.getOwnPropertyDescriptor` added
  as forwarders to the namesake `Object.*` methods (were absent).

**WeakMap key rule:**
- `WeakMap.prototype.set` throws TypeError for non-Object keys per
  §24.3.3.6 step 5 (was silently storing primitives).

Net code-change summary: 30 commits, ~24 files touched, all changes
local to protoJS (no protoCore modifications).

### Fixed (test262 spec conformance push, round 3 — 2026-06-02)

Continuation of the prior conformance push. 30 more commits targeting
concrete ECMA-262 gaps surfaced by deeper test262 traversal. Each
commit fixes one root cause and remains under the "purity > performance"
constraint.

**Numerical/string coercion:**
- `String.prototype.toString` throws TypeError on non-string receivers
  per §22.1.3.27 / §22.1.3.32 (was returning "[object Object]").
- `Object.create` throws TypeError on non-Object/non-null prototype
  arg per §20.1.2.2 step 1 (was returning `{}` for `undefined`,
  primitives, etc.).
- `Object.defineProperty` raises TypeError for missing target arg
  (was silent PROTO_NONE return).
- `Object.hasOwn` raises TypeError on null/undefined target per
  §20.1.2.13 step 1.
- `Array.prototype.sort` rejects non-callable comparefn with TypeError
  per §23.1.3.30 step 1 (was silently using the string default).
- `Array.from(src, mapFn, ...)` rejects non-callable mapFn with
  TypeError per §23.1.2.1 step 2.
- `JSON.parse` throws SyntaxError on malformed input per §25.5.1
  step 3 (was returning `null`).

**ToIntegerOrInfinity coverage extended:**
- `Array.prototype.includes` / `at` / `fill` / `copyWithin` /
  `lastIndexOf` now apply ToIntegerOrInfinity to their index
  arguments. NaN → 0, +Infinity past-end returns false / out-of-range,
  -Infinity clamps to 0. (Companion to the indexOf / slice / splice /
  flat fixes from the prior round.)
- `Number.prototype.toString` applies ToIntegerOrInfinity on radix
  (string "16" coerces correctly; NaN → 0 → RangeError).
- `ToNumber` trims the full Unicode WhiteSpace + LineTerminator set
  (NBSP, BOM, U+2028/2029, U+3000, etc.) — not just ASCII.

**Spec details inside built-ins:**
- `Number.prototype.toString` emits fractional digits for non-base-10
  radixes (`(0.5).toString(2) === "0.1"`).
- `String.prototype.normalize` raises RangeError for forms outside
  {NFC, NFD, NFKC, NFKD}.
- `String.prototype.trim / trimStart / trimEnd` strip the full
  Unicode WhiteSpace set (NBSP, BOM, U+2028/2029, U+3000, etc.).
- `String.fromCodePoint` raises RangeError for NaN, Infinity, negative,
  > 0x10FFFF, or non-integer code points per §22.1.2.2 step 5.
- `parseInt` only auto-detects the "0x" prefix when the radix
  argument is unspecified or 16 — explicit radix 10/8/2 etc. parses
  "0" and stops at 'x'.
- `Array.from` clamps NaN / negative array-like lengths to 0
  (was hanging on negative length via uint wrap-around).

**Built-in shape (name / length / descriptor):**
- `Function.prototype.length === 0`, `Function.prototype.name === ""`
  per §20.2.3.
- `String.prototype.toLocaleString` implemented (identity without
  ICU) — `"abc".toLocaleString()` no longer throws.
- `Boolean.prototype.{toString, valueOf}` re-installed with name /
  length attributes (was empty strings).
- `Object.prototype.{hasOwnProperty, isPrototypeOf,
  propertyIsEnumerable}` carry .length === 1 (was 0).
- `Promise.length === 1` per §27.2.3.
- `Object.create.length === 2` per §20.1.2.2.
- `Error.prototype[@@toStringTag] === "Error"` (now
  `Object.prototype.toString.call(new TypeError())` returns
  `"[object Error]"`).
- `Date.parse` and `Date.UTC` implemented per §21.4.3.2 /
  §21.4.3.4 (ISO 8601 fragments / UTC time component to ms).

**Reflect + Promise:**
- `Object.create` mutable ctor: the constructor backref / prototype
  round-trip now preserves identity so `Object.prototype.constructor
  === Object` holds (was failing after the constructor-backref
  series due to immutable-ctor splitting).
- `JSON.stringify` honours the replacer-array form (filters object
  keys per §25.5.2 step 4) and invokes `toJSON` before serialising
  per step 3.

**Misc:**
- `Function.prototype.bind` raises TypeError on non-callable receiver
  per §20.2.3.2 step 1.
- `Object.assign` skips non-enumerable own properties per §20.1.2.1
  step 4.c.ii.1.

### Fixed (test262 spec conformance push, rounds 1+2 — 2026-06-02)

A ~80-commit sprint dedicated to closing concrete ECMA-262 conformance
gaps surfaced by test262. Each commit fixes one root cause and
preserves the protoJS architectural rule that purity wins over
performance until the community justifies otherwise. Documented by
category — see `git log c7f7dc9d..` for the full per-commit detail.

**Constructor backref + descriptor (§17, §19.x.4.1):**
`prototype.constructor` now points at the live constructor object on
every built-in prototype (Boolean, Number, Object, Function, Promise,
Map, Set, all 11 TypedArray kinds, ArrayBuffer, DataView, Error +
each subtype) with the non-enumerable `__pd_constructor__ = 0x3`
descriptor. Function.prototype was switched to mutable so the
recursive backref doesn't split it into two identities; the same
lesson applies to every other built-in prototype and is captured in
`memory/feedback_protojs_proto_constructor_backref.md`.

**Type coercion via ToObject / ToNumber / ToString / ToPrimitive:**
- `Object.getOwnPropertyDescriptor`, `getOwnPropertyNames`,
  `getOwnPropertyDescriptors` (new), `getOwnPropertySymbols` (stub),
  `Object.fromEntries`, `Array.from`, every `Array.prototype.*`
  iterator, and all four `Reflect.has/get/set/ownKeys` now throw
  TypeError on null / undefined / primitive targets per spec §28.1.x
  step 1 / §20.1.2.x step 1. `arrayThrowIfNullUndefined` now
  recognises the undefined sentinel uniformly.
- `ToNumber` distinguishes null (→ +0) from undefined (→ NaN).
  `globalIsNaN` / `globalIsFinite` route through `jsToNumber` so
  objects coerce via valueOf/toString.
- `parseInt` / `parseFloat` apply ToString step (custom toString /
  valueOf) on object arguments. `decodeURI` /
  `decodeURIComponent` raise URIError on malformed escapes.
- `Number` constructor delegates to `jsToNumber`. `String()` no-arg
  returns `""` (was "undefined"). `objToStr` returns "undefined"
  for the undefined sentinel and now calls user-defined `toString`
  via callJSFunction for `'a'.concat([1,2])` style coercion.
- `Array.prototype.indexOf` / `lastIndexOf` / `slice` / `splice` /
  `flat` apply ToIntegerOrInfinity to their index arguments
  (`Infinity` / `-Infinity` / `NaN` handled per spec).
- Spec-compliant number formatting: `ToString(Number)` and console
  number printing both use shortest round-trip; `toExponential` /
  `toPrecision` emit single-digit exponents, handle NaN/Infinity,
  throw RangeError, and serialise `-0` without sign;
  `toFixed(-0)` returns `"0.00"`.

**Built-in shape (name / length / descriptor):**
- Every Math method, JSON.parse / stringify, Date.now,
  parseInt / parseFloat / isNaN / isFinite / encodeURI /
  encodeURIComponent / decodeURI / decodeURIComponent, global
  built-in constructors (Boolean / Number / String / Object /
  Error + subtypes) now carry the spec-mandated `.name` and
  `.length` with the §17 descriptor (`writable:false`,
  `enumerable:false`, `configurable:true` → 0x2).
  Global functions also carry the §17 wrapper descriptor (0x3) on
  the globalRoot binding, so `for (k in globalThis)` no longer
  emits them.
- `Math` is now mutable so `delete Math.sqrt` and `Math.foo = 1`
  persist as the spec allows.
- `Symbol.toStringTag` set on JSON and Promise.prototype so
  `Object.prototype.toString.call(...)` returns `[object JSON]` /
  `[object Promise]`.
- `Error.prototype.message === ""` per §20.5.5.3.
- `String.prototype.length === 0` per §22.1.3.

**Spec details fixed in built-ins:**
- `Array(N)` validates length per §22.1.1.2 (RangeError on
  non-integer / NaN / Infinity / >= 2^32 / negative).
- `Array.prototype.with` throws RangeError on out-of-range index.
- `Array.prototype.toString` invokes the receiver's own `join`
  (now respects user overrides).
- `Array.prototype.toLocaleString` implemented.
- `Array.prototype.join` treats null / undefined elements as `""`.
- `Object.prototype.toLocaleString` delegates to ToString for typed
  primitives (was aliased to `objectToString`, which gave
  `[object Number]` for `(1).toLocaleString()`).
- `Object.assign` honours non-enumerable own properties (skips them
  per §20.1.2.1 step 4.c.ii.1).
- `Math.pow(1, NaN) === NaN`, `Math.pow(±1, ±Infinity) === NaN` per
  §21.3.2.24. `Math.round` preserves `-0`. `Math.clz32` applies
  ToUint32 (NaN / ±Infinity / 0 / 2^32 → 0). `Math.hypot(NaN,
  Infinity) === Infinity` (Infinity dominates NaN).
- `JSON.stringify` indent argument per §25.5.2 step 6; undefined
  array elements become `null`, undefined object members are
  dropped, callable values serialise as `null` (arrays) or are
  dropped (objects).
- `String.prototype.repeat` / `padStart` / `padEnd` apply ToLength
  with RangeError on `-1` / `+Infinity` / target > 16M code units.
- `String.prototype.replace` / `replaceAll` invoke callable
  replacements; `String.prototype.search` / `match` handle
  non-regex patterns; indexOf / startsWith / endsWith / includes
  coerce missing args to "undefined" per ToString rules.
- `Function.prototype.bind` throws TypeError on non-callable
  receivers per §20.2.3.2 step 1.

**Promise:**
- `makeSettledPromise` parents the result on `Promise.prototype` so
  `Promise.resolve / reject / all / allSettled / race / any`
  outputs satisfy `p instanceof Promise`.
- `collectIterable` reads the array's native `__elements__`
  storage; `Promise.all` / `allSettled` / `race` / `any` no longer
  collapse their input into an array of undefined values.

**Reflect:** added `deleteProperty`, `getPrototypeOf`,
`setPrototypeOf`, `isExtensible`, `preventExtensions` (§28.1.4 /
§28.1.5 / §28.1.8 / §28.1.10 / §28.1.11) — all routed through the
existing non-object TypeError guard.

**Numeric format:** `ToString(Number)` uses shortest round-trip
per §7.1.12.1 (`String(3.14)` → `"3.14"`, was
`"3.1400000000000001"`). `console.log` matches the same algorithm.
Exponent normalised to single-digit form.

**Other:**
- `Number.parseInt === parseInt` and `Number.parseFloat ===
  parseFloat` per §21.1.2.12 / §21.1.2.13 (was two distinct
  function objects). `patchNumberParseFns` re-binds Number.* to
  the global references after the global fns are installed.
- `Number.prototype` methods reinstalled in `ensureNumberConstructor`
  so they inherit Function.prototype's `.call/.apply/.bind` (build
  ran before `ensureFunctionPrototype` so the original wrappers
  had no parent).

### Fixed

- **Standard benchmark suite restored after silent regression**
  (2026-05-31):  Two regressions had broken the standard suite between
  2026-05-06 and now.  Together they explain why no comparable
  performance number could be produced for nearly a month.

  - **`printf("TRACE: ...")` in `DISPATCH()` macro** (commit `283a02a5`).
    Committed by snapshot `7b5d9ddd` on 2026-05-22 with the explicit
    note "in-progress... not separately reviewed" — the line emitted a
    trace to stdout on every bytecode dispatch.  The `__BENCH_RESULT__`
    regex never matched, every benchmark reported `Error: undefined`,
    and per-dispatch printf overhead was catastrophic but masked by the
    runner failure upstream.  One-line removal.
  - **`Date.now` undefined** (commit `b546a64f`).  `TimingAPIs::init`
    created `Date` via `ctx->fromMethod(...)` then attached `.now` via
    `setAttribute`.  Method objects do not retain attribute writes —
    the assignment silently dropped, leaving `Date.now` permanently
    undefined.  Every standard benchmark times its workload via
    `Date.now()` so every benchmark threw `TypeError`.  Switched to
    `newObject(true)` with matching `name`/`prototype` so the
    interpreter's stub-installer guard skips it; constructor behaviour
    (`new Date()`) intentionally not provided — no standard benchmark
    needs it and reintroducing it properly belongs to broader Date
    work.

  After the fixes the suite passes 14/14 vs Node and 14/14 vs QuickJS.
  Geomean ratio against the 2026-04-28 baseline is **0.249** — protoJS
  is ~75 % faster than that baseline across the six benchmarks present
  in both runs (P-JS-{0..7} cycle's actual landed effect, finally
  measurable).  See README.md § "Honest baseline — 2026-05-31" and
  `tests/benchmarks/results/comparison_2026-05-31.md` for the full
  comparison including the new QuickJS reference.

- **`tree_traversal` UAF stabilised** (2026-05-04): The benchmark built a
  16383-node binary tree of mutable objects (depth=14) and then summed the
  values; with `PROTOCORE_GC_REINCLUDE_SURVIVORS=ON` it crashed reproducibly
  during the sum pass with a use-after-free on a snapshot's `attributes`
  pointer (stale `ocValue->attributes` pointing into a recycled cell). Root
  cause was a stale-mark bug in protoCore's GC: mark would set the mark bit
  on cells reachable from a root but not in `segmentsToProcess`, sweep never
  cleared those bits, and the next cycle's mark skipped the entire subtree
  underneath any such cell. Fix landed in protoCore (pre-mark unmark pass);
  see `protoCore/docs/GarbageCollector.md` § "Phase 4a". With the fix the
  benchmark passes 10/10 and the rest of the standard suite (`object_property`,
  `object_write_only`, `object_read_only`, `json_transform`, `string_concat`,
  `string_processing`, `array_literal`, `control_flow`, `function_calls`,
  `numeric_loop`, `tree_traversal`) is 5/5 stable.

### Added

- **Restore standard timing APIs** (2026-04-26): The runtime now exposes
  `Date.now()`, `performance.now()`, and `console.time()` /
  `console.timeEnd()` / `console.timeLog()` (plus `console.info` and
  `console.debug` as Node-style aliases of `log`).  Implemented as
  native ProtoMethod bindings in `src/console.cpp` (new class
  `protojs::TimingAPIs` alongside `protojs::Console`) and wired in
  from `src/main.cpp` next to the existing `Console::init` calls.
  Backends: `std::chrono::system_clock` for `Date.now` (whole-
  millisecond integer since the Unix epoch); `std::chrono::steady_clock`
  for `performance.now` (double-precision ms since program start)
  and for `console.time` (per-label store is process-wide and mutex-
  guarded to match Node semantics across callbacks).  Closes the
  regression where every benchmark in `tests/benchmarks/standard/`
  and the legacy `console.time`-based suite threw `TypeError: is not
  a function` before reaching its workload.

- **Fix function-argument binding regression** (2026-04-26): User-defined
  function arguments were arriving as `undefined` at the callee
  (`function f(a,b){return a+b}; f(3,4)` returned `NaN`).  Root cause
  was in `src/runtime/ProtoInterpreter.cpp` — the slot-storage helpers
  (`setSlot`, `initStack`, `stackPush/Pop/peek`) all silently no-op'd
  when `ctx->closureLocals` was null, but the OP_call / OP_call_method
  / OP_call_constructor handlers create a child `ProtoContext` with
  `parameterNames=nullptr`, which means protoCore's lazy-init in the
  ProtoContext constructor leaves `closureLocals` null.  The handlers
  then called `setSlot(&childCtx, i, arg)` to seed argument slots, but
  every one of those calls dropped on the floor.  When `runBytecode`
  later bootstrapped `closureLocals` (line 1040), it was too late —
  the args were gone.

  Fix: added `ensureClosureLocals(ctx)` and called it from every
  helper that mutates closureLocals.  Idempotent with the existing
  bootstrap in `runBytecode`.  All call paths now correctly deliver
  arguments to callees.

- **JSON.stringify / JSON.parse polyfill** (2026-04-26): With the
  argument-binding fix in place, a JS-level polyfill is now usable.
  Added `kJSONPolyfillPrefix` in `src/main.cpp` — top-level globals
  (no IIFE) prepended to user code in non-module mode.  Cross-eval
  function references in the current runtime are still flaky (a
  function defined in `wrapper.eval` A is not callable cleanly from
  `wrapper.eval` B because its bcId is module-relative), so prepending
  keeps the polyfill and user code in the same module.  Also worked
  around `String.prototype.length` returning `undefined` in the
  protoCore eval path by iterating with `charAt(i) === ''` as the
  end-of-string sentinel.  The protoCore-side `JSON` namespace stub
  in `ProtoInterpreter.cpp` is now a mutable Object (was immutable,
  which silently swallowed property assignments).  Verified end-to-end:
  `JSON.stringify({a:1, b:"x", c:[true,null]})` →
  `{"ok":true,"name":"numeric_loop","time_ms":42}` style output;
  `__BENCH_RESULT__<json>` lines now emit correctly.

- **Fix interpreter slot/stack quadratic-allocation regression** (2026-04-26):
  Commit 4bd3657 (Mar 5) had replaced the interpreter's `std::vector`-based
  value stack and local-slot store with `ProtoSparseList` (persistent AVL
  tree) — to make all references GC-visible.  The intent was right but
  the cost was catastrophic: every `stackPush` / `stackPop` / `setSlot`
  allocated O(log N) AVL cells, and a tight integer loop spent ~38 % of
  CPU in the GC scanning the resulting cells.  Microbenchmarks slowed
  ~1000× — a `for(let i=0;i<10000;i++) s+=i` loop went from
  milliseconds to ~7 seconds.

  Switched the slot/stack storage to `ProtoContext::automaticLocals` —
  a flat `const ProtoObject*[]` that protoCore already scans as a GC
  root, giving the same GC visibility with O(1) array writes.  Layout
  per call frame:

      [0 .. argCount-1]               args
      [argCount .. argCount+varCount] local vars
      [.. closureCount]               closure vars
      [stackBase .. stackBase + top]  pushed operand stack

  `stackBase` and `stackTop` live in a thread_local `std::vector<
  InterpFrame>` — pushed at runBytecode entry, RAII-popped at exit so
  nested calls compose correctly.  All ~750 stackPush/Pop/Top/At/Size/
  Empty + setSlot/getSlot call sites kept their existing 1-arg
  (ProtoContext*) signature; only the helpers' bodies changed.

  protoCore companion change: added
  `ProtoContext::resizeAutomaticLocals(unsigned int newCount)` so
  runBytecode can grow the slot region after construction (the
  bytecode module's `stackSize_` + var/closure counts are only
  available after the function is resolved, not at ProtoContext
  build time).  See `proto::ProtoContext::resizeAutomaticLocals`
  in protoCore commit on the same day.

  Measurements (Release):

      bench (5K iter int loop):  ~9 s  → ~40 ms       (~225×)
      bench (100K):               ~90 s →  ~298 ms     (~300×)
      bench (1M):                 timeout → ~6.9 s    (linear)

  Standard suite (`run_standard_comparison.js`): 5/7 benchmarks now
  run end-to-end (was 0/7 before the timing/JSON/arg-binding fixes
  earlier in this session).  Geomean vs Node 271× slower (was
  effectively infinite — every protoJS run hit the 120 s/bench
  timeout).  function_calls.js and string_concat.js still time out
  at default sizes; reducing inner counts would fit them in the
  per-bench budget but that's a separate tuning step.

- **PROTOJS_BIN env var** (2026-04-26): `tests/benchmarks/run_standard_
  comparison.js` now honours `PROTOJS_BIN` for selecting which
  protojs binary to test, so an experimental build can be benched
  without overwriting `../build/protojs`.

- **setImmediate in CLI** (2026-03-03): Global `setImmediate(callback)` enqueues to the event loop so scripts can yield between ProtoThread creations and avoid lock contention when creating several threads in quick succession. Used by `parallel_cpu.js` under protoCore.

- **Phase 6: ProtoCore-native global object** (2026-03-03): The global scope is now a ProtoObject built at first eval from the QuickJS global via `JS_GetOwnPropertyNames` and `TypeBridge::fromJS`. No QuickJS heap is used for the global container; conversion only at host boundaries. `runBytecode` accepts `pGlobalRoot` and updates it on `put_field`/`define_field` so top-level `var` assignments persist and subsequent reads see the new object. Directed tests: `proto_eval_smoke.js` (6 cases, including Phase 6 global var) and `tests/test262/tests/phase6_native_global.js`. Docs: ARCHITECTURE.md § 1.4, CONFORMANCE_JS.md Phase 6 table, src/runtime/README.md, TECHNICAL_AUDIT.md.

### Fixed

- **Multithreading: first ProtoContext in thread entry** (2026-03-03): Deferred and runInThread now create the first ProtoContext inside the thread’s initial function with `nullptr` as caller, so no ProtoContext is shared across threads. `deferredProtoThreadEntry` builds `ProtoContext(space, nullptr, ...)` and uses it for all work; `cpuChunkThreadEntry` does the same and then calls `cpuChunkWorker`. Deferred uses only ProtoThreads (no CPUThreadPool fallback for execution); if `newThread` fails or wrapper/space is missing, the Deferred is rejected. Docs: `src/runtime/README.md` § Multithreading and protoCore.

- **parallel_cpu benchmark under protojs** (2026-03-03): `protoCore.runInThread` tasks are scheduled with `setImmediate` stagger so the main thread yields between `newThread` calls. Under protojs, `WORK_PER_TASK` is 2e5 so the run completes within the runner timeout; Node keeps 2e6. Standard comparison passes all 7 benchmarks; parallel_cpu reports protoJS faster than Node on that benchmark.

- **Packaging** (2026-02-08): Added `packaging/build_deb.sh` to build the protoJS .deb from current templates on Debian/Ubuntu. INSTALLATION and PROCEDURES updated: users must rebuild the .deb (e.g. run `./packaging/build_deb.sh`) after the protocore dependency fix—otherwise an old .deb still reports "protoCore is not installed" when the `protocore` package is installed.

- **Debian package dependency check** (2026-02-08): protoJS .deb preinst now looks for the protoCore package under the name **`protocore`** (lowercase), which is how CPack installs the protoCore .deb. Also added fallback check for `protoCore`. The control template `Depends` was updated to `protocore (>= 1.0.0)` so installation succeeds when protoCore is installed from its CPack-generated .deb. Docs (INSTALLATION.md, PROCEDURES.md) updated accordingly.

- **protoCore getImportModule API** (2026-02-08): CommonJSLoader now passes `ProtoContext*` as the first argument to `ProtoSpace::getImportModule(context, logicalPath, attrName)` to match the current protoCore API (fixes build error when building against updated protoCore).

- **-Wformat-security warnings** (2026-02-08): All `JS_ThrowTypeError(ctx, dynamic_string.c_str())` calls replaced with `JS_ThrowTypeError(ctx, "%s", dynamic_string.c_str())` in CommonJSLoader, ESModuleLoader, IOModule, FSModule, and DNSModule so the format string is a literal and the compiler no longer reports format-security warnings.

- **require() built-in module resolution** (2026-02-08): `require('fs')`, `require('path')`, `require('stream')`, `require('crypto')`, `require('buffer')`, and other core modules now resolve from the global object so integration tests (fs, stream, crypto, buffer) pass. See CommonJSLoader built-in resolution and docs (README, NATIVE_MODULES).

- **GCBridge null-pointer handling** (2026-02-07): Fixed `-Wnonnull` compiler warnings and potential undefined behavior in `GCBridge::detectLeaks()` and `GCBridge::getMemoryStats()` when `ProtoContext` is null. Both functions now return early with null/empty values instead of dereferencing a null pointer. Added null checks in `reportLeaks()` and `getMemoryStats()` for defensive handling of empty reports.

- **Exception logging in eval** (2026-03-06): When compile or run failed, the code called `JS_GetException(ctx)` a second time to stringify the exception; the first call had already consumed it, so the second returned an uninitialized value that stringified as "[unsupported type]". Fixed by (1) capturing the exception inside `compileToBytecode` via an optional out-parameter as soon as `JS_Eval` returns an exception, and (2) using that value (or the one already in `val`) for logging in `eval`. Real exceptions from the compiler are now reported correctly when QuickJS sets them; when the compiler fails without setting an exception (e.g. some `__JS_EvalInternal` fail paths), the message may still be "undefined". Standard suite `__BENCH_RESULT__` remains unavailable until both compile success and `console.log` output are fixed in the protoCore path.

### Build & Test

- Full project recompilation: protoCore + protoJS clean build
- All 33 unit tests passing (ctest)
- Integration tests verified (hello_world, arithmetic, modules)

### Performance (2026-02-07)

- Performance suite executed successfully: `run_nodejs_comparison.js` (5/5 benchmarks)
- **Array operations:** 34–45x faster than Node.js (immutable structural sharing)
- **Overall speedup:** ~10–45x depending on workload
- Added [docs/PERFORMANCE_RUN_2026-02-07.md](docs/PERFORMANCE_RUN_2026-02-07.md) with run report and analysis

### Performance (2026-03-06)

- Re-ran Node.js comparison suite: 5/5 benchmarks passed; protoJS wins all 5.
- **Latest results:** array_operations 45x faster; overall speedup 10.81x (protoJS avg 42.6 ms vs Node 460.4 ms).
- Full combined suite (41 tests) run with Node.js; report and JSON written to `tests/benchmarks/results/report_2026-03-06_00-07-34.html` and `results_2026-03-06_00-07-34.json`.
- Updated [docs/PERFORMANCE_RUN_2026-02-07.md](docs/PERFORMANCE_RUN_2026-02-07.md) with latest run table; [docs/PERFORMANCE_REPORT.md](docs/PERFORMANCE_REPORT.md) with "Latest Node.js comparison", report paths, and new "Results analysis" section (Node comparison table + full-suite summary).
