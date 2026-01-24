# Phase 3: Testing Infrastructure Design

**Priority:** Critical  
**Timeline:** Month 6, Week 1-2  
**Dependencies:** All modules

---

## Overview

The Testing Infrastructure provides comprehensive testing capabilities for protoJS, including unit tests, integration tests, Node.js compatibility tests, performance benchmarks, and memory leak detection. Target is 80%+ code coverage.

---

## Architecture

### Test Structure

```
tests/
├── unit/              # C++ unit tests
├── integration/       # JavaScript integration tests
├── compatibility/     # Node.js compatibility tests
├── benchmarks/        # Performance benchmarks
├── memory/            # Memory leak tests
└── concurrency/       # Concurrency tests
```

---

## Unit Tests (C++)

### Framework

**Catch2 Framework:**
- Already integrated in CMakeLists.txt
- Comprehensive assertion macros
- Test fixtures and sections
- Parameterized tests

### Test Categories

#### TypeBridge Tests
- `tests/unit/TypeBridge/test_primitives.cpp`
- `tests/unit/TypeBridge/test_arrays.cpp`
- `tests/unit/TypeBridge/test_objects.cpp`
- `tests/unit/TypeBridge/test_functions.cpp`
- `tests/unit/TypeBridge/test_dates.cpp`

**Example:**
```cpp
TEST_CASE("TypeBridge: Number conversion", "[TypeBridge]") {
    JSContextWrapper wrapper;
    JSContext* ctx = wrapper.getJSContext();
    proto::ProtoContext* pContext = wrapper.getProtoContext();
    
    // Test integer conversion
    JSValue jsInt = JS_NewInt32(ctx, 42);
    const proto::ProtoObject* protoInt = TypeBridge::fromJS(ctx, jsInt, pContext);
    REQUIRE(protoInt->isInteger(pContext));
    REQUIRE(protoInt->asInteger(pContext) == 42);
    
    // Test conversion back
    JSValue jsResult = TypeBridge::toJS(ctx, protoInt, pContext);
    int32_t result;
    JS_ToInt32(ctx, &result, jsResult);
    REQUIRE(result == 42);
}
```

#### Module Tests
- `tests/unit/modules/test_fs.cpp`
- `tests/unit/modules/test_net.cpp`
- `tests/unit/modules/test_http.cpp`
- `tests/unit/modules/test_stream.cpp`
- `tests/unit/modules/test_buffer.cpp`

#### Thread Pool Tests
- `tests/unit/test_thread_pool_executor.cpp`
- `tests/unit/test_cpu_thread_pool.cpp`
- `tests/unit/test_io_thread_pool.cpp`
- `tests/unit/test_event_loop.cpp`

#### GCBridge Tests
- `tests/unit/test_gcbridge.cpp`
- Mapping registration
- Root registration
- Weak references
- Leak detection

---

## Integration Tests (JavaScript)

### Test Runner

**JavaScript Test Runner:**
```javascript
// tests/runner.js
class TestRunner {
    async runTests(testDir) {
        // Discover test files
        // Execute tests
        // Report results
    }
}
```

### Test Structure

**Integration Test Examples:**
```javascript
// tests/integration/buffer/test_creation.js
import { Buffer } from 'buffer';

test('Buffer.from(array)', () => {
    const buf = Buffer.from([1, 2, 3]);
    assert(buf.length === 3);
    assert(buf[0] === 1);
});

// tests/integration/fs/test_readfile.js
import { fs } from 'fs';

test('fs.promises.readFile', async () => {
    const content = await fs.promises.readFile('test.txt');
    assert(content.length > 0);
});
```

### Test Categories

- **Basic Operations:** Arithmetic, strings, arrays, objects
- **Modules:** fs, net, http, stream, buffer
- **Deferred:** Basic, concurrent, immutable sharing
- **Module System:** ES Modules, CommonJS, circular dependencies

---

## Node.js Compatibility Tests

### Test Suite

**Subset of Node.js Tests:**
- Core module tests
- API compatibility tests
- Behavior compatibility tests

**Test Structure:**
```
tests/compatibility/
├── fs/
├── net/
├── http/
├── stream/
└── buffer/
```

