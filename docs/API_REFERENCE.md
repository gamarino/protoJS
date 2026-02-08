# API Reference

Complete API reference for protoJS.

## Globals

### `console`

Global object for logging.

#### `console.log(...args)`
Prints messages to stdout.

```javascript
console.log("Hello", "world", 42);
```

#### `console.error(...args)`
Prints error messages to stderr.

```javascript
console.error("Error occurred:", error);
```

#### `console.warn(...args)`
Prints warnings to stderr.

```javascript
console.warn("Warning: deprecated API");
```

---

## Deferred

### Constructor

#### `new Deferred(executor)`

Creates a new `Deferred` that executes the `executor` in a worker thread.

**Parameters:**
- `executor`: Function `(resolve, reject) => void` containing the work to execute

**Example:**
```javascript
const deferred = new Deferred((resolve, reject) => {
    const result = heavyComputation();
    resolve(result);
});
```

**Note:** In Phase 1, the implementation is basic. Future phases will add `.then()`, `.catch()`, and automatic CPU-intensive work detection.

---

## `protoCore` Module

### Collections

#### `protoCore.Set`

Class for sets (similar to JavaScript's `Set`, but with special protoCore features).

##### Constructor

```javascript
new protoCore.Set([iterable])
```

Creates a new `ProtoSet` optionally initialized with values from `iterable`.

##### Methods

- **`add(value)`**: Adds a value to the set
- **`has(value)`**: Returns `true` if the value exists
- **`remove(value)`**: Removes a value from the set
- **`size`**: Property that returns the number of elements

**Example:**
```javascript
const set = new protoCore.Set([1, 2, 3, 3, 4]);
console.log(set.size); // 4
set.add(5);
console.log(set.has(3)); // true
```

---

#### `protoCore.Multiset`

Class for multisets (allows duplicate elements and counts occurrences).

##### Constructor

```javascript
new protoCore.Multiset([iterable])
```

##### Methods

- **`add(value)`**: Adds a value
- **`count(value)`**: Returns the number of occurrences of `value`
- **`remove(value)`**: Removes one occurrence of `value`
- **`size`**: Returns the total number of elements (including duplicates)
- **`has(value)`**: Returns `true` if the value exists

**Example:**
```javascript
const multiset = new protoCore.Multiset([1, 1, 2, 2, 2]);
console.log(multiset.count(2)); // 3
console.log(multiset.size); // 5
```

---

#### `protoCore.SparseList`

Class for sparse lists (optimized for arrays with gaps).

##### Constructor

```javascript
new protoCore.SparseList()
```

##### Methods

- **`set(index, value)`**: Sets a value at the index
- **`get(index)`**: Gets the value at the index
- **`has(index)`**: Returns `true` if the index has a value
- **`remove(index)`**: Removes the value at the index
- **`size`**: Returns the number of elements set

**Example:**
```javascript
const sparse = new protoCore.SparseList();
sparse.set(0, "first");
sparse.set(100, "hundredth");
console.log(sparse.get(0)); // "first"
console.log(sparse.has(50)); // false
```

---

#### `protoCore.Tuple`

Factory function to create immutable tuples.

##### Syntax

```javascript
protoCore.Tuple([...values])
```

Returns an immutable array (tuple).

**Example:**
```javascript
const tuple = protoCore.Tuple([1, 2, 3]);
console.log(tuple.length); // 3
// tuple.push(4); // Error: immutable
```

---

### Mutability Control

#### `protoCore.ImmutableObject(obj)`

Creates an immutable object from `obj`.

```javascript
const immutable = protoCore.ImmutableObject({a: 1, b: 2});
// immutable.a = 3; // Error or creates new object
```

#### `protoCore.MutableObject(obj)`

Creates a mutable object from `obj`.

```javascript
const mutable = protoCore.MutableObject({a: 1, b: 2});
mutable.a = 3; // OK
```

#### `protoCore.isImmutable(obj)`

Returns `true` if the object is immutable.

```javascript
const obj = {a: 1};
console.log(protoCore.isImmutable(obj)); // false

const immutable = protoCore.ImmutableObject({a: 1});
console.log(protoCore.isImmutable(immutable)); // true
```

#### `protoCore.makeImmutable(obj)`

Converts an object to immutable.

```javascript
const obj = {a: 1};
const immutable = protoCore.makeImmutable(obj);
```

#### `protoCore.makeMutable(obj)`

Converts an object to mutable.

```javascript
const immutable = protoCore.ImmutableObject({a: 1});
const mutable = protoCore.makeMutable(immutable);
```

---

## `process` Module

Global object that provides information about the current process.

### Properties

#### `process.argv`

Array of command line arguments.

```javascript
console.log(process.argv);
// ['protojs', 'script.js', 'arg1', 'arg2']
```

#### `process.env`

Object with environment variables.

```javascript
console.log(process.env.PATH);
console.log(process.env.HOME);
console.log(process.env.USER);
```

**Note:** In Phase 1, only common variables (`PATH`, `HOME`, `USER`) are exposed. Future phases will expose all environment variables.

### Methods

#### `process.cwd()`

Returns the current working directory.

```javascript
const cwd = process.cwd();
console.log(cwd); // "/home/user/project"
```

#### `process.platform()`

Returns the operating system platform.

```javascript
const platform = process.platform();
console.log(platform); // "linux", "darwin", "win32"
```

#### `process.arch()`

Returns the CPU architecture.

```javascript
const arch = process.arch();
console.log(arch); // "x64", "ia32", "arm"
```

#### `process.exit(code)`

Terminates the process with the specified exit code.

```javascript
process.exit(0); // Success
process.exit(1); // Error
```

---

## `io` Module

Module for input/output operations.

### `io.readFile(path)`

Reads a complete file synchronously.

**Parameters:**
- `path`: File path (string)

**Returns:** File content as string

**Example:**
```javascript
const content = io.readFile("data.txt");
console.log(content);
```

**Note:** In Phase 1, this operation is synchronous and blocking. Future phases will add async versions.

### `io.writeFile(path, content)`

Writes content to a file synchronously.

**Parameters:**
- `path`: File path (string)
- `content`: Content to write (string)

**Example:**
```javascript
io.writeFile("output.txt", "Hello, world!");
```

**Note:** In Phase 1, this operation is synchronous and blocking. Future phases will add async versions.

---

## Data Types

### Automatic Conversions

protoJS automatically converts between JavaScript and protoCore types:

- **Number** ↔ `proto::Number`
- **String** ↔ `proto::ProtoString`
- **Boolean** ↔ `proto::Boolean`
- **BigInt** ↔ `proto::LargeInteger`
- **Array** ↔ `proto::ProtoList` (dense) or `proto::ProtoSparseList` (sparse)
- **Object** ↔ `proto::ProtoObject`

### Immutability by Default

In protoJS, arrays and objects are immutable by default when converted to protoCore. Operations that would normally mutate an object return a new object.

```javascript
const arr1 = [1, 2, 3];
const arr2 = arr1.concat([4]); // arr1 doesn't change
console.log(arr1); // [1, 2, 3]
console.log(arr2); // [1, 2, 3, 4]
```

---

## Command Line

### Syntax

```bash
protojs [options] [script.js]
protojs [options] -e "code"
```

### Options

#### `--cpu-threads N`

Specifies the number of threads in the CPU pool.

```bash
protojs --cpu-threads 8 script.js
```

**Default:** Number of system CPUs

#### `--io-threads N`

Specifies the number of threads in the I/O pool.

```bash
protojs --io-threads 24 script.js
```

**Default:** `cpu-threads × 3.0`

#### `--io-threads-factor F`

Specifies the multiplier factor for calculating I/O threads.

```bash
protojs --io-threads-factor 4.0 script.js
```

**Default:** `3.0`

#### `-e, --eval CODE`

Evaluates JavaScript code inline.

```bash
protojs -e "console.log('Hello')"
```

---

## Phase 6 (C++ / tooling APIs)

Phase 6 adds **npm support**, **performance benchmarking**, and **Node.js test compatibility** as C++ libraries used by the runtime and tooling. These are not exposed as JavaScript APIs; they are used by the build/test/benchmark infrastructure.

| API | Purpose |
|-----|--------|
| **Semver** (`src/npm/Semver.h`) | Version parsing, comparison, range satisfaction (`satisfies`, `findHighest`), normalization. |
| **NPMRegistry** (`src/npm/NPMRegistry.h`) | Fetch package metadata, resolve version, download tarball, search packages. |
| **BenchmarkRunner** (`src/benchmarking/BenchmarkRunner.h`) | Run benchmark scripts (protoJS/Node), compare time/memory, generate text/JSON/HTML reports. |
| **NodeJSTestRunner** (`src/testing/NodeJSTestRunner.h`) | Run tests with Node.js and protoJS, compare output, generate compatibility reports and gap lists. |

**Full API details, data structures, and C++ usage:** [Phase 6 module guides](PHASE6_MODULE_GUIDES.md).

**Runnable usage examples (CLI and scripts):** [Examples – Phase 6](EXAMPLES.md#phase-6-benchmarking-and-test-compatibility).

---

## Implementation Notes

### Phase 1 (Current)

- Basic implementation of all modules
- Deferred with simplified execution
- TypeBridge with main conversions
- Basic tests

### Future Phases

- Complete Promise API for Deferred (`.then()`, `.catch()`, `.finally()`)
- Automatic CPU-intensive work detection
- Async I/O versions (`readFileAsync`, `writeFileAsync`)
- Complete environment variable support
- More Node.js modules (fs, path, http, etc.)
- Module system (CommonJS + ES Modules)

---

## Common Errors

### "Deferred is not defined"

Make sure the Deferred module is initialized. In Phase 1, it may not be available in all contexts.

### "protoCore is not defined"

The `protoCore` module must be initialized before use. Verify that `ProtoCoreModule::init()` has been called.

### Type Conversions

Some complex conversions (such as objects with functions) may not be supported in Phase 1. Check `TypeBridge.cpp` to see which conversions are implemented.

---

## References

- [Deferred Guide](DEFERRED_USAGE.md)
- [protoCore Module](PROTOCORE_MODULE.md)
- [Thread Pool Configuration](THREAD_POOLS.md)
- [Advanced Examples](EXAMPLES.md)
- [Phase 6 module guides (npm, benchmarking, Node.js test)](PHASE6_MODULE_GUIDES.md)