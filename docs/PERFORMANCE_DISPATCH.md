# Interpreter dispatch: switch, the former computed goto, and the runBytecode frame

protoJS's interpreter (`src/runtime/ProtoInterpreter.cpp`, `runBytecode`)
dispatches each opcode through one `switch` over the opcode, generated from
`PROTOJS_DISPATCH_TARGETS`, on every compiler. Every handler ends with
`DISPATCH()`, a plain `goto` to that switch.

Until October 2026 the GCC build jumped through a table of label addresses
instead (GCC's computed goto), and Clang and MSVC used the switch. **The
computed goto was removed because it was incorrect, not because it was slow.**
A computed goto does not run the destructors of the scopes it leaves, and many
handlers dispatch from inside a scope that holds an RAII guard -- a
`ProtoContext::CriticalSection`, a restorer of the interpreter's thread-local
call state, a `std::string`. Every such dispatch leaked its guard. A leaked
`CriticalSection` kept the frame's critical-section depth above zero, so no
safepoint handed the frame's young generation to the collector and everything
the frame allocated stayed a root until it returned: writing one array element
a million times exhausted any heap ceiling, and `x = [i]` in a loop overshot a
19 MB ceiling to 1.3 GB. A plain `goto` leaves scopes normally. The
measurements below (made before the removal) show the switch costs GCC no
more than the label table, so the table was removed rather than kept behind an
option; defining `PROTOJS_COMPUTED_GOTO` to 1 is now a compile error.
`tests/cli/gc-stress.sh` with `tests/integration/gc/frame_garbage_is_collected.js`
(ctest `cli/gc-frame-garbage`) is the regression test.

After the removal, the same machine and method as below, GCC 13.3.0:
`loop_sum` 4.50 G cycles (±0.7 %), 13.69 G instructions; `call_fib` 4.50 G
cycles (±0.8 %), 10.58 G instructions, 5.55 M branch misses -- within the
spread of the computed-goto figures in the first row of the table below.

The review of the Windows port raised three questions: does the shared
indirect branch of the switch cost much, does MSVC inline the per-opcode
helpers (it ignored `[[gnu::always_inline]]`), and what does MSVC's large
`runBytecode` frame cost on every JavaScript call. This page records what was
measured, and what was not.

## Benchmarks

Two dispatch-bound programs in `tests/benchmarks/dispatch/`. Each checks the
value it computed and exits 1 on a wrong result, so a crash or an early stop
cannot pass for a fast run.

- `loop_sum.js` -- 20,000,000 iterations of `sum += i` on function locals:
  dispatch of arithmetic, compare and branch opcodes, no calls, no allocation.
- `call_fib.js` -- recursive `fib(30)`: 2,692,537 JavaScript calls, so call
  and return dispatch and the `runBytecode` prologue.

- `int_from_producers.js` -- 5,000,000 iterations of an integer loop whose
  bound, step and modulus come from `Math.ceil`, `Number("1")` and
  `parseInt("3")`: checks that numbers produced by built-ins take the same
  SmallInteger fast paths as literals.

## Integral numbers are SmallIntegers (2026-10-02)

Built-ins (`Math.*`, `Number(string)`, `parseFloat`, `Date`, typed-array
reads, ...) and the interpreter's double arithmetic (`6 / 2`, `0.5 + 0.5`,
`x++` on a double) used to return a boxed double even for an integral value,
so a loop bounded by `Math.ceil(n)` ran every operation on the slow path and
allocated a 64-byte cell per result. They now build every number with
`makeNumber` (`src/JSNumber.h`): an integral value up to
`Number.MAX_SAFE_INTEGER` in magnitude, other than `-0`, is a SmallInteger.
`perf stat -r 5`, same machine as below, protoCore 2.8.0:

| Benchmark | cycles before | cycles after | change |
|-----------|--------------:|-------------:|-------:|
| `int_from_producers` | 22.59 G (±1.5 %) | 2.69 G (±1.7 %) | -88 % (8.4x) |
| `loop_sum` | 4.42 G (±0.3 %) | 4.53 G (±0.4 %) | +2.5 % |
| `call_fib` | 4.63 G (±1.0 %) | 4.52 G (±0.6 %) | -2.4 % |

`loop_sum` and `call_fib` run only literal integers; their instruction counts
moved by less than 0.4 %, and the cycle differences are of the size this page
attributes to code layout.

## Linux: GCC and Clang on the same machine

AMD Ryzen 5 5500U (Zen 2), Ubuntu 24.04, GCC 13.3.0, Clang 17.0.6, protoCore
2.8.0 (installed package), Release builds made without `-j`, 2026-10-02.
`perf stat -r 3 -e cycles,instructions,branches,branch-misses`; each
configuration was measured twice for `loop_sum` and the runs agreed to within
the stated spread.

Measured before the removal, when GCC could still build either dispatch
(`-DPROTOJS_COMPUTED_GOTO=0` selected the switch):

