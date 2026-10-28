# protoJS function-call prologue (2026-10-28)

Question: the per-operation report of the same day
([../2026-10-28-per-op-overhead/README.md](../2026-10-28-per-op-overhead/README.md))
left a plain JavaScript call at about 2,200 instructions, the largest share in
`runBytecodeImpl`'s prologue and epilogue. How much of that setup exists only
for activations that are not plain function calls (scripts, evals, async
functions, generators, class constructors), and can a plain call skip it?

Base: master `a7e6ef1ef`. Branch: `perf/lean-call`. Both built Release
(`-O2 -g`) against protoCore 3.3.1 (`libprotoCore.so.4` from the local
prefix, checked with `ldd`). Machine: DEV12 (AMD Lucienne, 6 cores), desktop
load present; every measurement was serialised under the shared build lock.

## Method

As in the per-op report: a ptrace single-step tracer attributes every
user-space instruction of one call (`s = work(s)` with
`function work(v) { return (v + 1) | 0; }`, minus the empty loop) to its
function and source line. `perf stat` at N = 200,000 and 400,000 gives the
cost per iteration; every micro-benchmark prints its computed result and the
harness checks it. Instructions are reproducible to the unit; cycles are
noisy (see "Layout" below).

## What changed

The prologue now asks the compiled function, through flags that the loader
sets once per function (`ProtoBytecodeModule`), whether a step applies to it:

| step | before | now | instructions per call saved |
|---|---|---|---|
| class-constructor check at the call site (`__is_class_ctor__` attribute probe, OP_call / OP_call_method) | every call to a bytecode function | only when the callee's bytecode is a class constructor (`isClassConstructor`, from QuickJS's `is_class_constructor` bit) | ~84 (getOwnAttributeDirect 78, symbol lookup 6) |
| `std::uncaught_exceptions()` at entry, for closing iterator records on unwinding | every activation | only bodies that run `OP_for_of_start` (`opensIteratorRecords`) | ~54 (TLS lookup, `__cxa_get_globals`) |
| closure-slot pass (fill empty captured slots from the global object) | every activation with closure variables | only when the call site left a slot empty (`populateClosureCellsFromInstance` now reports it) or an arrow binds `this`; the pass itself moved out of line, unchanged | ~20 (string compare per closure variable) |
| `resizeAutomaticLocals` | called twice on a fresh entry | once, and only when the context is too small | ~5 |
| async / generator resume hand-offs | probed on every activation | only for functions that suspend (`suspends`) | a few |
| global hoisting | every activation with closure variables | only code that declares globals (`declaresGlobals`) | a few |
| null / TDZ sentinel bootstrap and built-in globals probe | every non-nested activation | skipped for calls made by a running activation on the same global root (`kEntryFromActivation`); still run by `callJSFunction` and every other entry | ~10 (the probe moved out of line) |
| module-scope split lookup (`JSContextWrapper::current`) | every activation | only the root module | ~3 |

Per call (trace): **2,210 -> 1,982 instructions (-10.3 %)**. Empty loop
iteration: 612 -> 613. `runBytecodeImpl` frame: `sub $0xcc8,%rsp` in both
(3,272 bytes); `js/workers/deep_recursion` passes.

The call sites `OP_call`, `OP_call_method`, `OP_call_constructor` and
`callJSFunction` enter through one helper, `runCallActivation`; the other
entries (eval, generator and async resume, the `valueOf`/`toString` helper)
keep the general path, which still runs every step.

## Results

Per-operation harness (`perf stat -r 5`, cost per iteration, base -> branch):

| operation | instructions | cycles |
|---|---|---|
| empty loop | 608 -> 609 | 151 -> 139 |
| `s = work(s)` (local function) | 2,817 -> 2,591 (-8.0 %) | 1,035 -> 955 |
| `s = work(s)` (global function) | 4,380 -> 4,154 (-5.2 %) | 1,673 -> 1,546 |
| closure call | 2,630 -> 2,404 (-8.6 %) | 977 -> 911 |
| arrow call | 2,748 -> 2,522 (-8.2 %) | 1,013 -> 942 |
| call of a closure with captures | 3,302 -> 2,995 (-9.3 %) | 1,218 -> 1,178 |
| method call `o.m(i)` | 3,211 -> 3,001 (-6.5 %) | 1,294 -> 1,137 |
| `s = o.b` (o global) | 1,240 -> 1,241 | 355 -> 406; recheck, two copies each: 368 / 356 -> 353 / 343 |
| `s = o.b` (o local) | 1,115 -> 1,116 | 280 -> 302; recheck (-r 10): 287-295 -> 298-312 |
| `arr.map(cb)` (1000 elements) | 45,464 -> 45,295 | 24,129 -> 23,649 |

