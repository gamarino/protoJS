# ProtoJS - Linker Issue Resolution Guide

**Date**: January 24, 2026  
**Status**: Analysis Complete - Exact Requirements Specified

---

## Summary

The 6 linker errors are due to missing or incompatible method implementations in `libproto.a`. This document specifies exactly what needs to be implemented in protoCore to resolve all linker issues.

---

## Linker Errors Analysis

### Error 1: `ProtoString::asObject()`

**Error Message**:
```
undefined reference to `proto::ProtoString::asObject(proto::ProtoContext*) const'
```

**Location**: GCBridge.cpp lines 45, 73, 244, 576

**Usage Context**:
```cpp
const proto::ProtoString* jsKey = pContext->fromUTF8String("key")->asString(pContext);
const proto::ProtoObject* obj = jsKey->asObject(pContext);  // Line 45
```

**Requirement**: 
- **Class**: `proto::ProtoString` (existing class in protoCore)
- **Method**: `asObject(proto::ProtoContext*) const`
- **Return Type**: `const proto::ProtoObject*`
- **Purpose**: Convert a ProtoString to its ProtoObject representation
- **Implementation Notes**: 
  - ProtoString likely already has internal representation as ProtoObject
  - Should return pointer to self or underlying object
  - Must be const method
  - Takes ProtoContext for consistency with protoCore API

**Expected Implementation**:
```cpp
const proto::ProtoObject* ProtoString::asObject(proto::ProtoContext* pContext) const {
    return reinterpret_cast<const proto::ProtoObject*>(this);
    // OR if ProtoString has a different memory layout:
    // return this->getInternalObject(pContext);
}
```

---

### Error 2: `ProtoObject::asSparseList()`

**Error Message**:
```
undefined reference to `proto::ProtoObject::asSparseList(proto::ProtoContext*) const'
```

**Location**: GCBridge.cpp line 558

**Usage Context**:
```cpp
const proto::ProtoObject* wrappedMappings = contextMappings->getAt(pContext, ctxHash);
const proto::ProtoSparseList* mappings = wrappedMappings->asSparseList(pContext);  // Line 558
```

**Requirement**:
- **Class**: `proto::ProtoObject` (existing class in protoCore)
- **Method**: `asSparseList(proto::ProtoContext*) const`
- **Return Type**: `const proto::ProtoSparseList*`
- **Purpose**: Safely cast/convert a ProtoObject to ProtoSparseList if it is one
- **Implementation Notes**:
  - Type-checking conversion method
  - Should return nullptr if object is not a ProtoSparseList
  - Must be const method
  - Takes ProtoContext for consistency
  - Similar to C++'s `dynamic_cast`

**Expected Implementation**:
```cpp
const proto::ProtoSparseList* ProtoObject::asSparseList(proto::ProtoContext* pContext) const {
    // Check if this object is actually a ProtoSparseList
    if (this->getType(pContext) == TYPE_SPARSE_LIST) {
        return reinterpret_cast<const proto::ProtoSparseList*>(this);
    }
    return nullptr;
}
```

---

### Error 3: `ProtoExternalPointer::getPointer()`

**Error Message**:
```
undefined reference to `proto::ProtoExternalPointer::getPointer(proto::ProtoContext*) const'
```

**Location**: GCBridge.cpp lines 176, 604

**Usage Context**:
```cpp
const proto::ProtoExternalPointer* extPtr = reinterpret_cast<const proto::ProtoExternalPointer*>(obj);
void* ptr = extPtr->getPointer(pContext);  // Line 604
```

**Requirement**:
- **Class**: `proto::ProtoExternalPointer` (existing class in protoCore)
- **Method**: `getPointer(proto::ProtoContext*) const`
- **Return Type**: `void*`
- **Purpose**: Extract the wrapped C++ pointer from a ProtoExternalPointer
- **Implementation Notes**:
  - ProtoExternalPointer is a wrapper for C++ pointers
  - Must retrieve the stored void* pointer
  - Must be const method
  - Takes ProtoContext for consistency

**Expected Implementation**:
```cpp
void* ProtoExternalPointer::getPointer(proto::ProtoContext* pContext) const {
    // Return the wrapped pointer stored in this object
    return this->_internalPointer;  // or getInternalPointer(), depending on internal structure
}
```

---

### Error 4: `ProtoContext::fromExternalPointer()`

**Error Message**:
```
undefined reference to `proto::ProtoContext::fromExternalPointer(void*)'
```

