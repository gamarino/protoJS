# Usage Guide: Deferred

## Introduction

`Deferred` is protoJS's implementation for asynchronous execution with transparent worker threads. Similar to JavaScript's `Promise`, but with the advantage of automatically executing in worker threads from the CPU pool.

## Basic Usage

### Creating a Deferred

```javascript
const deferred = new Deferred((resolve, reject) => {
    // Code to execute
    const result = heavyComputation();
    resolve(result);
});
```

### Handling Results

```javascript
// In complete implementation, would support .then()
deferred.then(value => {
    console.log("Result:", value);
}).catch(error => {
    console.error("Error:", error);
});
```

## Features

### Automatic Execution in Worker Threads

Unlike standard `Promise` which executes on the main thread, `Deferred` automatically executes in a thread from the CPU pool.

### Sharing Immutable Objects

Immutable objects are shared between threads without copying, resulting in efficient memory usage.

### Thread Pool

The CPU pool is automatically initialized with one thread per processor core.

## Complete Example

```javascript
// Create multiple deferreds
const deferreds = [];

for (let i = 0; i < 10; i++) {
    const d = new Deferred((resolve) => {
        // CPU-intensive work
        let sum = 0;
        for (let j = 0; j < 1000000; j++) {
            sum += j;
        }
        resolve(sum);
    });
    deferreds.push(d);
}

// All execute concurrently in worker threads
```

## Configuration

The CPU pool size can be configured:

```bash
protojs --cpu-threads 8 script.js
```

## Implementation Notes

- **Option B**: The JS function executes on the main thread, but heavy work is delegated to protoCore in worker threads.
- **Phase 1**: Basic implementation. Future phases will add automatic CPU-intensive work detection.
