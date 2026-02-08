# Advanced Examples

Collection of advanced examples for protoJS.

## Immutable Arrays

In protoJS, arrays are immutable by default when converted to protoCore.

```javascript
const original = [1, 2, 3];
const withFour = original.concat([4]);
console.log("Original:", original); // [1, 2, 3] - unchanged
console.log("New:", withFour);      // [1, 2, 3, 4]
```

## Concurrent Deferred

Deferred automatically executes in worker threads.

```javascript
const deferreds = [];
for (let i = 0; i < 10; i++) {
    const d = new Deferred((resolve) => {
        let sum = 0;
        for (let j = 0; j < 1000000; j++) {
            sum += j;
        }
        resolve({index: i, sum: sum});
    });
    deferreds.push(d);
}
```

## protoCore Collections

### ProtoSet

```javascript
const set = new protoCore.Set([1, 2, 3, 3, 4]);
console.log(set.size); // 4
set.add(5);
console.log(set.has(3)); // true
```

### ProtoMultiset

```javascript
const multiset = new protoCore.Multiset([1, 1, 2, 2, 2]);
console.log(multiset.count(2)); // 3
console.log(multiset.size); // 5
```

### ProtoSparseList

```javascript
const sparse = new protoCore.SparseList();
sparse.set(0, "first");
sparse.set(100, "hundredth");
console.log(sparse.get(0)); // "first"
console.log(sparse.has(50)); // false
```

### ProtoTuple

```javascript
const tuple = protoCore.Tuple([1, 2, 3]);
console.log(tuple.length); // 3
// tuple.push(4); // Error: immutable
```

## Mutability Control

```javascript
const immutable = protoCore.ImmutableObject({a: 1, b: 2});
const mutable = protoCore.MutableObject({a: 1, b: 2});
mutable.a = 3; // OK
console.log(protoCore.isImmutable(immutable)); // true
```

## I/O Operations

```javascript
const content = io.readFile("data.txt");
console.log(content);
io.writeFile("output.txt", "Hello, world!");
```

## Process Information

```javascript
console.log("Args:", process.argv);
console.log("Platform:", process.platform());
console.log("Arch:", process.arch());
console.log("CWD:", process.cwd());
```

## Phase 6: Benchmarking and test compatibility

Phase 6 provides benchmarking and Node.js test compatibility via C++ (BenchmarkRunner, NodeJSTestRunner). Scripts run with the protoJS CLI; the runner infrastructure executes them and compares with Node.js.

### Running a benchmark script

Run any JavaScript file as a benchmark (timing and output are captured by the runner):

```bash
# Run with protoJS
./protojs tests/benchmarks/minimal_test.js

# Run with Node.js (for comparison)
node tests/benchmarks/minimal_test.js
```

### Phase 6 benchmark suite

Run the dedicated Phase 6 benchmark script (version-style and report-style workloads):

```bash
./protojs tests/benchmarks/phase6_benchmark_suite.js
```

Example output: per-operation timings (version parse, array filter/sort, object iteration, string report).

### Minimal benchmark script (runner-compatible)

Scripts that print results and exit can be used as benchmark inputs:

```javascript
// my_benchmark.js
const start = Date.now();
for (let i = 0; i < 1e6; i++) { /* work */ }
console.log("Elapsed:", Date.now() - start, "ms");
```

Run: `./protojs my_benchmark.js`. The BenchmarkRunner C++ API runs such scripts and records time/memory.

### Node.js test compatibility

To compare protoJS vs Node.js on a test file, the NodeJSTestRunner (C++) runs both and compares stdout. From the project root with `protojs` and `node` on `PATH`:

- Test file example: `tests/integration/basic/hello_world.js`
- The runner executes `./protojs <file>` and `node <file>`, then compares output.

See [Phase 6 module guides](PHASE6_MODULE_GUIDES.md) for C++ API usage (runSuite, runTest, generateReport, exportToJSON/HTML).

## References

- [API Reference](API_REFERENCE.md)
- [Deferred Guide](DEFERRED_USAGE.md)
- [protoCore Module](PROTOCORE_MODULE.md)
- [Phase 6 module guides (npm, benchmarking, Node.js test)](PHASE6_MODULE_GUIDES.md)