**Location**: GCBridge.cpp lines 78, 266

**Usage Context**:
```cpp
JSValue* jsValPtr = new JSValue(JS_DupValue(ctx, jsVal));
const proto::ProtoObject* jsValWrapper = pContext->fromExternalPointer(jsValPtr);  // Line 78
```

**Requirement**:
- **Class**: `proto::ProtoContext` (existing class in protoCore)
- **Method**: `fromExternalPointer(void*)`
- **Return Type**: `const proto::ProtoObject*`
- **Parameters**: 
  - `void*` - A C++ pointer to wrap
- **Purpose**: Wrap a raw C++ pointer in a ProtoExternalPointer object
- **Implementation Notes**:
  - Factory method to create ExternalPointer objects
  - Should take ownership or reference the pointer
  - Consistency with other `from*` methods in ProtoContext
  - Should be non-const (creates new object)

**Expected Implementation**:
```cpp
const proto::ProtoObject* ProtoContext::fromExternalPointer(void* ptr) {
    // Create a new ProtoExternalPointer wrapping the given pointer
    proto::ProtoExternalPointer* extPtr = new proto::ProtoExternalPointer(ptr, this);
    // Register with GC if needed
    return reinterpret_cast<const proto::ProtoObject*>(extPtr);
}
```

---

### Summary Table

| # | Class | Method | Signature | Return | Status |
|---|-------|--------|-----------|--------|--------|
| 1 | ProtoString | asObject | `(ProtoContext*) const` | `ProtoObject*` | ❌ Missing |
| 2 | ProtoObject | asSparseList | `(ProtoContext*) const` | `ProtoSparseList*` | ❌ Missing |
| 3 | ProtoExternalPointer | getPointer | `(ProtoContext*) const` | `void*` | ❌ Missing |
| 4 | ProtoContext | fromExternalPointer | `(void*)` | `ProtoObject*` | ❌ Missing |

---

## Implementation Priority

### Priority 1 (Blocking)
- ✅ **Error 4**: `ProtoContext::fromExternalPointer()` - Core factory method
- ✅ **Error 3**: `ProtoExternalPointer::getPointer()` - Accessor for wrapped pointer

### Priority 2 (High)
- ✅ **Error 1**: `ProtoString::asObject()` - String wrapper access
- ✅ **Error 2**: `ProtoObject::asSparseList()` - Type casting

---

## Implementation Strategy

### Option A: Implement All 4 Methods in protoCore
**Effort**: 2-4 hours
**Impact**: Full functionality
**Risk**: Low (pure additions, no breaking changes)

**Steps**:
1. Add `asObject()` method to ProtoString class
2. Add `asSparseList()` method to ProtoObject class
3. Add `getPointer()` method to ProtoExternalPointer class
4. Add `fromExternalPointer()` method to ProtoContext class
5. Rebuild libproto.a
6. Test with protoJS

### Option B: Workaround in ProtoJS (Fallback)
**Effort**: 4-8 hours
**Impact**: Limited functionality
**Risk**: Higher maintenance burden

**Approach**: 
- Implement type-casting logic directly in protoJS
- Use RTTI or manual type tags
- More complex code, less efficient

### Option C: Hybrid Approach
**Effort**: 1-2 hours
**Impact**: Partial functionality

**Approach**:
- Focus on Priority 1 (Error 3 & 4)
- Implement workarounds for Priority 2 (Error 1 & 2)
- Simplify GCBridge to avoid string/list conversions

---

## Detailed Implementation Guide

### For ProtoCore Maintainers

#### 1. Add to ProtoString (in protoCore headers)

```cpp
namespace proto {

class ProtoString : public ProtoObject {
    // ... existing methods ...
    
    /**
     * Convert this ProtoString to its ProtoObject representation
     * @param pContext The proto context
     * @return Pointer to this object as ProtoObject
     */
    const ProtoObject* asObject(ProtoContext* pContext) const {
        return reinterpret_cast<const ProtoObject*>(this);
    }
};

}
```

#### 2. Add to ProtoObject (in protoCore headers)

