# protoJS per-operation overhead (2026-10-28)

Question: how much of protoJS's cost per JavaScript operation is protoJS's
own work on top of protoCore, and how much of it can be removed without
changing protoCore? The maintainer's priority was the two largest gaps over
the protoCore floor: **property read** and **function call**.

Base: master `59f1b9e46`. Branch: `perf/per-op-overhead`. Both built Release
(`-O2 -g`) against protoCore v3.3.1 (`libprotoCore.so.4` from the local
prefix, checked with `ldd`). Machine: DEV12 (AMD Lucienne, 6 cores), desktop
load present; every comparison was run interleaved and serialised under the
shared build lock.

## Method

- **Disassembly first** (`protojs --disasm`) for every micro-benchmark, to
  know which opcodes one JavaScript operation runs.
- **Calls into protoCore** per loop iteration: a gdb Python breakpoint
  counter over libprotoCore's exported functions, between two
  `performance.now()` markers.
- **Instructions per iteration by function and source line**: a ptrace
  single-step tracer (every user-space instruction between the markers,
  symbolised with DWARF line info). This attributes the interpreter's own
  work line by line, which `perf` sampling could not do usefully inside the
  21,000-line `runBytecodeImpl`.
- **Per-operation cost**: `perf stat -r 3` (instructions, cycles) of the
  same loop at N = 200,000 and 400,000; the difference divided by 200,000 is
  the cost of one iteration (operation plus loop). Instructions are
  reproducible to the unit; cycles moved by 10-30 % between identical runs
  (A/A), so a change was kept only when instructions fell and cycles did not
  rise beyond that noise.
- **Net ns per operation**: the same body timed in-process with
  `performance.now()`, minus an empty loop of the same length, median of 5
  runs of 10^6 iterations. Node.js 22 `--jitless` on the same scripts for
  reference.

## Where the time went (base)

Calls into protoCore per loop iteration on master:

| operation | protoCore calls | where they came from |
|---|---|---|
| empty loop | 4 | the loop counter's global key rebuilt from a ProtoList (getSize/getAt/asString) and read with getAttribute |
| `s = o.b` (o global) | 43 | 3 x get_var (4 calls each), isProxy probed twice, a primitive-value chain probe, the accessor flag probed up the chain, the element list fetched twice, `propertyKeyOOP` probing `__is_symbol__` up the chain before answering for a plain string, then hasOwnAttribute + getAttribute |
| `s = work(s)` | 34 | 2 x get_var, the global write (setAttribute + allocation), isProxy, the callee's `__bytecode_id__` and `__closure_module__` through the general getAttribute, the `__protojs_globals_init__` probe on every activation, ProtoContext construction and destruction |
| `o.b = i` | 51 | as the read, plus ~10 cell allocations inside protoCore's setAttribute (the floor) |
| `a.push(i)` | 52 | ~20 cell allocations of appendLast (the floor); isProxy x 3, the `push` lookup walking 3 levels, 2 hasAttribute + 2 getAttribute in arrayPush, 2 more for the length |
| `s += 'x'` | 35 | 13 cell allocations of the rope (the floor); the BigInt operator probing `isBigInt` up the chain of both strings, repeated isString / asString |

The instruction trace showed the larger share was not protoCore at all but
the interpreter itself: an empty loop iteration (9 opcodes) cost 899
instructions, about 100 per dispatch. Each operand-stack push or pop went
through a `thread_local std::vector<InterpFrame>` wrapper and re-indexed it
(a callee could reallocate it); the closure-variable keys were rebuilt from a
ProtoList on every function entry (`jsStringFromUTF8` twice per closure
variable per call); integer comparisons and arithmetic tested for BigInt and
ToPrimitive before looking at the small integer they almost always had.

## Fixes kept

| commit | change | measured effect |
|---|---|---|
| `4d9023aa1` | closure-variable keys held in a `std::vector` built once at load, not decoded from a ProtoList per access / per call | numeric_loop -9 % instr / -11 % cycles; function_calls -6.5 % / -12 % |
| `610a6a84b` | `isProxy` answers false with no lookup until the first Proxy is created (process-wide atomic flag) | object_read_only -7 % / -8 % |
| `bdeff32e1` | `get_array_el`: own fast path for dense elements and for named keys on an object with no own accessor flag; `propertyKeyOOP` answers for strings before probing `__is_symbol__`; small-integer index decoded inline | object_read_only -36 % instr / -35 % cycles; object_write_only -20 % |
| `33089846b` | small-integer fast paths in lt/lte/gt/gte/sub/mul/and/or/xor, toBool, toInt32, toPrimitive before BigInt / ToPrimitive work | loop 899 -> 743 instr; local call 3713 -> 3341; a[i&7] 2152 -> 1778 |
| `eb7a255de` | interpreter frames in a fixed per-thread block (65,536 activations, allocated on first use, never moved); each activation keeps a pointer to its frame | loop 743 -> 608 instr, 226 -> 156 cycles; o.b 1381 -> 1191 |
| `33467a6c0` + `cce1e15ed` | the callee's identity read with `getOwnAttributeDirect` first (chain lookup kept as fallback, the earlier semantics); the globals-initialised probe cached per thread for (global object, space, GC cycle) | local call 3044 -> 2813 instr; method 3523 -> 3281 |
| `4b017bb6f` | `putFieldGroupCommit` and the frame block allocation kept out of line (see "a regression caught" below) | runBytecodeImpl frame 3656 -> 3272 bytes |
| `e6222c207` | get_field tests "length" only for the length symbol or a heap-string key; the lazy-prototype check compares only heap strings; put_var checks an own binding before the chain-walking hasAttribute | o.b 1316 -> 1240; o[k] 2089 -> 1820; global write 2313 -> 2217 |