Standard benchmarks (`tests/benchmarks/standard`, `perf stat -r 3`, median
of 4 interleaved rounds, branch / base):

| benchmark | cycles | instructions |
|---|---|---|
| function_calls | 0.948 | 0.948 |
| tree_traversal | 0.929 | 0.941 |
| string_insert_middle | 0.850 | 0.696 |
| json_transform / _small / _tiny | 0.983 / 0.972 / 0.987 | ~1.00 |
| object_property / read_only / write_only | 0.991 / 1.004 / 0.995 | ~1.00 |
| array_literal | 1.003 | 1.007 |
| control_flow | 1.014 | 1.003 |
| numeric_loop | 1.024 | 1.003 |
| string_repeated_doubling | 1.033 | 1.001 |
| others | 0.97 - 1.00 | 1.00 |

## Layout

`s = o.b` with a local `o` executes the same instructions per iteration
(1,115 / 1,116) yet measured +4 % cycles on the branch in a 10-repetition
recheck on two fresh copies of each binary (base 287-295, branch 298-312
cycles per iteration). It contains no call, so the code does not run there.
This is code placement inside `runBytecodeImpl`, of the kind recorded before
(micro-optimisations moving the dispatch loop's layout). The global-`o`
variant, which looked 14 % slower in the first run, was within noise on
recheck. The standard `object_read_only` benchmark is flat (1.004).

## Tried and dropped

- **A single "lean activation" flag** (`module->isPlain`, decided at load:
  not async, not a generator, not a class constructor, no `arguments`, no
  direct eval, no global declarations) gating every skipped step at once,
  as originally planned. Measured: the steps it gated cost about 50-70
  instructions per call in total, the decision itself about 17, and adding a
  live `lean` boolean across the 21,000-line function made the compiler spill
  `pContext` from `%r15` to the stack. That cost 22 more instructions per
  loop iteration (612 -> 635) in every function, call or no call, and left
  the call itself only 2 instructions cheaper than the variant without the
  flag. Replaced by the per-step flags above, which each test a field of the
  module the prologue already holds and keep the register allocation of
  master.
- **Probing the thread's readiness at every entry** (sentinels, globals flag)
  so `callJSFunction` could also skip them: the probe cost what it saved.
  `callJSFunction` keeps those checks.
- **Restoring the original epilogue struct shape** to recover the loop
  regression of the single-flag version: no effect (632).
- **Moving the built-in globals registration out of line** (it runs once per
  global object): no change in instructions or frame size; not kept.

## What remains in a call

About 240 instructions are still the prologue and epilogue's own lines; the
largest remaining items are the materialisation of 9 `[&]` lambdas per
activation (~45), `ModuleScope` save/restore (~13), the bytecode buffer and
size fetched through protoCore on every entry (~18 with PLT), and
`InterpFrame` push/pop. The call site (`OP_call`) costs ~180, of which 36
clear the small-buffer slot array. `ProtoContext` construction and destruction
in protoCore is ~140 and the callee lookup (`get_var`, `__bytecode_id__`,
`__closure_module__`) ~300; both are outside this change.

## Semantic notes

- A function whose bytecode is not a class constructor no longer has its
  `__is_class_ctor__` attribute probed at the call site. That attribute is an
  internal marker set only on class constructors; a program writing it onto
  an ordinary function by hand no longer makes calls to it throw.
- The debugger's activation pop is tied to whether that activation pushed one,
  instead of re-reading `debughooks::g_enabled` at exit.
- Async and generator hand-offs (`t_asyncResumeCont`, `t_asyncEntryKind`,
  `t_genResumePc`) are read only by activations of functions that suspend;
  they are always set immediately before entering such a function.

## Verification

- ctest: 200 / 200 (plus the new `js/basic/call_activation_kinds`).
- test262, the 10 subsets of the per-op report run sequentially: base and
  branch both 21,519 passed; the per-test outcome lists are identical.
- AddressSanitizer build: no AddressSanitizer report in any test. 196 / 201
  pass; the 5 failures are the same 5 tests that fail on master under ASan
  (three bounded-memory ceilings, `js/workers/deep_recursion` with ASan's
  larger frames, `cli/dap-session` timing).
- `js/workers/deep_recursion`: passes; frame size unchanged.
