# Phase 3: GCBridge Design

**Priority:** Critical  
**Timeline:** Month 3, Week 1-2  
**Dependencies:** protoCore GC, TypeBridge

---

## Overview

GCBridge integrates QuickJS JSValue lifecycle with protoCore garbage collection, ensuring proper memory management and preventing memory leaks. It maintains bidirectional mapping between JSValues and ProtoObjects and registers JSValues as GC roots.

---

## Architecture

### Design Principles

1. **Bidirectional Mapping:** JSValue ↔ ProtoObject mapping
2. **GC Root Registration:** Active JSValues registered as GC roots
3. **Weak References:** Support for weak references
4. **Memory Leak Detection:** Tools to detect and report leaks
5. **Thread Safety:** Safe for concurrent access

### Component Structure

```
GCBridge
├── Mapping System (JSValue ↔ ProtoObject)
├── Root Registration (JSValue as GC roots)
├── Weak Reference Support
├── Memory Leak Detection
└── Memory Profiling Tools
```

---

## Implementation Details

### File Structure

```
src/
├── GCBridge.h
├── GCBridge.cpp
└── GCBridgeInternals.h (internal helpers)
```

### Core API

```cpp
class GCBridge {
public:
    // Mapping management
    static void registerMapping(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx);
    static void unregisterMapping(JSValue jsVal, JSContext* ctx);
    static const proto::ProtoObject* getProtoObject(JSValue jsVal, JSContext* ctx);
    static JSValue getJSValue(const proto::ProtoObject* protoObj, JSContext* ctx);
    
    // GC root management
    static void registerRoot(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx);
    static void unregisterRoot(JSValue jsVal, JSContext* ctx);
    
    // Weak references
    static void registerWeakRef(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx);
    static void unregisterWeakRef(JSValue jsVal, JSContext* ctx);
    
    // Memory leak detection
    static void detectLeaks(JSContext* ctx);
    static void reportLeaks(JSContext* ctx);
    static MemoryLeakReport getLeakReport(JSContext* ctx);
    
    // Memory profiling
    struct MemoryStats {
        size_t totalJSValues;
        size_t totalProtoObjects;
        size_t registeredRoots;
        size_t weakReferences;
        size_t leakedObjects;
    };
    static MemoryStats getMemoryStats(JSContext* ctx);
    
    // Cleanup
    static void cleanup(JSContext* ctx);
};
```

---

## Mapping System

### Bidirectional Mapping

**Data Structures:**
```cpp
struct MappingEntry {
    JSValue jsValue;
    const proto::ProtoObject* protoObj;
    bool isRoot;
    std::chrono::time_point<std::chrono::steady_clock> created;
};

class GCBridge {
private:
    // Per-context mappings
    static std::unordered_map<JSContext*, std::unordered_map<JSValue, MappingEntry>> jsToProtoMap;
    static std::unordered_map<JSContext*, std::unordered_map<const proto::ProtoObject*, JSValue>> protoToJSMap;
    static std::mutex mapMutex;
};
```

**Mapping Strategy:**
- Store mappings per JSContext
- Use JSValue as key (with context)
- Thread-safe access with mutex
- Automatic cleanup on context destruction

### Registration

**When to Register:**
- During TypeBridge conversion (fromJS/toJS)
- When creating module objects
- When creating Deferred objects
- When creating Buffer objects

**Implementation:**
```cpp
void GCBridge::registerMapping(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    
    MappingEntry entry;
    entry.jsValue = JS_DupValue(ctx, jsVal);
    entry.protoObj = protoObj;
    entry.isRoot = false;
    entry.created = std::chrono::steady_clock::now();
    
    jsToProtoMap[ctx][jsVal] = entry;
    protoToJSMap[ctx][protoObj] = jsVal;
    
    // Register as root if JSValue is active
    if (isActiveJSValue(jsVal, ctx)) {
        registerRoot(jsVal, protoObj, ctx);
    }
}
```

---

## GC Root Registration

### Root Identification

**Active JSValues:**
- Values in global scope
- Values in module scope
- Values referenced by active objects
- Values in call stack

**Registration:**
```cpp
void GCBridge::registerRoot(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    // Add to protoCore GC roots
    proto::ProtoSpace* space = getProtoSpace(ctx);
    space->addRoot(protoObj);
    
    // Mark as root in mapping
    auto& entry = jsToProtoMap[ctx][jsVal];
    entry.isRoot = true;
}
```

### Root Scanning Integration

**Integration with protoCore GC:**
- During STW phase, scan registered roots
- Mark all ProtoObjects referenced by JSValues
- Ensure JSValues are not collected while active

**Implementation:**
```cpp
void GCBridge::scanRoots(proto::ProtoSpace* space, JSContext* ctx) {
    std::lock_guard<std::mutex> lock(mapMutex);
    
    for (auto& [jsVal, entry] : jsToProtoMap[ctx]) {
        if (entry.isRoot && isActiveJSValue(jsVal, ctx)) {
            // Mark ProtoObject as reachable
            space->markObject(entry.protoObj);
        }
    }
}
```

---

## Weak References

### Weak Reference Support

**Use Cases:**
- WeakMap implementation
- WeakSet implementation
- Caching without preventing GC
- Observer patterns

**Implementation:**
```cpp
class WeakReference {
    JSValue jsVal;
    const proto::ProtoObject* protoObj;
    bool isAlive;
};

void GCBridge::registerWeakRef(JSValue jsVal, const proto::ProtoObject* protoObj, JSContext* ctx) {
    // Register weak reference
    // Don't prevent GC
    // Check if still alive on access
}
```

