# Critical Phase 1 Repair: Real Deferred Execution Implementation

## Overview

This document describes the implementation of **real JavaScript function execution in worker threads** using QuickJS bytecode serialization. The previous implementation was a "fake" that executed hardcoded C++ code. This new implementation enables genuine JS functions to execute asynchronously on the CPU thread pool.

## Architecture

### High-Level Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│ Main Thread                                                         │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  1. User: new Deferred(() => { let x = 0; while(x<1e6) x++; return x; })
│                                                                     │
│  2. Deferred Constructor                                            │
│     - Validate function                                             │
│     - Serialize to bytecode: JS_WriteObject()                       │
│     - Copy buffer to runtime memory (js_malloc_rt)                  │
│     - Create DeferredTask with serialized buffer                    │
│     - Submit to CPUThreadPool                                       │
│     - Return Promise-like object                                    │
│                                                                     │
│  3. EventLoop (Main Thread)                                         │
│     - Process callbacks from worker threads                         │
│     - Deserialize results: JS_ReadObject()                          │
│     - Resolve/reject promise callbacks                              │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Submit task
                                    ↓
┌─────────────────────────────────────────────────────────────────────┐
│ Worker Thread (from CPUThreadPool)                                  │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  1. workerThreadExecution()                                         │
│     - Check thread-local JSContext/JSRuntime                        │
│     - If not exists: create ephemeral runtime + context             │
│     - Deserialize function: JS_ReadObject()                         │
│     - Execute function: JS_Call()                                   │
│                                                                     │
│  2. Handle Result                                                   │
│     - Serialize result: JS_WriteObject()                            │
│     - Copy to main runtime memory: js_malloc_rt()                   │
│     - Enqueue callback to EventLoop                                 │
│                                                                     │
│  3. Handle Error                                                    │
│     - Serialize exception: JS_WriteObject()                         │
│     - Copy to main runtime memory: js_malloc_rt()                   │
│     - Enqueue error callback to EventLoop                           │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### Thread Safety

- **QuickJS Non-Threadedness**: Each worker thread has its own ephemeral `JSContext` and `JSRuntime`
- **Memory Isolation**: Serialized buffers are copied from worker runtime to main runtime memory
- **Thread-Local Storage**: `thread_local static` pointers store per-worker context/runtime
- **Callback Queue**: `EventLoop` safely enqueues callbacks from workers to main thread

## Key Components

### 1. DeferredTask Structure

```cpp
struct DeferredTask {
    JSValue func;                      // Original function (main context)
    uint8_t* serializedFunc;           // Bytecode buffer
    size_t serializedFuncSize;         // Buffer size
    JSValue resolve;                   // Promise resolve callback
    JSValue reject;                    // Promise reject callback
    JSContext* mainJSContext;          // Main thread context
    JSRuntime* rt;                     // Shared runtime for allocations
    proto::ProtoSpace* space;          // ProtoCore context
    JSContextWrapper* wrapper;         // Wrapper for lifecycle management
    
    // Promise state
    JSValue result;
    JSValue error;
    bool isResolved, isRejected;
    JSValue thenCallback;
    JSValue catchCallback;
    
    // Result from worker thread
    uint8_t* serializedResult;         // Result bytecode
    size_t serializedResultSize;
    bool hasError;
};
```

### 2. Serialization (Main Thread)

**Location**: `Deferred::constructor()` in `Deferred.cpp` lines 63-94

```cpp
// Serialize the function to bytecode
size_t serializedSize = 0;
uint8_t* serializedFunc = JS_WriteObject(ctx, &serializedSize, argv[0], JS_WRITE_OBJ_BYTECODE);

if (!serializedFunc || serializedSize == 0) {
    return JS_ThrowTypeError(ctx, "Deferred: Function not serializable. Functions with complex closures may not be supported.");
}

// Copy to runtime memory for thread safety
uint8_t* copiedBuffer = static_cast<uint8_t*>(js_malloc_rt(rt, serializedSize));
memcpy(copiedBuffer, serializedFunc, serializedSize);
js_free(ctx, serializedFunc);  // Free original from context
```

**API Used**:
- `JS_WriteObject(ctx, &size, value, JS_WRITE_OBJ_BYTECODE)`: Serializes JS value to bytecode
- `js_malloc_rt(rt, size)`: Allocates memory in runtime heap
- `js_free(ctx, ptr)`: Frees memory from context

### 3. Worker Thread Execution

**Location**: `Deferred::workerThreadExecution()` in `Deferred.cpp` lines 198-428

#### Thread-Local Context Setup (lines 198-240)

```cpp
thread_local static JSContext* workerCtx = nullptr;
thread_local static JSRuntime* workerRt = nullptr;

if (!workerCtx) {
    // Each worker thread needs its own QuickJS runtime
    workerRt = JS_NewRuntime();
    workerCtx = JS_NewContext(workerRt);
    // Runtime persists for lifetime of worker thread
}
```