**Example:**
```javascript
// tests/compatibility/fs/test_readfile.js
// Based on Node.js test/parallel/test-fs-readfile.js
import { fs } from 'fs';
import { assert } from 'assert';

test('fs.promises.readFile - basic', async () => {
    const data = await fs.promises.readFile('test.txt');
    assert(Buffer.isBuffer(data));
});
```

---

## Performance Benchmarks

### Benchmark Framework

**Benchmark Structure:**
```javascript
// tests/benchmarks/array_operations.js
import { performance } from 'perf_hooks';

const iterations = 1000000;
const array = new Array(1000).fill(0);

const start = performance.now();
for (let i = 0; i < iterations; i++) {
    array.map(x => x * 2);
}
const end = performance.now();

console.log(`Array map: ${end - start}ms`);
```

### Benchmark Categories

- **Array Operations:** map, filter, reduce, etc.
- **String Operations:** concatenation, splitting, etc.
- **Concurrent Operations:** Deferred vs Promise
- **Memory Usage:** Structural sharing benefits
- **Module Loading:** Load time benchmarks

---

## Memory Leak Tests

### Leak Detection

**Test Structure:**
```cpp
TEST_CASE("Memory Leak: TypeBridge conversions", "[Memory]") {
    JSContextWrapper wrapper;
    
    for (int i = 0; i < 10000; i++) {
        JSValue val = JS_NewString(wrapper.getJSContext(), "test");
        const proto::ProtoObject* obj = TypeBridge::fromJS(wrapper.getJSContext(), val, wrapper.getProtoContext());
        JS_FreeValue(wrapper.getJSContext(), val);
    }
    
    // Check for leaks
    MemoryStats stats = GCBridge::getMemoryStats(wrapper.getJSContext());
    REQUIRE(stats.leakedObjects == 0);
}
```

### Leak Test Categories

- TypeBridge conversions
- Module loading
- Deferred execution
- Stream operations
- Network operations

---

## Concurrency Tests

### Test Structure

**Concurrency Test Examples:**
```cpp
TEST_CASE("Concurrency: Multiple Deferreds", "[Concurrency]") {
    JSContextWrapper wrapper;
    
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 100; i++) {
        futures.push_back(std::async([&wrapper, i]() {
            // Create and execute Deferred
        }));
    }
    
    for (auto& future : futures) {
        future.wait();
    }
    
    // Verify no race conditions
}
```

---

## Test Coverage

### Coverage Tools

**Tools:**
- gcov for C++ coverage
- lcov for coverage reports
- Custom JavaScript coverage (if needed)

**Coverage Targets:**
- 80%+ overall coverage
- 90%+ for critical components
- 70%+ for utility components

**Coverage Reports:**
- HTML reports
- JSON export
- Integration with CI/CD

---

## Continuous Integration

### CI Configuration

**GitHub Actions / CI:**
```yaml
# .github/workflows/ci.yml
name: CI
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build
        run: cmake --build build
      - name: Run Tests
        run: ctest --output-on-failure
      - name: Coverage
        run: |
          lcov --capture --directory . --output-file coverage.info
          lcov --list coverage.info
```

---

## Test Execution

### Running Tests

**C++ Tests:**
```bash
cd build
ctest --output-on-failure
```

**JavaScript Tests:**
```bash
./protojs tests/runner.js
```

**All Tests:**
```bash
./scripts/run_tests.sh
```

---

## Success Criteria

1. ✅ 80%+ code coverage
2. ✅ All unit tests passing
3. ✅ All integration tests passing
4. ✅ Node.js compatibility tests (70%+ pass)
5. ✅ Performance benchmarks established
6. ✅ Memory leak tests passing
7. ✅ Concurrency tests passing
8. ✅ CI/CD integration

---

## Implementation Order

1. **Week 1:**
   - Enhance existing test framework
   - Add unit tests for new modules
   - Add integration tests
   - Set up coverage reporting

2. **Week 2:**
   - Node.js compatibility tests
   - Performance benchmarks
   - Memory leak tests
   - Concurrency tests
   - CI/CD integration

---

## Notes

- Comprehensive testing is critical for production
- Coverage targets ensure quality
- Performance benchmarks track improvements
- Memory leak tests prevent regressions
- CI/CD ensures continuous quality