**Weak Reference Checking:**
- Check if ProtoObject still exists
- Check if JSValue still valid
- Return null/undefined if collected

---

## Memory Leak Detection

### Leak Detection Strategy

**Detection Methods:**
1. Track all registered mappings
2. Identify JSValues that are no longer reachable
3. Identify ProtoObjects that are no longer reachable
4. Report orphaned mappings

**Implementation:**
```cpp
struct MemoryLeakReport {
    std::vector<JSValue> orphanedJSValues;
    std::vector<const proto::ProtoObject*> orphanedProtoObjects;
    size_t totalLeaks;
    std::chrono::duration<double> leakAge;
};

MemoryLeakReport GCBridge::detectLeaks(JSContext* ctx) {
    MemoryLeakReport report;
    
    std::lock_guard<std::mutex> lock(mapMutex);
    
    for (auto& [jsVal, entry] : jsToProtoMap[ctx]) {
        if (!isActiveJSValue(jsVal, ctx) && entry.isRoot) {
            // Potential leak: JSValue not active but still registered as root
            report.orphanedJSValues.push_back(jsVal);
        }
    }
    
    return report;
}
```

### Leak Reporting

**Report Format:**
- List of leaked objects
- Memory size of leaks
- Age of leaks
- Stack traces (if available)

**Tools:**
- Command-line flag: `--detect-leaks`
- API: `GCBridge.detectLeaks()`
- Automatic detection in debug mode

---

## Memory Profiling

### Profiling Tools

**Metrics:**
- Total JSValues
- Total ProtoObjects
- Registered roots
- Weak references
- Memory usage
- GC frequency

**Implementation:**
```cpp
MemoryStats GCBridge::getMemoryStats(JSContext* ctx) {
    MemoryStats stats;
    
    std::lock_guard<std::mutex> lock(mapMutex);
    
    stats.totalJSValues = jsToProtoMap[ctx].size();
    stats.totalProtoObjects = protoToJSMap[ctx].size();
    
    for (auto& [jsVal, entry] : jsToProtoMap[ctx]) {
        if (entry.isRoot) {
            stats.registeredRoots++;
        }
    }
    
    // Get GC stats from protoCore
    proto::ProtoSpace* space = getProtoSpace(ctx);
    stats.gcCycles = space->getGCCycleCount();
    stats.memoryUsed = space->getMemoryUsed();
    
    return stats;
}
```

---

## Integration Points

### TypeBridge Integration

**During Conversion:**
```cpp
const proto::ProtoObject* TypeBridge::fromJS(JSContext* ctx, JSValue val, proto::ProtoContext* pContext) {
    const proto::ProtoObject* obj = /* conversion */;
    
    // Register mapping
    GCBridge::registerMapping(val, obj, ctx);
    
    return obj;
}
```

### JSContextWrapper Integration

**Context Lifecycle:**
```cpp
JSContextWrapper::JSContextWrapper(...) {
    // Initialize GCBridge for this context
    GCBridge::initialize(ctx);
}

JSContextWrapper::~JSContextWrapper() {
    // Cleanup all mappings
    GCBridge::cleanup(ctx);
}
```

### Module System Integration

**Module Objects:**
- Register module namespace objects
- Register module exports
- Cleanup on module unload

### Deferred Integration

**Deferred Objects:**
- Register Deferred JSValues
- Track ProtoObjects in deferred tasks
- Cleanup on Deferred completion

---

## Thread Safety

### Concurrent Access

**Thread Safety Requirements:**
- Multiple threads may access GCBridge
- JSContext is thread-local (QuickJS requirement)
- ProtoObjects may be shared between threads (if immutable)

**Implementation:**
- Per-context mappings (thread-local effectively)
- Mutex for shared data structures
- Atomic operations for counters
- Lock-free structures where possible

---

## Error Handling

**Error Scenarios:**
- Mapping registration failure
- Root registration failure
- Memory allocation failure
- GC callback errors

**Error Handling:**
- Log errors but continue operation
- Report errors via monitoring
- Graceful degradation

---

## Testing Strategy

### Unit Tests
- Mapping registration/unregistration
- Root registration
- Weak reference handling
- Leak detection
- Memory profiling

### Integration Tests
- Integration with TypeBridge
- Integration with module system
- Integration with Deferred
- GC cycle testing
- Memory leak scenarios

### Stress Tests
- Large number of mappings
- Rapid creation/destruction
- Concurrent access
- Memory pressure scenarios

---

## Dependencies

- **protoCore:** GC system, ProtoSpace, ProtoContext
- **QuickJS:** JSValue, JSContext
- **TypeBridge:** Conversion points
- **JSContextWrapper:** Context lifecycle

---

## Success Criteria

1. ✅ All JSValues properly registered as GC roots
2. ✅ No memory leaks in test suite
3. ✅ Weak references working correctly
4. ✅ Memory leak detection functional
5. ✅ Memory profiling accurate
6. ✅ Thread-safe operation
7. ✅ Performance impact minimal (<5%)

---

## Implementation Order

1. **Week 1:**
   - Mapping system (bidirectional)
   - Root registration
   - Basic integration with TypeBridge

2. **Week 2:**
   - Weak references
   - Memory leak detection
   - Memory profiling
   - Testing and optimization

---

## Notes

- GCBridge is critical for memory safety
- Must be implemented before production use
- Performance is important but safety is priority
- Integration with protoCore GC is key
- Thread safety is essential for multi-threaded execution