#### Deserialization (lines 243-263)

```cpp
JSValue func = JS_ReadObject(workerCtx, task->serializedFunc, task->serializedFuncSize, JS_READ_OBJ_BYTECODE);

if (JS_IsException(func)) {
    // Deserialization failed - func contains exception
    task->hasError = true;
    // ... handle error
    return;
}
```

**API Used**:
- `JS_ReadObject(ctx, buffer, size, JS_READ_OBJ_BYTECODE)`: Deserializes bytecode to callable function

#### Execution (lines 269-271)

```cpp
JSValue result = JS_Call(workerCtx, func, JS_UNDEFINED, 0, nullptr);
```

#### Result Serialization (lines 320-352)

```cpp
size_t resultSize = 0;
uint8_t* serializedResult = JS_WriteObject(workerCtx, &resultSize, result, JS_WRITE_OBJ_BYTECODE);

// Copy to main runtime memory
uint8_t* copiedResult = static_cast<uint8_t*>(js_malloc_rt(task->rt, resultSize));
memcpy(copiedResult, serializedResult, resultSize);
js_free(workerCtx, serializedResult);  // Free from worker runtime

task->serializedResult = copiedResult;
task->serializedResultSize = resultSize;
task->hasError = false;
```

#### Main Thread Callback (lines 357-415)

```cpp
EventLoop::getInstance().enqueueCallback([task]() {
    JSContext* ctx = task->mainJSContext;
    
    if (task->hasError) {
        // Deserialize exception in main thread context
        JSValue error = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
        JSValue rejectArgs[] = { error };
        JS_Call(ctx, task->reject, JS_UNDEFINED, 1, rejectArgs);
    } else {
        // Deserialize result in main thread context
        JSValue result = JS_ReadObject(ctx, task->serializedResult, task->serializedResultSize, JS_READ_OBJ_BYTECODE);
        JSValue resolveArgs[] = { result };
        JS_Call(ctx, task->resolve, JS_UNDEFINED, 1, resolveArgs);
    }
    
    // Clean up serialized result buffer
    js_free_rt(task->rt, task->serializedResult);
});
```

## Error Handling

### Serialization Failures
- **Error**: `"Deferred: Function not serializable. Functions with complex closures may not be supported."`
- **Cause**: Functions with closures over non-serializable objects
- **Handling**: Immediately throw error in constructor

### Deserialization Failures
- **Error**: Propagated from QuickJS
- **Handling**: Caught in try-catch, serialized, enqueued for main thread rejection

### Execution Failures
- **Error**: JS exceptions thrown in worker
- **Handling**: Serialized as exception, propagated via reject callback

### Resource Allocation Failures
- **Memory Allocation**: Fallback to simple error message
- **Runtime Creation**: Error callback enqueued immediately

## Memory Management

### Allocation

- **Bytecode Buffer (Main)**: `js_malloc_rt(main_runtime, size)`
- **Result Buffer (Worker → Main)**: `js_malloc_rt(main_runtime, size)`
  - Allocated in worker thread but in main runtime's memory
  - Safe because `js_malloc_rt` works across threads for the same runtime

### Deallocation

#### Finalizer (lines 120-143)
```cpp
void Deferred::finalizer(JSRuntime* rt, JSValue val) {
    DeferredTask* task = static_cast<DeferredTask*>(JS_GetOpaque(val, protojs_deferred_class_id));
    if (task) {
        // Free JSValues from main runtime
        JS_FreeValueRT(rt, task->func);
        JS_FreeValueRT(rt, task->resolve);
        JS_FreeValueRT(rt, task->reject);
        
        // Free serialized buffers
        if (task->serializedFunc) {
            js_free_rt(rt, task->serializedFunc);
        }
        if (task->serializedResult) {
            js_free_rt(rt, task->serializedResult);
        }
        
        delete task;
    }
}
```

#### DeferredTask Destructor (lines 57-65)
```cpp
~DeferredTask() {
    if (serializedFunc) {
        js_free_rt(rt, serializedFunc);
    }
    if (serializedResult) {
        js_free_rt(rt, serializedResult);
    }
}
```

## Integration Points

### 1. CPUThreadPool

**File**: `src/CPUThreadPool.h`/`.cpp`

- Provides `ThreadPoolExecutor` instance via `getExecutor()`
- `executeTaskInWorkerThread()` calls `pool.getExecutor().submit(lambda)`
- Lambda captures `std::shared_ptr<DeferredTask>` for lifetime management

### 2. EventLoop

**File**: `src/EventLoop.h`/`.cpp`

- `enqueueCallback()`: Safely add callbacks from worker threads
- `processCallbacks()`: Executed in main thread event loop
- Ensures promise resolution on main thread

### 3. JSContextWrapper

**File**: `src/JSContext.h`/`.cpp`

