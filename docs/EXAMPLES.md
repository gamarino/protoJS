# Examples

Short scripts that use the globals `protojs` installs. See [API_REFERENCE.md](API_REFERENCE.md) for the full list and for known gaps.

## Parallel CPU work with `protoCore.runInThread`

Each call runs the native `cpuChunk` worker on its own protoCore thread and returns a `Deferred`.

```javascript
const TASKS = 4;
let finished = 0;
const start = Date.now();

for (let i = 0; i < TASKS; i++) {
    protoCore.runInThread('cpuChunk', [2000000]).then((sum) => {
        finished++;
        console.log("task", i, "result", sum);
        if (finished === TASKS) {
            console.log("all tasks done in", Date.now() - start, "ms");
        }
    });
}
```

`tests/benchmarks/standard/parallel_cpu.js` is a complete benchmark built on the same call.

## Deferred

The function runs on a later event-loop turn of the main thread, receives no arguments, and its return value fulfils the Deferred. See [DEFERRED_USAGE.md](DEFERRED_USAGE.md).

```javascript
const d = new Deferred(() => {
    let sum = 0;
    for (let j = 0; j < 100000; j++) {
        sum += j;
    }
    return sum;
});

d.then((value) => console.log("sum:", value));
console.log("scheduled");   // printed before "sum: ..."
```

## `setImmediate`

```javascript
setImmediate(() => console.log("second"));
console.log("first");
```

## File I/O with `io`

```javascript
if (io.writeFile("output.txt", "Hello, world!")) {
    console.log(io.readFile("output.txt"));
}

io.readFileAsync("output.txt").then((text) => {
    console.log("read asynchronously:", text);
});
```

## Process information

`process.platform` and `process.arch` are functions in protoJS.

```javascript
console.log("Args:", process.argv.length);
console.log("Platform:", process.platform());
console.log("Arch:", process.arch());
console.log("CWD:", process.cwd());
console.log("HOME:", process.env.HOME);
```

## Benchmark scripts

The benchmark scripts are plain JavaScript files that print their own timings, so they run under both `protojs` and Node.js:

```bash
./build/protojs tests/benchmarks/minimal_test.js
node tests/benchmarks/minimal_test.js

./build/protojs tests/benchmarks/phase6_benchmark_suite.js
```

The standard suite and its comparison runners are described in [tests/benchmarks/standard/README.md](../tests/benchmarks/standard/README.md):

```bash
node tests/benchmarks/run_standard_comparison.js
```

A script can report its own elapsed time in the same way:

```javascript
// my_benchmark.js
const start = Date.now();
let acc = 0;
for (let i = 0; i < 1000000; i++) {
    acc += i;
}
console.log("Elapsed:", Date.now() - start, "ms", acc);
```

```bash
./build/protojs my_benchmark.js
```

## See also

- [API Reference](API_REFERENCE.md)
- [Deferred](DEFERRED_USAGE.md)
- [The `protoCore` global](PROTOCORE_MODULE.md)
