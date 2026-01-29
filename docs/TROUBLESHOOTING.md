# Troubleshooting Guide

Solutions to common problems in protoJS.

---

## Table of Contents

1. [Build Problems](#build-problems)
2. [Runtime Errors](#runtime-errors)
3. [Deferred Problems](#deferred-problems)
4. [Module Problems](#module-problems)
5. [Performance Problems](#performance-problems)
6. [Thread Pool Problems](#thread-pool-problems)
7. [Type Conversion Problems](#type-conversion-problems)

---

## Build Problems

### Error: "protoCore shared library not found"

**Symptom:**
```
CMake Error: protoCore shared library not found. Build protoCore first: ...
```

**Solution:**
1. Build the **protoCore shared library** (official name: protoCore) in the protoCore project:
   ```bash
   cd ../protoCore
   cmake -B build -S .
   cmake --build build --target protoCore
   ```
   This produces `libprotoCore.so` (Linux), `libprotoCore.dylib` (macOS), or `protoCore.dll` (Windows) in `protoCore/build/` (or `protoCore/build_check/`).

2. Verify that protoCore is in the expected path or set `PROTOCORE_DIR` in CMake:
   ```bash
   cmake -DPROTOCORE_DIR=/path/to/protoCore ..
   ```

### Error: "QuickJS headers not found"

**Symptom:**
```
fatal error: quickjs.h: No such file or directory
```

**Solution:**
1. Verify that QuickJS is in `deps/quickjs/`
2. If not, clone QuickJS:
   ```bash
   git clone https://github.com/bellard/quickjs.git deps/quickjs
   ```

### Error: "C++20 features not supported"

**Symptom:**
```
error: 'concepts' not supported
```

**Solution:**
1. Update your compiler:
   - GCC 10+ or Clang 12+ required
2. Verify the version:
   ```bash
   g++ --version
   clang++ --version
   ```

### Linker Error: "undefined reference"

**Symptom:**
```
undefined reference to `proto::...`
```

**Solution:**
1. Verify that the protoCore shared library was built and found by CMake (see "protoCore shared library not found" above).
2. protoJS links against `${PROTOCORE_LIBRARY}` (the protoCore shared library). Ensure protoCore is built with `cmake --build build --target protoCore` before building protoJS.

---

## Runtime Errors

### "Deferred is not defined"

**Symptom:**
```javascript
ReferenceError: Deferred is not defined
```

**Solution:**
1. Verify that `Deferred::init()` has been called in `main.cpp`
2. In Phase 1, Deferred may not be fully functional
3. Verify that the module is initialized before use

### "protoCore is not defined"

**Symptom:**
```javascript
ReferenceError: protoCore is not defined
```

**Solution:**
1. Ensure that `ProtoCoreModule::init()` has been called
2. Verify in `main.cpp` that the module is initialized:
   ```cpp
   ProtoCoreModule::init(ctx);
   ```

### "process is not defined"

**Symptom:**
```javascript
ReferenceError: process is not defined
```

**Solution:**
1. Verify that `ProcessModule::init()` has been called with correct arguments
2. The process module must be initialized in `main.cpp`:
   ```cpp
   ProcessModule::init(ctx, argc, argv);
   ```

### "io is not defined"

**Symptom:**
```javascript
ReferenceError: io is not defined
```

**Solution:**
1. Verify that `IOModule::init()` has been called
2. Ensure that the module is initialized in `main.cpp`

---

## Deferred Problems

### Deferred does not execute in worker thread

**Symptom:**
Code inside Deferred executes on the main thread, blocking the application.

**Solution:**
1. In Phase 1, the implementation is basic and may not execute in worker threads
2. Verify that `CPUThreadPool` is initialized:
   ```cpp
   CPUThreadPool::initialize();
   ```
3. Check `NEXT_STEPS.md` for the current implementation status

### Deferred does not return result

**Symptom:**
There is no way to get the Deferred result.

**Solution:**
1. In Phase 1, `.then()` and `.catch()` may not be implemented
2. Complete implementation is planned for future phases
3. For now, the result is processed internally

### Multiple Deferreds do not execute in parallel

**Symptom:**
Deferreds execute sequentially instead of in parallel.

**Solution:**
1. Verify that `CPUThreadPool` has multiple threads:
   ```bash
   protojs --cpu-threads 4 script.js
   ```
2. Verify that the pool is correctly initialized
3. In Phase 1, the implementation may have limitations

---

## Module Problems

### protoCore Module: Methods not available

**Symptom:**
```javascript
TypeError: set.add is not a function
```

**Solution:**
1. Verify that the object is a correct instance:
   ```javascript
   const set = new protoCore.Set([1, 2, 3]);
   ```
2. Don't use `protoCore.Set` as a function, use `new`
3. Verify that the module is correctly initialized

### process Module: Missing environment variables

**Symptom:**
```javascript
console.log(process.env.SOME_VAR); // undefined
```

**Solution:**
1. In Phase 1, only common variables (`PATH`, `HOME`, `USER`) are exposed
2. For other variables, use `std::getenv` in C++ or wait for future phases
3. Check the implementation in `ProcessModule.cpp`

### io Module: File not found

**Symptom:**
```javascript
Error: readFile error: No such file or directory
```

**Solution:**
1. Verify that the file path is correct (relative to current directory)
2. Use absolute paths if necessary:
   ```javascript
   const content = io.readFile("/absolute/path/to/file.txt");
   ```
3. Verify file read permissions

---

## Performance Problems

### Application slow with many Deferreds

**Symptom:**
The application becomes slow when many Deferreds are created.

**Solution:**
1. Limit the number of concurrent Deferreds
2. Increase the CPU thread pool size:
   ```bash
   protojs --cpu-threads 8 script.js
   ```
3. Consider grouping work into fewer, larger Deferreds

### High memory usage

**Symptom:**
The application consumes a lot of memory.

**Solution:**
1. Verify that you're using immutability correctly (structural sharing)
2. Avoid unnecessary copies of large arrays
3. Use `ProtoSparseList` for arrays with many gaps
4. Verify that there are no memory leaks in the C++ code

### Thread pool saturated

**Symptom:**
Tasks take a long time to execute.

**Solution:**
1. Increase the thread pool size:
   ```bash
   protojs --cpu-threads 16 script.js
   ```
2. Verify that there are no blocking tasks on the main thread
3. Consider using the I/O thread pool for I/O operations

---

## Thread Pool Problems

### CPU Thread Pool does not initialize

**Symptom:**
```
Error: CPUThreadPool not initialized
```

**Solution:**
1. Verify that `CPUThreadPool::initialize()` is called in `JSContextWrapper`
2. Verify that the number of threads is > 0
3. Verify that `std::thread::hardware_concurrency()` returns a valid value

### I/O Thread Pool with too many threads

**Symptom:**
The system becomes slow with many I/O threads.

**Solution:**
1. Reduce the I/O thread factor:
   ```bash
   protojs --io-threads-factor 2.0 script.js
   ```
2. Or specify a fixed number:
   ```bash
   protojs --io-threads 8 script.js
   ```

### Threads do not close correctly

**Symptom:**
The application does not terminate or leaves zombie threads.

**Solution:**
1. Verify that `shutdown()` is called in destructors
2. Ensure that `JSContextWrapper` is correctly destroyed
3. Verify that there are no circular references preventing destruction

---

## Type Conversion Problems

### Error converting object to protoCore

**Symptom:**
```
TypeError: Cannot convert object to protoCore
```

**Solution:**
1. Verify that the object has a simple structure (without complex functions)
2. In Phase 1, some types may not be supported
3. Check `TypeBridge.cpp` to see which conversions are implemented

### Array does not convert correctly

**Symptom:**
A JavaScript array does not behave as expected after conversion.

**Solution:**
1. Verify if the array is sparse (has gaps)
2. Sparse arrays convert to `ProtoSparseList`
3. Dense arrays convert to `ProtoList` (immutable)
4. Use `protoCore.SparseList` explicitly if you need specific behavior

### BigInt does not work

**Symptom:**
```javascript
const big = BigInt(123);
// Error or unexpected behavior
```

**Solution:**
1. Verify that `TypeBridge` supports conversion from `BigInt` to `LargeInteger`
2. In Phase 1, there may be limitations
3. Check the implementation in `TypeBridge.cpp`

---

## Debugging

### Enable Detailed Logging

Add logging in C++ code to debug:

```cpp
#include <iostream>
std::cerr << "Debug: value = " << value << std::endl;
```

### Verify Thread Pool Status

In C++ code, verify the thread pool status:

```cpp
// Verify that CPUThreadPool is initialized
if (CPUThreadPool::getInstance()) {
    std::cerr << "CPUThreadPool initialized" << std::endl;
}
```

### Verify Type Conversions

Add logging in `TypeBridge.cpp` to see which conversions are being performed.

---

## Getting Help

### Check Documentation

1. [API Reference](API_REFERENCE.md)
2. [Examples](EXAMPLES.md)
3. [Architecture](../ARCHITECTURE.md)
4. [Implementation Status](../IMPLEMENTATION_STATUS.md)

### Check Implementation Status

Check `IMPLEMENTATION_STATUS.md` and `NEXT_STEPS.md` to see which features are implemented and which are pending.

### Report Problems

If you find a bug or undocumented problem:

1. Verify that you're using the latest version
2. Review compilation and execution logs
3. Document the steps to reproduce the problem
4. Include system information (OS, compiler, version)

---

## Known Issues (Phase 1)

### Current Limitations

1. **Deferred**: Basic implementation, `.then()` and `.catch()` are not fully implemented
2. **TypeBridge**: Some complex conversions may not be supported
3. **Process.env**: Only exposes common variables (`PATH`, `HOME`, `USER`)
4. **I/O**: Synchronous and blocking operations (async versions in future phases)
5. **Modules**: Complete module system not implemented (only global modules)

### Temporary Solutions

For problems related to Phase 1 limitations:

1. Check `NEXT_STEPS.md` to see the roadmap
2. Use workarounds documented in the examples
3. Consider contributing implementations for missing features

---

## References

- [API Reference](API_REFERENCE.md)
- [Examples](EXAMPLES.md)
- [Architecture](../ARCHITECTURE.md)
- [Implementation Status](../IMPLEMENTATION_STATUS.md)
- [Next Steps](../NEXT_STEPS.md)