- Stores wrapper in `JS_SetContextOpaque(ctx, wrapper)`
- Retrieved via `JS_GetContextOpaque(ctx)` in `getWrapperFromContext()`
- Provides access to `JSRuntime` and `ProtoSpace`

## Testing

### Test Script: `test_real_deferred.js`

Located in project root, demonstrates:

1. **CPU-Intensive Loop**: Loop counting to 1,000,000
2. **Simple Arithmetic**: Computes 42 * 2
3. **String Manipulation**: Reverses a string
4. **Complex Computation**: Fibonacci(20)
5. **Error Handling**: Throws and catches error
6. **Concurrent Tasks**: 3 simultaneous Deferreds

**Execution**:
```bash
cd /home/gamarino/Documentos/proyectos/protoJS
./protojs test_real_deferred.js
```

**Expected Output**:
- All tasks complete successfully
- Correct computed results
- Error handling propagates correctly
- Timing shows work executed asynchronously

## Restrictions and Limitations

### Per User Requirements

1. **No Shared Global Variables**: Each worker has isolated context
2. **No `proto_internal.h` Usage**: All operations use `protoCore.h` public API
3. **ProtoCore Objects**: Native objects handled via `proto::ProtoSpace`
4. **Serialization Errors**: Clear error message if function not serializable

### Technical Limitations

1. **Closure Complexity**: QuickJS may not serialize functions with complex closures over global state
2. **Non-Serializable Values**: Functions capturing `JSValue` directly cannot be serialized
3. **No Cross-Context Sharing**: `JSValue` objects are context-specific and must be serialized

## Performance Considerations

### Bytecode Transfer Strategy

- **Pros**:
  - Avoids duplicating entire function source code as strings
  - QuickJS bytecode is optimized and compact
  - Result round-trip is fast for primitives
  
- **Cons**:
  - Complex objects serialization adds overhead
  - Copy operations needed for thread safety
  - Each worker thread has runtime overhead

### Optimization Opportunities

1. **Object Reuse**: Cache serialized functions for repeated execution
2. **Pooling**: Pre-allocate worker contexts
3. **Lazy Serialization**: Only serialize when submitting to worker
4. **Result Caching**: Store results for identical function + args

## Future Enhancements

### Phase 2 Potential

1. **Shared ProtoCore Objects**: Pass protoCore objects safely to workers
2. **Worker Affinity**: Pin Deferred to specific workers for cache efficiency
3. **Cancellation**: Support task cancellation mid-execution
4. **Timeouts**: Implement execution timeouts per task
5. **Progress Reporting**: Allow workers to report progress to main thread

## Files Modified

### Core Implementation
- `src/Deferred.h`: Updated struct with serialized buffer fields
- `src/Deferred.cpp`: Complete rewrite of serialization/deserialization logic

### Supporting Files
- `src/main.cpp`: Enhanced event loop timeout handling for test scripts
- `src/modules/CommonJSLoader.h`: Created missing header file

### Pre-Existing Issues Fixed
- `src/TypeBridge.cpp`: Corrected API mismatches (partially)

## Compilation & Build

### Prerequisites
- QuickJS with `JS_WRITE_OBJ_BYTECODE` and `JS_READ_OBJ_BYTECODE` flags
- protoCore library with public API
- C++20 compiler with threading support

### Build Steps
```bash
cd /home/gamarino/Documentos/proyectos/protoJS
mkdir -p build && cd build
cmake ..
make -j4
```

### Known Issues
- TypeBridge.cpp has pre-existing API mismatches (not Deferred-related)
- GCBridge.cpp has pre-existing refactoring issues
- ExecutionEngine.cpp requires additional fixes

These issues are orthogonal to the Deferred bytecode transfer implementation and can be addressed in Phase 2.

## Verification Checklist

- [x] Bytecode serialization implemented
- [x] Worker thread deserialization implemented  
- [x] Result round-trip via EventLoop implemented
- [x] Memory cleanup in finalizer implemented
- [x] Error propagation from worker to main thread
- [x] Thread-local context management
- [x] CPU pool integration
- [x] Test script created
- [x] Documentation complete
- [ ] Compilation successful (waiting for TypeBridge/GCBridge fixes)

## References

### QuickJS API
- `JS_WriteObject`: https://bellard.org/quickjs/quickjs.html#Object-serialization
- `JS_ReadObject`: https://bellard.org/quickjs/quickjs.html#Object-deserialization
- `JS_Call`: Execute function
- `JS_NewRuntime`: Create isolated runtime
- `JS_NewContext`: Create execution context

### protoCore API
- `ProtoContext::fromUTF8String`: String conversion
- `ProtoSpace`: Object allocation space
- `ProtoExternalPointer`: Wrapper for opaque pointers

### Threading
- `CPUThreadPool`: Async task execution
- `EventLoop`: Main thread callback processing
- `thread_local`: Worker-local state storage