```cpp
namespace proto {

class ProtoObject {
    // ... existing methods ...
    
    /**
     * Safely convert this object to a ProtoSparseList if it is one
     * @param pContext The proto context
     * @return Pointer to ProtoSparseList if this is a sparse list, nullptr otherwise
     */
    const ProtoSparseList* asSparseList(ProtoContext* pContext) const {
        // Check type - implementation depends on your type system
        if (getType(pContext) == ProtoType::SPARSE_LIST ||
            getClassName(pContext) == "ProtoSparseList") {
            return reinterpret_cast<const ProtoSparseList*>(this);
        }
        return nullptr;
    }
};

}
```

#### 3. Add to ProtoExternalPointer (in protoCore headers)

```cpp
namespace proto {

class ProtoExternalPointer : public ProtoObject {
    // ... existing members ...
    
private:
    void* _externalPointer;
    
public:
    /**
     * Get the wrapped external C++ pointer
     * @param pContext The proto context
     * @return The wrapped void* pointer
     */
    void* getPointer(ProtoContext* pContext) const {
        return _externalPointer;
    }
};

}
```

#### 4. Add to ProtoContext (in protoCore headers)

```cpp
namespace proto {

class ProtoContext {
    // ... existing methods ...
    
    /**
     * Create a ProtoExternalPointer wrapping a C++ pointer
     * @param ptr The C++ pointer to wrap
     * @return ProtoObject representing the wrapped pointer
     */
    const ProtoObject* fromExternalPointer(void* ptr) {
        ProtoExternalPointer* extPtr = new ProtoExternalPointer(ptr, this);
        // Register for GC if needed
        return reinterpret_cast<const ProtoObject*>(extPtr);
    }
};

}
```

---

## Verification Checklist

After implementing these 4 methods in protoCore:

- [ ] Rebuild libproto.a
- [ ] Verify methods are exported in symbols
- [ ] Run: `nm libproto.a | grep -E "asObject|asSparseList|getPointer|fromExternalPointer"`
- [ ] Should show all 4 methods with proper signatures
- [ ] Rebuild protoJS
- [ ] Verify no linker errors
- [ ] Run: `./protojs test_real_deferred.js`
- [ ] Verify all 6 test cases pass

---

## Alternative: Workaround in ProtoJS

If modifying protoCore is not immediately possible, here's how to work around in protoJS:

### Workaround Strategy

Replace problematic methods with manual implementations in GCBridge.cpp:

```cpp
// Instead of: jsKey->asObject(pContext)
// Use: 
auto jsKeyAsObject = pContext->newObject(true);
jsKeyAsObject = jsKeyAsObject->setAttribute(pContext, 
    pContext->fromUTF8String("_stringValue")->asString(pContext),
    jsKey);

// Instead of: wrappedMappings->asSparseList(pContext)
// Check type manually:
if (checkIfSparseList(wrappedMappings, pContext)) {
    const proto::ProtoSparseList* mappings = 
        reinterpret_cast<const proto::ProtoSparseList*>(wrappedMappings);
}

// Instead of: pContext->fromExternalPointer(ptr)
// Use existing methods if available, or store differently
```

**Effort**: 4-8 hours
**Risk**: Higher (more complex code, less maintainable)
**Recommendation**: Only use if protoCore changes are blocked

---

## Expected Build Success

Once all 4 methods are implemented and libproto.a is rebuilt:

```bash
$ cd /home/gamarino/Documentos/proyectos/protoJS/build
$ cmake ..
$ make -j4
# ... compilation ...
# Expected: NO linker errors
# Result: ./build/protojs executable created
```

---

## Next Steps

1. **For protoCore Team**:
   - Review these 4 method requirements
   - Implement in protoCore source
   - Rebuild libproto.a
   - Test symbols are exported

2. **For ProtoJS Team** (if protoCore changes are pending):
   - Continue with workaround approach
   - Simplify GCBridge functionality
   - Focus on core deferred execution

3. **After Implementation**:
   - Rebuild protoJS binary
   - Run full test suite
   - Begin Phase 2 validation

---

## Code Review Checklist

For protoCore implementer:

- [ ] All 4 methods have proper const qualifiers
- [ ] Return types match exactly (with `const` where needed)
- [ ] ProtoContext parameter handling consistent with other methods
- [ ] Memory management correct (no leaks)
- [ ] Documentation/comments added
- [ ] No breaking changes to existing API
- [ ] Export symbols properly in libproto.a
- [ ] Unit tests added for new methods

---

**Document Prepared By**: Technical Analysis  
**Date**: January 24, 2026  
**Status**: Ready for Implementation  
**Estimated Effort**: 2-4 hours (protoCore) or 4-8 hours (protoJS workaround)
