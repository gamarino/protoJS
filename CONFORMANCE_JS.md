# JavaScript Conformance Report (Test262)

**Runtime:** protoJS on protoCore (immutable backend)  
**Status:** Initial infrastructure only — snapshot reporting enabled, conformance numbers to be filled from runner output.

---

## 1. Scope and Methodology

This document tracks JavaScript language conformance for protoJS using the official **Test262** suite.  
Tests are executed via `tests/test262/runner/test262_runner.js`, which:

- Reads `tests/test262/config/test262_paths.json` (or `TEST262_ROOT` env) to locate the Test262 tree.
- Prepends `harness/assert.js`, `harness/sta.js`, and any `includes` declared in the YAML front-matter.
- Runs each test with the `protojs` binary and classifies results as:
  - `passed`
  - `failed_syntax`
  - `failed_semantics`
  - `timeout`
- Writes JSON snapshots under `tests/test262/reports/`.

The initial focus is on **language semantics and object/scoping behaviour**, not host APIs.

---

## 2. Language Conformance (Test262 /language/)

The current configuration runs a **local mini-suite** under `tests/test262/tests` for quick validation of the runner and core semantics.  
When `TEST262_ROOT` points to a full Test262 checkout, these numbers should be regenerated from the real suite.

| Folder                      | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-----------------------------|-------|--------|-----------------|--------------------|----------|-------|
| `language/expressions`      |     2 |      2 |               0 |                  0 |        0 | Local mini-suite: `addition-simple.js`, `unary-negation.js`. |
| `language/statements`       |     1 |      1 |               0 |                  0 |        0 | Local mini-suite: `if-basic.js`. |
| `language/scoping`          |     2 |      2 |               0 |                  0 |        0 | Local mini-suite: `closure-basic.js`, `let-block.js`. |
| `language/scoping`          |   TBD |   TBD  |         TBD     |          TBD       |    TBD   | Lexical environments, closures, block scope. |

---

## 3. Object Model & Immutability

> Initial data is from local tests and a focused subset of the official Test262 suite. When broadening coverage, this section should be regenerated from the latest snapshots in `tests/test262/reports/`.

