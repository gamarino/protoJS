# Performance notes

Measurements of specific changes, kept so that a later reader can see what a
change cost and how that was established. Dispatch (computed goto, switch, the
`runBytecode` frame) has its own page, [PERFORMANCE_DISPATCH.md](PERFORMANCE_DISPATCH.md).

## Every closure records its function table (2026-10-02)

Commit d1673bbbb made `OP_fclosure` / `OP_fclosure8` stamp `__closure_module__`
on every closure, not only on closures created outside the main script, so that
a callback the main script hands to a required module runs its own body (see
CHANGELOG). The stamp is one more attribute write per closure created. This
section measures what that costs.

### Method

- Three Release builds of the same tree, configured and built the same way
  (`cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF`, built without `-j`,
  GCC 13.3.0, protoCore 2.8.0 from the installed package, checked with `ldd`):
  - **before the merge**: 53f7d3739, master before the interpreter-leftovers
    merge;
  - **parent**: fcd6b955f, the commit before d1673bbbb (isolates the stamp);
  - **after**: 89eb9cbf6, master with the merge.
- `perf stat -r 5 -e cycles,instructions` per benchmark and build, whole
  process (start-up is about 61 M instructions, under 2 % of any benchmark
  here). Each comparison was run twice, the builds interleaved; both rounds
  are given where they differ.
- AMD Ryzen 5 5500U (Zen 2), Ubuntu 24.04.
- Every benchmark prints the value it computed and checks it, so a crash or an
  early stop cannot pass for a fast run: `function_calls` checks its final
  state (1,000,001), `tree_traversal` its sum (49,136), `closure_create` its
  accumulator (130,000) and `call_fib` fib(30) (832,040). All runs printed the
  expected values.

Benchmarks:

- `tests/benchmarks/standard/function_calls.js` -- 1,000,000 calls of one
  top-level function.
- `tests/benchmarks/standard/tree_traversal.js` -- builds and sums a
  16,383-node tree five times.
- `tests/benchmarks/dispatch/closure_create.js` (added for this measurement)
  -- 20,000 iterations, each creating a function-expression closure (through a
  call to `makeAdder`) and an arrow closure, and calling each once: 40,000
  closures.
- `tests/benchmarks/dispatch/call_fib.js` -- recursive fib(30), 2,692,537 calls.

### Results

Parent (fcd6b955f) against after (89eb9cbf6), the stamp alone:

| Benchmark | instructions | cycles |
|---|---|---|
| `function_calls` | 7.09 G → 6.80 G (**-4.0 %**) | 3.44-3.51 G → 3.40-3.46 G (-1.0 to -1.5 %) |
| `tree_traversal` | 5.05-5.06 G → 4.93 G (**-2.4 %**) | 3.11-3.27 G → 3.24-3.27 G (0 to +4.3 %, within run-to-run spread) |
| `closure_create` | 5.47 G → 5.59-5.60 G (**+2.2 to +2.3 %**) | 3.49-3.60 G → 3.49-3.66 G (0 to +1.9 %) |
| `call_fib` | 11.07 G → 10.43 G (**-5.8 %**) | 4.63-4.65 G → 4.50-4.57 G (-1.2 to -3.2 %) |

Before the merge (53f7d3739) against after gives the same picture
(`function_calls` -4.0 % instructions, `tree_traversal` -3.2 %, `closure_create`
+2.5-2.7 %, `call_fib` -5.8 %), so the other commits of the merge do not change
these benchmarks measurably.

Peak resident memory of `closure_create`: 827 MB → 861 MB (+4 %, about 840
bytes more per closure).

### Reading

- **Creating a closure costs about 2.3 % more instructions** on a benchmark that
  does little else: about 3,100 instructions more per closure (+125 M over
  40,000 closures), for the one extra attribute write. Cycles moved by 0 to
  1.9 %, inside the spread between the two rounds. This is below the 3 % the
  change was allowed, so it was kept as it is; storing the table in an
  existing slot or resolving it lazily was not pursued.
- **Calling a function costs fewer instructions** (-2.4 to -5.8 %). Every call
  site resolves the callee through `resolveNestedFunction`, which first reads
  `__closure_module__` from the closure; before the change the main script's
  closures had none, so that read missed and resolution fell back to the
  running module's table. The likely explanation is that the hit is cheaper
  than the miss plus the fallback; this was not profiled further, and the
  cycle gains are smaller than the instruction gains (-1 to -3 %).
- Net: programs that call functions more often than they create them -- most
  programs -- execute fewer instructions after the change.

### Closure creation is expensive in itself

Independently of this change, creating a closure is slow and memory-hungry in
protoJS: in `closure_create`, about 137,000 instructions and roughly 20 KB of
heap per closure created and called, which the collector does not reclaim
during the run (at 1,000,000 iterations the process grew past 22 GB and was
killed). A profile is dominated by protoCore's sparse-list (attribute map)
construction and cell allocation. That is why `closure_create` runs only 20,000
iterations; it is a separate problem from the stamp measured here, and is not
addressed by it.
