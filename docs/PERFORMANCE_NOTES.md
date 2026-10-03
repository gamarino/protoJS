# Performance notes

Measurements of specific changes, kept so that a later reader can see what a
change cost and how that was established. Dispatch (computed goto, switch, the
`runBytecode` frame) has its own page, [PERFORMANCE_DISPATCH.md](PERFORMANCE_DISPATCH.md).

## Runs of writes published once (2026-10-03)

Every write to a mutable object publishes a new version of it into protoCore's
mutable table: a new immutable snapshot plus a path copy in the table, about 12
cells. That cost is the price of protoCore's model (lock-free sharing, a
collector without write barriers), not a defect; but a run of writes to one
object with nothing between them that can observe the object does not need a
publication per write. `o.a = x; o.b = y + 1; o.c = 0` is compiled, at load
time, into the immutable-style program it is equivalent to: compute the values
in order, derive the new version from the current one, publish it once
(`ProtoObject::setAttributes`, protoCore 2.11.0). Other threads see the whole
run or none of it.

**What is grouped** (`markPutFieldGroups`, `src/runtime/BytecodeSpecialiser.cpp`):
two or more consecutive `recv.name = value` statements on the same receiver
(`this`, an argument or a local), where every value after the first uses only
constants, argument / local / closure reads, the receiver's own fields
(`p.x += 1`), arithmetic, comparisons, `!` and `typeof`. Calls, `new`, reads of
other objects, TDZ checks on other bindings, `await`, `yield`, `with` and
every other opcode end the run, and no instruction inside a run may be a jump
target. Names the runtime gives a meaning beyond a data property are never
grouped: `length`, `prototype`, array indices and `__..__` names.

**When the runtime declines** ("Write groups", `src/runtime/ProtoInterpreter.cpp`):
at the first write of a run it checks, once, that every write would take
`OP_put_field`'s plain data path and that nothing still to be computed can run
code or throw; otherwise every write of the run takes the per-write path,
unchanged. The run is declined when the receiver is not an ordinary object (a
primitive, the global object, a Proxy, an array, a Map / Set / typed array or
any other non-default behavior, a Symbol wrapper), is non-extensible, sealed
or frozen, has an own accessor, or when a written name has a setter or getter
on the chain, a Proxy on the chain or a non-writable descriptor; when a later
statement reads a field that is not an own data property with a defined value;
and when an arithmetic operand (an argument, a local or a field read) is not a
number, a string or a boolean, since an object operand runs `valueOf` and a
BigInt or Symbol one can throw.

**Measured** (`process.memoryUsage().heapUsed` around 50,000 operations, no
collection in between; Release, protoCore 2.11.0):

| Operation | Per write | Grouped |
|-----------|----------:|--------:|
| `new P(i)`, constructor with five `this.x = ...` | 111.4 cells | 32.1 cells |
| `v.move(dx, dy)`: `this.x = this.x + dx; this.y = this.y + dy` | 23.1 | 11.0 |
| `p.x += 1; p.y += 1` | 23.1 | 11.0 |

The structure benchmarks do not change: `records` and `doctree`
(`benchmarks/structures/`, one task, `REPS=5`, three interleaved rounds) contain
no such run (they build objects as literals, which `markObjectLiterals` already
publishes once), and their single-task medians were 2161 / 2157 ms and
1608 / 1547 ms per write / grouped, within the run-to-run spread of 1.8-2.3 s
and 1.4-1.7 s.

Tests: `js/basic/put_field_groups` (31 cases, each with the result Node.js
gives, including setters, frozen and non-extensible receivers, Proxies,
`valueOf` observing the receiver and exceptions mid-run; 12 of them fail when
the runtime check is removed), `js/basic/put_field_group_cells` (the
allocation above; fails per write) and `js/deferred/put_field_group_atomicity`
(three Deferreds write their own groups on one object while two read
snapshots; per write, readers saw about 2,200 partial groups per run, grouped
none).

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