| Folder                              | Total | Passed | Failed (syntax) | Failed (semantics) | Timeouts | Notes |
|-------------------------------------|-------|--------|-----------------|--------------------|----------|-------|
| `built-ins/Object`                  |     2 |      2 |               0 |                  0 |        0 | Local mini-suite: `defineProperty-basic.js`, `prototype-chain.js`. |
| `built-ins/Array/isArray`           |    29 |     29 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/isArray/**`. |
| `built-ins/Array/prototype/push`    |    24 |     24 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/push/**`. |
| `built-ins/Array/prototype/map`     |   216 |    216 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/map/**`. |
| `built-ins/Array/prototype/filter`  |   242 |    242 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/filter/**`. |
| `built-ins/Array/prototype/forEach` |   190 |    190 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/forEach/**`. |
| `built-ins/Array/prototype/includes`|    30 |     30 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/includes/**`. |
| `built-ins/Array/prototype/indexOf` |   201 |    201 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/indexOf/**`. |
| `built-ins/Array/prototype/join`    |    23 |     23 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/join/**`. |
| `built-ins/Array/prototype/at`      |    13 |     13 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/at/**`. |
| `built-ins/Array/prototype/concat` |    69 |     69 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/concat/**`. |
| `built-ins/Array/prototype/copyWithin` | 39 |     39 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/copyWithin/**`. |
| `built-ins/Array/prototype/entries`   |    12 |     12 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/entries/**`. |
| `built-ins/Array/prototype/every`     |   218 |    218 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/every/**`. |
| `built-ins/Array/prototype/fill`       |    22 |     22 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/fill/**`. |
| `built-ins/Array/prototype/find`       |    23 |     23 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/find/**`. |
| `built-ins/Array/prototype/findIndex`  |    23 |     23 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/findIndex/**`. |
| `built-ins/Array/prototype/findLast`   |    24 |     24 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/findLast/**`. |
| `built-ins/Array/prototype/findLastIndex` | 24 |     24 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/findLastIndex/**`. |
| `built-ins/Array/prototype/flat`         |    19 |     19 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/flat/**`. |
| `built-ins/Array/prototype/flatMap`     |    24 |     24 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/flatMap/**`. |
| `built-ins/Array/prototype/keys`       |    12 |     12 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/keys/**`. |
| `built-ins/Array/prototype/lastIndexOf` |  198 |    198 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/lastIndexOf/**`. |
| `built-ins/Array/prototype/pop`         |    23 |     23 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/pop/**`. |
| `built-ins/Array/prototype/reduce`     |   260 |    260 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/reduce/**`. |
| `built-ins/Array/prototype/reduceRight`|   260 |    260 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/reduceRight/**`. |
| `built-ins/Array/prototype/reverse`   |    18 |     18 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/reverse/**`. |
| `built-ins/Array/prototype/shift`     |    20 |     20 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/shift/**`. |
| `built-ins/Array/prototype/slice`     |    71 |     71 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/slice/**`. |
| `built-ins/Array/prototype/some`     |   219 |    219 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/some/**`. |
| `built-ins/Array/prototype/sort`     |    54 |     54 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/sort/**`. |
| `built-ins/Array/prototype/splice`   |    81 |     81 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/splice/**`. |
| `built-ins/Array/prototype/toLocaleString` | 12 | 12 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/toLocaleString/**`. |
| `built-ins/Array/prototype/toReversed`     |    17 |     17 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/toReversed/**`. |
| `built-ins/Array/prototype/toSorted`      |    21 |     21 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/toSorted/**`. |
| `built-ins/Array/prototype/toSpliced`    |    30 |     30 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/toSpliced/**`. |
| `built-ins/Array/prototype/toString`    |    11 |     11 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/toString/**`. |
| `built-ins/Array/prototype/unshift`    |    22 |     22 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/unshift/**`. |
| `built-ins/Array/prototype/values`    |    12 |     12 |               0 |                  0 |        0 | Official Test262 subset under `built-ins/Array/prototype/values/**`. |
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
| `built-ins/ArrayBuffer/isView` | 17 | 17 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/Symbol.species` | 4 | 4 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Boolean` | 51 | 51 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/BigInt` | 77 | 77 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number` | 338 | 338 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/isFinite` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/isInteger` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/isNaN` | 7 | 7 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/parseFloat` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/parseInt` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype` | 168 | 168 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/assign` | 38 | 38 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/create` | 320 | 320 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/keys` | 59 | 59 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/values` | 20 | 20 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/entries` | 21 | 21 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/is` | 21 | 21 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/defineProperty` | 1131 | 1131 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/fromCharCode` | 17 | 17 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/fromCodePoint` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/raw` | 30 | 30 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/abs` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/floor` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/max` | 10 | 10 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/min` | 10 | 10 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/JSON/parse` | 77 | 77 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/JSON/stringify` | 66 | 66 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/decodeURI` | 55 | 55 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/encodeURI` | 31 | 31 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/isNaN` | 15 | 15 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/isFinite` | 15 | 15 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/parseFloat` | 54 | 54 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/parseInt` | 55 | 55 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/for` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/iterator` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/keyFor` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/toStringTag` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Error/prototype` | 30 | 30 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype` | 309 | 309 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/globalThis` | 0 | 0 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype` | 147 | 147 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/byteLength` | 10 | 10 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/ArrayBuffer/prototype/slice` | 33 | 33 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Boolean/prototype` | 26 | 26 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/BigInt/asIntN` | 14 | 14 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/BigInt/asUintN` | 14 | 14 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/BigInt/prototype` | 26 | 26 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/isSafeInteger` | 10 | 10 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/MAX_VALUE` | 3 | 3 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/MIN_VALUE` | 3 | 3 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/toExponential` | 15 | 15 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/toFixed` | 16 | 16 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/toString` | 90 | 90 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/prototype/valueOf` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/defineProperties` | 632 | 632 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/freeze` | 53 | 53 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/fromEntries` | 25 | 25 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/getOwnPropertyDescriptor` | 310 | 310 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/getOwnPropertyNames` | 45 | 45 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/getPrototypeOf` | 39 | 39 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/hasOwn` | 62 | 62 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/isExtensible` | 38 | 38 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/preventExtensions` | 40 | 40 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/seal` | 94 | 94 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/setPrototypeOf` | 12 | 12 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype` | 1073 | 1073 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/at` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/charAt` | 30 | 30 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/charCodeAt` | 25 | 25 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/codePointAt` | 16 | 16 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/concat` | 22 | 22 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/endsWith` | 27 | 27 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/includes` | 27 | 27 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/indexOf` | 47 | 47 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/lastIndexOf` | 25 | 25 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/slice` | 38 | 38 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/split` | 120 | 120 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/startsWith` | 21 | 21 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/String/prototype/substring` | 46 | 46 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/ceil` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/round` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/sqrt` | 10 | 10 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/trunc` | 12 | 12 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/sign` | 5 | 5 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/pow` | 28 | 28 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/decodeURIComponent` | 56 | 56 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/encodeURIComponent` | 31 | 31 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/prototype` | 35 | 35 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Error/isError` | 12 | 12 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/apply` | 48 | 48 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/bind` | 100 | 100 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/call` | 49 | 49 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Date` | 594 | 594 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Date/now` | 6 | 6 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Date/parse` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype` | 485 | 485 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/RegExp` | 1879 | 1687 | 0 | 192 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype` | 487 | 458 | 0 | 29 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/exec` | 79 | 79 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/RegExp/prototype/test` | 45 | 45 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Promise` | 652 | 652 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Promise/all` | 98 | 98 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Promise/prototype` | 124 | 124 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Promise/prototype/then` | 75 | 75 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Promise/resolve` | 30 | 30 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Map` | 204 | 204 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype` | 156 | 156 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/get` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/set` | 14 | 14 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Set` | 383 | 383 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype` | 357 | 357 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/add` | 21 | 21 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/has` | 30 | 30 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/WeakMap/prototype` | 117 | 117 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/WeakSet/prototype` | 66 | 66 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Proxy/get` | 19 | 19 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Proxy/set` | 27 | 27 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Reflect/get` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Reflect/set` | 18 | 18 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Reflect/apply` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Iterator/from` | 19 | 19 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Iterator/prototype` | 373 | 373 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/GeneratorFunction` | 23 | 23 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/GeneratorFunction/prototype` | 6 | 6 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/AsyncFunction` | 18 | 18 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/JSON/isRawJSON` | 6 | 6 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/JSON/rawJSON` | 10 | 10 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype` | 248 | 248 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/hasOwnProperty` | 63 | 63 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Object/prototype/toString` | 41 | 41 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/RangeError` | 15 | 15 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/RangeError/prototype` | 5 | 5 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/TypeError` | 15 | 15 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/NativeErrors/TypeError/prototype` | 5 | 5 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Array/fromAsync` | 95 | 95 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/DataView/prototype` | 499 | 499 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Atomics/load` | 14 | 14 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Atomics/store` | 16 | 16 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/TypedArray/prototype` | 1396 | 1396 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/StringIteratorPrototype` | 7 | 7 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/MapIteratorPrototype` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/SetIteratorPrototype` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/RegExpStringIteratorPrototype` | 17 | 17 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/constructor` | 1 | 1 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Function/prototype/toString` | 80 | 80 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Error/prototype/constructor` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Error/prototype/toString` | 17 | 17 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/match` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/replace` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/search` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/split` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Symbol/toPrimitive` | 2 | 2 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/POSITIVE_INFINITY` | 4 | 4 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Number/NEGATIVE_INFINITY` | 4 | 4 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/exp` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/log` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/sin` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/cos` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Math/tan` | 9 | 9 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/getTime` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Date/prototype/toString` | 8 | 8 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Date/UTC` | 17 | 17 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Promise/prototype/catch` | 14 | 14 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Promise/reject` | 15 | 15 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/has` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Map/prototype/delete` | 11 | 11 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Set/prototype/delete` | 20 | 20 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Proxy/has` | 26 | 26 | 0 | 0 | 0 | Official Test262 subset. |
| `built-ins/Reflect/has` | 10 | 10 | 0 | 0 | 0 | Official Test262 subset. |
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