## Result per operation

Instructions and cycles per loop iteration (operation + loop), `perf stat
-r 3`; net ns per operation (loop subtracted), median of 5; Node `--jitless`
on the same script; protoCore floor from the floor program (direct protoCore
API, same work).

| operation | instr base -> now | cycles base -> now | net ns base -> now | Node jitless | protoCore floor |
|---|---|---|---|---|---|
| empty loop iteration | 899 -> 608 (-32 %) | 270 -> 155 (-43 %) | - | - | - |
| `o.b`, o global | 1846 -> 1240 (-33 %) | 680 -> 416 (-39 %) | 117 -> 59 (-50 %) | 14 | 15 |
| `o.b`, o local | 1661 -> 1115 (-33 %) | 594 -> 291 (-51 %) | 89 -> 41 (-54 %) | 6 | 15 |
| `o[k]` | 3101 -> 1820 (-41 %) | 1237 -> 603 (-51 %) | 284 -> 126 (-56 %) | 22 | - |
| `a[i & 7]` | 3550 -> 1545 (-56 %) | 1406 -> 556 (-60 %) | 321 -> 121 (-62 %) | 13 | - |
| `work(s)`, top-level function | 3948 -> 2820 (-29 %) | 1635 -> 1038 (-37 %) | 378 -> 261 (-31 %) | 32 | 20 |
| `f(s)`, closure | 3701 -> 2633 (-29 %) | 1476 -> 967 (-34 %) | 351 -> 241 (-31 %) | 28 | 20 |
| `o.m(s)` | 4537 -> 3213 (-29 %) | 2019 -> 1183 (-41 %) | 487 -> 297 (-39 %) | 37 | - |
| `o.b = i` | 3884 -> 3224 (-17 %) | 1604 -> 1189 (-26 %) | 496 -> 432 (-13 %) | 6 | 350 |
| `a.push(i)` | 11353 -> 10448 (-8 %) | 4709 -> 4037 (-14 %) | 1990 -> 1811 (-9 %) | 52 | 800-970 |
| `s += 'x'` | 9540 -> 9180 (-4 %) | 3499 -> 3287 (-6 %) | 1521 -> 1463 (-4 %) | 58 | 850 |

Property read: the net cost halved (117 -> 59 ns global receiver, 89 -> 41
ns local). Function call: cut by about a third (378 -> 261 ns). Neither
reached the floor; see "what remains".

## Standard comparison and structure benchmarks

`perf stat -r 3`, base -> branch, same machine, interleaved:

| benchmark | cycles | instructions | time |
|---|---|---|---|
| object_read_only | 0.51 | 0.51 | 84 -> 41 ms |
| object_write_only | 0.68 | 0.71 | 1319 -> 960 ms |
| function_calls | 0.70 | 0.76 | 169 -> 129 ms |
| numeric_loop | 0.70 | 0.79 | 74 -> 53 ms |
| control_flow | 0.76 | 0.82 | 161 -> 124 ms |
| string_insert_middle | 0.77 | 0.59 | < 1 ms |
| object_property | 0.81 | 0.84 | 447 -> 388 ms |
| tree_traversal | 0.82 | 0.84 | 49 -> 38 ms |
| array_literal | 0.86 | 0.93 | 190 -> 173 ms |
| json_transform | 0.88 | 0.88 | 23 -> 20 ms |
| string_processing | 0.95 | 0.94 | 2 ms |
| string_concat | 0.96 | 0.97 | 66 ms |
| list_snapshot_history | 0.97 | 0.98 | 12 ms |

`run_standard_comparison.js` (two runs each, wall clock, noisier): geometric
mean Node.js 9.87x / 10.69x faster on master, 8.92x / 8.21x on the branch.

Structure benchmarks (`benchmarks/structures/run.py --ns 1,4`, median ms;
join and graph re-run with 5 repetitions after a 3-repetition run showed
+10 % / +12 % outliers that did not reproduce):

| bench | seq N=1 | par N=1 | seq N=4 | par N=4 |
|---|---|---|---|---|
| records | 0.98 | 1.00 | 0.90 | 0.89 |
| join | 0.88 | 0.84 | 0.96 | 0.97 |
| doctree | 0.88 | 0.69 | 0.75 | 0.84 |
| wordfreq | 0.94 | 0.91 | 0.89 | 0.94 |
| graph | 0.89 | 0.96 | 0.89 | 0.92 |