| Build | `loop_sum` cycles | instructions | `call_fib` cycles | instructions | branch misses (`call_fib`) |
|---|---|---|---|---|---|
| GCC, computed goto (default) | 4.63 G (±0.2 %) | 12.66 G | 4.47 G (±1.8 %) | 11.14 G | 5.49 M |
| GCC, switch (`-DPROTOJS_COMPUTED_GOTO=0`) | 4.10-4.12 G (±1.4-3.3 %) | 13.46 G | 4.59 G (±1.2 %) | 11.24 G | 5.63 M |
| Clang 17, switch | 5.87-6.17 G (±0.5-2.2 %) | 14.53 G | 4.56 G (±1.2 %) | 11.44 G | 5.53 M |

What this shows, and does not:

- **The switch's shared indirect branch is not what costs on this CPU.**
  Built with the switch, GCC runs `loop_sum` in about 11 % *fewer* cycles than
  with computed goto (6 % more instructions), and `call_fib` within 3 %.
  Branch misses are about the same for all three builds: Zen 2's indirect
  predictor follows the one dispatch branch as well as the per-opcode ones.
  An older or simpler predictor may behave differently; that was not
  measured.
- **Clang is slower than GCC on the tight loop, and not because of the
  dispatch.** Same switch, same source: Clang executes 8 % more instructions
  than GCC's switch build and takes 27-35 % more cycles on `loop_sum`; on
  `call_fib` the three builds are within 3 %. This is code generation in the
  opcode handlers, not dispatch, and it is what macOS (Apple clang) runs.
- These are single-thread figures on one machine. Cross-machine timings are
  not comparable with them.

## The runBytecode frame

Stack reserved by `runBytecode`'s prologue, read from the disassembly of the
same builds (`sub ...,%rsp`):

| Build | Frame |
|---|---|
| GCC, computed goto | 7,880 bytes (a 4,096-byte probe step, then 3,784) |
| GCC, switch | 3,256 bytes |
| Clang 17, switch | 2,936 bytes |
| MSVC 19.44 (Windows) | about 46 KiB (from the Windows port; not re-measured here) |

MSVC does not share stack slots between the block-scoped locals of the
handlers, so its frame is six times GCC's. On Windows any frame larger than a
page calls `__chkstk`, which touches each page of the new frame in turn so the
guard page can grow the stack: about a dozen page touches on every JavaScript
call. The same mechanism appears in GCC's computed-goto build on Ubuntu
(`-fstack-clash-protection` probes the first page). The stack depth this
allows is covered in [INSTALLATION.md](INSTALLATION.md#windows-msvc): with the
64 MiB then reserved, 700 nested calls succeeded and 720 did not (CI runner,
2026-10-02), which puts a JavaScript call at about 90 KiB of stack in the MSVC
build. Since 2026-10-04 the reservation is 256 MiB, and running out of native
stack throws RangeError on every platform (`src/runtime/NativeStackGuard.h`)
instead of ending the process.

**Not done: shrinking the MSVC frame.** It would mean moving the handlers'
large locals out of `runBytecode` (to the heap or a per-thread scratch area)
across a function of some 10,000 lines whose locals are live across
`goto`-based control flow, exception unwinding and re-entrant calls. That
risks correctness for a cost that has not been measured on Windows, and the
`__chkstk` probing cannot be turned off safely (skipping it would let a frame
jump past the guard page). Raising the threshold with `/Gs` is unsafe for the
same reason.

## MSVC inlining

The slot and stack accessors used by every handler were declared
`[[gnu::always_inline]]`, which MSVC ignores, so MSVC was free not to inline
them. They are now `PROTOJS_ALWAYS_INLINE` -- `__forceinline` under MSVC,
`always_inline` under GCC and Clang. No Windows timing before and after this
change exists; the cross-platform CI job prints the timing of the two
benchmarks on Windows and macOS (below) from now on.

## macOS and Windows CI timing

`cross-platform.yml` times both benchmarks three times on each runner and
prints the wall-clock seconds (informational, never asserted; shared runners
are noisy, and the machines differ from the one above and from each other).

First recorded run, 2026-10-02 (cross-platform run 36971824910), wall-clock
seconds of three consecutive runs, including process start-up:

| Runner | `loop_sum` | `call_fib` |
|---|---|---|
| macOS 14, arm64, Apple clang (switch) | 2.07, 2.05, 1.95 | 1.54, 1.42, 1.34 |
| Windows Server 2022, MSVC (switch), protoCore 2.8.0 job | 3.29, 3.28, 3.28 | 2.44, 2.38, 2.39 |
| Windows Server 2022, MSVC (switch), protoCore 2.7.0 job | 1.64, 1.67, 1.62 | 1.31, 1.31, 1.28 |
| Linux reference above (GCC, computed goto), for scale only | about 1.2 | about 1.2 |

The two Windows jobs ran the same protoJS source on runners of the same image
and differ by a factor of two, which says more about the runners' hardware
than about protoJS: these figures are not a comparison between platforms, and
not between the protoCore versions. What they establish is that the switch
dispatch and MSVC's frame do not make protoJS unusable on Windows; a
comparison needs the same hardware, which CI does not provide.