The structure benchmarks are dominated by allocation and collection, which
these changes do not touch; their gain (about 10 %) is the interpreter
share of their time.

## What remains

**Call** (`work(s)`, 2,820 instructions per iteration of which ~600 are the
loop): the trace of the branch attributes about 310 instructions to
runBytecodeImpl's prologue and epilogue (module scope, catch stack, frame
push, sentinels, async/generator checks, RAII guards), about 390 to five
attribute reads (the get_var of the callee, its identity, its module, the
global write), about 370 to the slot and operand-stack helpers, 127 to
ProtoContext construction and destruction (the protoCore floor of an
activation), 187 to the call handler itself, and the rest to dispatch
(about 15 instructions per opcode for the switch plus the debugger-hook
test). The 20 ns protoCore floor is a native call with a context; a
JavaScript call cannot reach it without removing the per-call attribute
reads, which needs the proposals below.

**Read** (`o.b`, o global): about 220 instructions are get_var reading the
global `o` (a protoCore attribute read on the global object plus TDZ and
cell checks), about 230 the get_field handler, and the rest the loop and
the `s =` store. With `o` local the read is 41 ns against Node's 6 ns.

**Writes, push, concat** are bounded by protoCore: a global or field write
is an AVL path copy (350 ns floor), push an appendLast (800-970 ns), `+=` a
rope node (850 ns). protoJS's own share of these is now 10-20 %.

## Tried and dropped

- **`std::deque` frames with a cached reference** (instead of the fixed
  block): instructions unchanged, cycles worse (loop 269 -> 348). Reverted.
- **Frame-aware operand-stack macros inside runBytecodeImpl** (passing the
  activation's frame pointer instead of re-deriving it from the context):
  instructions down 1-5 %, cycles within noise in every operation. Not kept.
- **Global variable reads with `getOwnAttributeDirect`**: broke built-in
  constructors that the global object inherits rather than owns
  (`new String("ab")`: TypeError). Reverted; global reads keep
  `getAttribute(key, false)`.
- **Computed-goto dispatch**: not attempted. The file documents why it was
  removed (a computed goto skips the destructors of the scopes it leaves,
  which leaked CriticalSections), and the switch was measured equal on GCC.

## A regression caught

The fixed frame block let GCC inline `putFieldGroupCommit` into
runBytecodeImpl; its locals grew every activation's native frame from 3,288
to 3,656 bytes, and the depth reached before the stack-guard RangeError fell
from 2,190 to 1,973, below the 2,000 `js/workers/deep_recursion` requires.
`4b017bb6f` keeps it out of line (frame 3,272 bytes, depth 2,201). Any
change that lets the compiler inline more into runBytecodeImpl should check
the frame size (`objdump -d`, the `sub $N, %rsp` in its prologue).

## Proposals (not implemented)

1. **Top-level lexical bindings in cells, not global-object attributes.**
   `let state` at top level is a protoCore attribute on the global object:
   every write is an AVL path copy (~350 ns) and every read a hashed lookup.
   A module-level cell (as closures already use) makes the read a slot load
   and the write one cell store. Estimated: global write 600 -> ~150 cycles,
   global read and the get_var of a top-level function about 100 cycles
   each, i.e. 15-25 % of a call to a top-level function. Needs care with
   `globalThis` property semantics (only `var` and function declarations
   are global-object properties per spec; `let`/`const`/`class` are not, so
   this is also closer to the spec).
2. **Static captured-slot information from the compiler.** QuickJS knows
   which locals are captured (`is_captured`). With that bit per slot the
   local opcodes (get_loc / put_loc / get_loc_check) would skip the
   cell-marker test on uncaptured slots. Estimated 20-40 instructions per
   local access, 5-10 % on loops.
3. **Per-closure call record.** The callee's bytecode module, its identity
   and its closure module are read as attributes on every call (three
   lookups). A closure created by OP_fclosure could carry one native record
   (a new protoCore object type holding a raw pointer, per the "extend by
   new type" rule) read with a single own lookup. Estimated 150-250
   instructions per call (about 10 %).
4. **A leaner activation prologue.** About 310 instructions per call go to
   setup that only async functions, generators, the debugger or the first
   activation need; splitting runBytecodeImpl's entry into a plain-function
   path and the general path would remove most of them. Estimated 10 % per
   call; the risk is duplicating the frame-setup logic.

## Verification

- `ctest`: 200/200 passed (Release, branch head).
- ASan (`-fsanitize=address`, `ASAN_OPTIONS=detect_leaks=0`): no
  AddressSanitizer report; 5 failures, all known under ASan on master
  (bounded-memory RSS ceilings x 2, deep_recursion, dap-session timing),
  a subset of the 7 recorded on 2026-10-27.
- test262 (strict, `TEST262_USE_PROTO_EVAL=1`): language/expressions,
  language/statements, language/global-code, language/eval-code,
  language/function-code, built-ins/Proxy, built-ins/Function,
  Array.prototype.push, String.prototype.at, Object.defineProperty: 23,030
  tests, 21,519 passed on both master and the branch (93.44 %), with no
  test changing outcome.
