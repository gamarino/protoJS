# Phase 8 — TypedArray + ArrayBuffer + DataView Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the full ECMAScript binary data subsystem (ArrayBuffer, 11 typed array views, DataView, iterators) in protoJS to recover ~6,500 failing Test262 tests.

**Architecture:** Each `ArrayBuffer` instance stores a `ProtoExternalBuffer` (GC-managed via Shadow GC) as attribute `__ab_data__`. Typed array instances store a reference back to their `ArrayBuffer` as `__ta_buffer__` and an integer element-type tag as `__ta_element_type__`. The interpreter intercepts `OP_get_array_el`, `OP_put_array_el`, `OP_get_field`, and `OP_put_field` to read/write raw bytes when the target object is a typed array. DataView stores its backing `ArrayBuffer`, `byteOffset`, and `byteLength` and dispatches to typed read/write helpers.

**Tech Stack:** C++20, protoCore API (`ProtoExternalBuffer`, `ProtoContext`, `ProtoObject`), QuickJS opcode dispatch (`ProtoInterpreter.cpp`), Test262 runner (`TEST262_PATTERNS=... node tests/test262/runner/test262_runner.js`).

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `src/JSSymbols.h` | Modify | Add all TypedArray/ArrayBuffer/DataView symbols |
| `src/JSSymbols.cpp` | Modify | Register new symbols with `DEFINE_SYMBOL` + `REGISTER` |
| `src/ArrayBufferPrototype.h` | Create | ArrayBuffer constructor + prototype API |
| `src/ArrayBufferPrototype.cpp` | Create | ArrayBuffer implementation |
| `src/TypedArrayPrototype.h` | Create | Element type enum + TypedArray constructor + prototype API |
| `src/TypedArrayPrototype.cpp` | Create | TypedArray implementation (~800 lines) |
| `src/DataViewPrototype.h` | Create | DataView constructor + prototype API |
| `src/DataViewPrototype.cpp` | Create | DataView implementation |
| `src/JSPrototypes.cpp` | Modify | Bootstrap ArrayBuffer + TypedArray + DataView constructors |
| `src/runtime/ProtoInterpreter.cpp` | Modify | Constructor dispatch + index proxy in OP_get/put_array_el + OP_get/put_field |
| `CMakeLists.txt` | Modify | Add 3 new source files |

---

## Task 1: Symbols + CMakeLists stub build

**Files:**
- Modify: `src/JSSymbols.h`
- Modify: `src/JSSymbols.cpp`
- Modify: `CMakeLists.txt`
- Create: `src/ArrayBufferPrototype.h`
- Create: `src/ArrayBufferPrototype.cpp`
- Create: `src/TypedArrayPrototype.h`
- Create: `src/TypedArrayPrototype.cpp`
- Create: `src/DataViewPrototype.h`
- Create: `src/DataViewPrototype.cpp`

- [ ] **Step 1: Add new symbols to `src/JSSymbols.h`**

After the existing `const proto::ProtoString* stringCtor(...)` declaration (line 97) and before the `symbolMatch` block (line 100), add:

```cpp
// ---- TypedArray / ArrayBuffer / DataView internal keys ------------------
const proto::ProtoString* abData(proto::ProtoContext* ctx);         // "__ab_data__"
const proto::ProtoString* abDetached(proto::ProtoContext* ctx);     // "__ab_detached__"
const proto::ProtoString* taBuffer(proto::ProtoContext* ctx);       // "__ta_buffer__"
const proto::ProtoString* taElementType(proto::ProtoContext* ctx);  // "__ta_element_type__"
const proto::ProtoString* taByteOffset(proto::ProtoContext* ctx);   // "__ta_byte_offset__"
const proto::ProtoString* taCtor(proto::ProtoContext* ctx);         // "__typed_array_ctor__"
const proto::ProtoString* dvBuffer(proto::ProtoContext* ctx);       // "__dv_buffer__"
const proto::ProtoString* dvByteOffset(proto::ProtoContext* ctx);   // "__dv_byte_offset__"
const proto::ProtoString* dvByteLength(proto::ProtoContext* ctx);   // "__dv_byte_length__"

// ---- TypedArray / ArrayBuffer / DataView JS property names -------------
const proto::ProtoString* ArrayBuffer(proto::ProtoContext* ctx);    // "ArrayBuffer"
const proto::ProtoString* DataView(proto::ProtoContext* ctx);       // "DataView"
const proto::ProtoString* Int8Array(proto::ProtoContext* ctx);      // "Int8Array"
const proto::ProtoString* Uint8Array(proto::ProtoContext* ctx);     // "Uint8Array"
const proto::ProtoString* Uint8ClampedArray(proto::ProtoContext* ctx); // "Uint8ClampedArray"
const proto::ProtoString* Int16Array(proto::ProtoContext* ctx);     // "Int16Array"
const proto::ProtoString* Uint16Array(proto::ProtoContext* ctx);    // "Uint16Array"
const proto::ProtoString* Int32Array(proto::ProtoContext* ctx);     // "Int32Array"
const proto::ProtoString* Uint32Array(proto::ProtoContext* ctx);    // "Uint32Array"
const proto::ProtoString* Float32Array(proto::ProtoContext* ctx);   // "Float32Array"
const proto::ProtoString* Float64Array(proto::ProtoContext* ctx);   // "Float64Array"
const proto::ProtoString* BigInt64Array(proto::ProtoContext* ctx);  // "BigInt64Array"
const proto::ProtoString* BigUint64Array(proto::ProtoContext* ctx); // "BigUint64Array"
const proto::ProtoString* byteLength(proto::ProtoContext* ctx);     // "byteLength"
const proto::ProtoString* byteOffset(proto::ProtoContext* ctx);     // "byteOffset"
const proto::ProtoString* buffer(proto::ProtoContext* ctx);         // "buffer"
const proto::ProtoString* BYTES_PER_ELEMENT(proto::ProtoContext* ctx); // "BYTES_PER_ELEMENT"
```

- [ ] **Step 2: Register new symbols in `src/JSSymbols.cpp`**

After the `DEFINE_SYMBOL(stringCtor, "__string_ctor__")` line (line 97), add:

```cpp
// ---- TypedArray / ArrayBuffer / DataView internal keys ------------------
DEFINE_SYMBOL(abData,        "__ab_data__")
DEFINE_SYMBOL(abDetached,    "__ab_detached__")
DEFINE_SYMBOL(taBuffer,      "__ta_buffer__")
DEFINE_SYMBOL(taElementType, "__ta_element_type__")
DEFINE_SYMBOL(taByteOffset,  "__ta_byte_offset__")
DEFINE_SYMBOL(taCtor,        "__typed_array_ctor__")
DEFINE_SYMBOL(dvBuffer,      "__dv_buffer__")
DEFINE_SYMBOL(dvByteOffset,  "__dv_byte_offset__")
DEFINE_SYMBOL(dvByteLength,  "__dv_byte_length__")

// ---- TypedArray / ArrayBuffer / DataView JS property names -------------
DEFINE_SYMBOL(ArrayBuffer,        "ArrayBuffer")
DEFINE_SYMBOL(DataView,           "DataView")
DEFINE_SYMBOL(Int8Array,          "Int8Array")
DEFINE_SYMBOL(Uint8Array,         "Uint8Array")
DEFINE_SYMBOL(Uint8ClampedArray,  "Uint8ClampedArray")
DEFINE_SYMBOL(Int16Array,         "Int16Array")
DEFINE_SYMBOL(Uint16Array,        "Uint16Array")
DEFINE_SYMBOL(Int32Array,         "Int32Array")
DEFINE_SYMBOL(Uint32Array,        "Uint32Array")
DEFINE_SYMBOL(Float32Array,       "Float32Array")
DEFINE_SYMBOL(Float64Array,       "Float64Array")
DEFINE_SYMBOL(BigInt64Array,      "BigInt64Array")
DEFINE_SYMBOL(BigUint64Array,     "BigUint64Array")
DEFINE_SYMBOL(byteLength,         "byteLength")
DEFINE_SYMBOL(byteOffset,         "byteOffset")
DEFINE_SYMBOL(buffer,             "buffer")
DEFINE_SYMBOL(BYTES_PER_ELEMENT,  "BYTES_PER_ELEMENT")
```

Also add all new symbols to the `REGISTER` block inside `getNameFromHash`, after the `REGISTER(stringCtor, ...)` line:

```cpp
REGISTER(abData,        "__ab_data__")
REGISTER(abDetached,    "__ab_detached__")
REGISTER(taBuffer,      "__ta_buffer__")
REGISTER(taElementType, "__ta_element_type__")
REGISTER(taByteOffset,  "__ta_byte_offset__")
REGISTER(taCtor,        "__typed_array_ctor__")
REGISTER(dvBuffer,      "__dv_buffer__")
REGISTER(dvByteOffset,  "__dv_byte_offset__")
REGISTER(dvByteLength,  "__dv_byte_length__")
REGISTER(ArrayBuffer,        "ArrayBuffer")
REGISTER(DataView,           "DataView")
REGISTER(Int8Array,          "Int8Array")
REGISTER(Uint8Array,         "Uint8Array")
REGISTER(Uint8ClampedArray,  "Uint8ClampedArray")
REGISTER(Int16Array,         "Int16Array")
REGISTER(Uint16Array,        "Uint16Array")
REGISTER(Int32Array,         "Int32Array")
REGISTER(Uint32Array,        "Uint32Array")
REGISTER(Float32Array,       "Float32Array")
REGISTER(Float64Array,       "Float64Array")
REGISTER(BigInt64Array,      "BigInt64Array")
REGISTER(BigUint64Array,     "BigUint64Array")
REGISTER(byteLength,         "byteLength")
REGISTER(byteOffset,         "byteOffset")
REGISTER(buffer,             "buffer")
REGISTER(BYTES_PER_ELEMENT,  "BYTES_PER_ELEMENT")
```

- [ ] **Step 3: Create `src/ArrayBufferPrototype.h`**

```cpp
#ifndef PROTOJS_ARRAYBUFFERPROTOTYPE_H
#define PROTOJS_ARRAYBUFFERPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Register the ArrayBuffer constructor and ArrayBuffer.prototype in the global root.
 * Idempotent — no-op when "ArrayBuffer" is already present.
 */
void ensureArrayBufferConstructor(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot);

/**
 * Create a new ArrayBuffer of the given byte length.
 * Returns a JS object with __ab_data__ = ProtoExternalBuffer.
 * Returns PROTO_NONE on failure.
 */
const proto::ProtoObject* createArrayBuffer(proto::ProtoContext* ctx,
                                            unsigned long byteLength);

/**
 * Return the raw byte pointer for a JS ArrayBuffer object, or nullptr if not an ArrayBuffer.
 */
void* getArrayBufferRawPtr(proto::ProtoContext* ctx, const proto::ProtoObject* ab);

/**
 * Return the byte length of a JS ArrayBuffer object, or 0 if not an ArrayBuffer.
 */
unsigned long getArrayBufferByteLength(proto::ProtoContext* ctx, const proto::ProtoObject* ab);

/**
 * Return true if ab is a non-detached ArrayBuffer.
 */
bool isArrayBuffer(proto::ProtoContext* ctx, const proto::ProtoObject* ab);

} // namespace protojs

#endif // PROTOJS_ARRAYBUFFERPROTOTYPE_H
```

- [ ] **Step 4: Create `src/ArrayBufferPrototype.cpp` (stub)**

```cpp
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"

namespace protojs {

// Implementation added in Task 2.

void ensureArrayBufferConstructor(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot) {}

const proto::ProtoObject* createArrayBuffer(proto::ProtoContext* ctx,
                                            unsigned long byteLength) {
    return PROTO_NONE;
}

void* getArrayBufferRawPtr(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    return nullptr;
}

unsigned long getArrayBufferByteLength(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    return 0;
}

bool isArrayBuffer(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    return false;
}

} // namespace protojs
```

- [ ] **Step 5: Create `src/TypedArrayPrototype.h`**

```cpp
#ifndef PROTOJS_TYPEDARRAYPROTOTYPE_H
#define PROTOJS_TYPEDARRAYPROTOTYPE_H

#include "headers/protoCore.h"
#include <cstdint>

namespace protojs {

/**
 * Element type tags stored as __ta_element_type__ on each typed array instance.
 * These integer values are written into ProtoInteger attributes and read back
 * in the interpreter index proxy.
 */
enum class TAElementType : uint8_t {
    Int8        = 0,
    Uint8       = 1,
    Uint8Clamped= 2,
    Int16       = 3,
    Uint16      = 4,
    Int32       = 5,
    Uint32      = 6,
    Float32     = 7,
    Float64     = 8,
    BigInt64    = 9,
    BigUint64   = 10,
};

/** Bytes per element for each TAElementType. Index = enum value. */
constexpr uint8_t TA_ELEMENT_SIZE[11] = {1, 1, 1, 2, 2, 4, 4, 4, 8, 8, 8};

/**
 * Register all 11 typed array constructors and %TypedArray%.prototype in the global root.
 * Idempotent.
 */
void ensureTypedArrayConstructors(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot);

/**
 * Read element at index from a typed array. Returns PROTO_NONE if out-of-bounds.
 * Called by the interpreter index proxy for OP_get_array_el / OP_get_field.
 */
const proto::ProtoObject* typedArrayGetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               uint8_t elementType);

/**
 * Write value at index in a typed array. Coerces value to element type.
 * Out-of-bounds writes are silently ignored. Returns the updated ta object.
 * Called by the interpreter index proxy for OP_put_array_el / OP_put_field.
 */
const proto::ProtoObject* typedArraySetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               const proto::ProtoObject* value,
                                               uint8_t elementType);

/**
 * Return true if obj is a TypedArray instance (has __ta_element_type__).
 */
bool isTypedArray(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/**
 * Return the element type tag of a TypedArray, or 0xFF if not a TypedArray.
 */
uint8_t getTypedArrayElementType(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/**
 * Return the element count (length) of a TypedArray.
 */
uint32_t getTypedArrayLength(proto::ProtoContext* ctx, const proto::ProtoObject* ta);

} // namespace protojs

#endif // PROTOJS_TYPEDARRAYPROTOTYPE_H
```

- [ ] **Step 6: Create `src/TypedArrayPrototype.cpp` (stub)**

```cpp
#include "TypedArrayPrototype.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"

namespace protojs {

// Implementations added in Tasks 3–8.

void ensureTypedArrayConstructors(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot) {}

const proto::ProtoObject* typedArrayGetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               uint8_t elementType) {
    return PROTO_NONE;
}

const proto::ProtoObject* typedArraySetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               const proto::ProtoObject* value,
                                               uint8_t elementType) {
    return const_cast<proto::ProtoObject*>(ta);
}

bool isTypedArray(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    const proto::ProtoObject* tag =
        obj->getAttribute(ctx, JSSymbols::taElementType(ctx), false);
    return tag && tag != PROTO_NONE && tag->isInteger(ctx);
}

uint8_t getTypedArrayElementType(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return 0xFF;
    const proto::ProtoObject* tag =
        obj->getAttribute(ctx, JSSymbols::taElementType(ctx), false);
    if (!tag || tag == PROTO_NONE || !tag->isInteger(ctx)) return 0xFF;
    return static_cast<uint8_t>(tag->asLong(ctx));
}

uint32_t getTypedArrayLength(proto::ProtoContext* ctx, const proto::ProtoObject* ta) {
    if (!ta || ta == PROTO_NONE) return 0;
    const proto::ProtoObject* lenObj =
        ta->getAttribute(ctx, JSSymbols::length(ctx), false);
    if (!lenObj || lenObj == PROTO_NONE || !lenObj->isInteger(ctx)) return 0;
    long long v = lenObj->asLong(ctx);
    return v > 0 ? static_cast<uint32_t>(v) : 0;
}

} // namespace protojs
```

- [ ] **Step 7: Create `src/DataViewPrototype.h`**

```cpp
#ifndef PROTOJS_DATAVIEWPROTOTYPE_H
#define PROTOJS_DATAVIEWPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Register the DataView constructor and DataView.prototype in the global root.
 * Idempotent.
 */
void ensureDataViewConstructor(proto::ProtoContext* ctx,
                               const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_DATAVIEWPROTOTYPE_H
```

- [ ] **Step 8: Create `src/DataViewPrototype.cpp` (stub)**

```cpp
#include "DataViewPrototype.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"

namespace protojs {

// Implementation added in Task 9.

void ensureDataViewConstructor(proto::ProtoContext* ctx,
                               const proto::ProtoObject** globalRoot) {}

} // namespace protojs
```

- [ ] **Step 9: Add new source files to `CMakeLists.txt`**

After line `src/ArrayPrototype.cpp` (line 116 in CMakeLists.txt), add:

```cmake
    src/ArrayBufferPrototype.cpp
    src/TypedArrayPrototype.cpp
    src/DataViewPrototype.cpp
```

- [ ] **Step 10: Verify the build compiles**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
cmake -B build -S . && cmake --build build --target protojs 2>&1 | tail -20
```
Expected: build succeeds with no errors.

- [ ] **Step 11: Commit**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
git add src/JSSymbols.h src/JSSymbols.cpp \
        src/ArrayBufferPrototype.h src/ArrayBufferPrototype.cpp \
        src/TypedArrayPrototype.h src/TypedArrayPrototype.cpp \
        src/DataViewPrototype.h src/DataViewPrototype.cpp \
        CMakeLists.txt
git commit -m "feat(typed-array): add symbols, header files, and stub implementations"
```

---

## Task 2: ArrayBuffer core — constructor, byteLength, slice, isView

**Files:**
- Modify: `src/ArrayBufferPrototype.cpp` (replace stubs)
- Modify: `src/JSPrototypes.cpp` (call `ensureArrayBufferConstructor`)
- Modify: `src/runtime/ProtoInterpreter.cpp` (dispatch `new ArrayBuffer(n)`)

- [ ] **Step 1: Run baseline ArrayBuffer test262 to see current failures**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS="built-ins/ArrayBuffer" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -5
```
Note the current pass count.

- [ ] **Step 2: Implement `createArrayBuffer` and helper functions in `src/ArrayBufferPrototype.cpp`**

Replace the file content with:

```cpp
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"
#include <cstring>
#include <algorithm>

namespace protojs {

// ---- Module-level state ---------------------------------------------------
static const proto::ProtoObject* s_abProto = nullptr;  // ArrayBuffer.prototype

// ---- Helper: get raw pointer from AB instance ----------------------------
void* getArrayBufferRawPtr(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    if (!ab || ab == PROTO_NONE) return nullptr;
    const proto::ProtoObject* dataObj =
        ab->getAttribute(ctx, JSSymbols::abData(ctx), false);
    if (!dataObj || dataObj == PROTO_NONE) return nullptr;
    return dataObj->getRawPointerIfExternalBuffer(ctx);
}

unsigned long getArrayBufferByteLength(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    if (!ab || ab == PROTO_NONE) return 0;
    const proto::ProtoObject* dataObj =
        ab->getAttribute(ctx, JSSymbols::abData(ctx), false);
    if (!dataObj || dataObj == PROTO_NONE) return 0;
    const proto::ProtoExternalBuffer* eb = dataObj->asExternalBuffer(ctx);
    return eb ? eb->getSize(ctx) : 0;
}

bool isArrayBuffer(proto::ProtoContext* ctx, const proto::ProtoObject* ab) {
    if (!ab || ab == PROTO_NONE) return false;
    const proto::ProtoObject* det =
        ab->getAttribute(ctx, JSSymbols::abDetached(ctx), false);
    if (det && det == PROTO_TRUE) return false;  // detached
    const proto::ProtoObject* dataObj =
        ab->getAttribute(ctx, JSSymbols::abData(ctx), false);
    return dataObj && dataObj != PROTO_NONE &&
           dataObj->getRawPointerIfExternalBuffer(ctx) != nullptr;
}

const proto::ProtoObject* createArrayBuffer(proto::ProtoContext* ctx,
                                            unsigned long byteLength) {
    const proto::ProtoObject* proto = s_abProto;
    const proto::ProtoObject* ab = proto ? proto->newChild(ctx, true)
                                         : ctx->newObject(true);
    if (!ab) return PROTO_NONE;

    const proto::ProtoObject* bufObj = ctx->newExternalBuffer(byteLength);
    if (!bufObj) return PROTO_NONE;

    // Zero the buffer.
    void* raw = bufObj->getRawPointerIfExternalBuffer(ctx);
    if (raw) memset(raw, 0, byteLength);

    ab = ab->setAttribute(ctx, JSSymbols::abData(ctx), bufObj);
    return ab;
}

// ---- ArrayBuffer.prototype methods ---------------------------------------

static const proto::ProtoObject* ab_get_byteLength(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    unsigned long len = getArrayBufferByteLength(ctx, self);
    return ctx->fromInteger(static_cast<long long>(len));
}

static const proto::ProtoObject* ab_slice(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    unsigned long srcLen = getArrayBufferByteLength(ctx, self);
    void* srcRaw = getArrayBufferRawPtr(ctx, self);
    if (!srcRaw) return createArrayBuffer(ctx, 0);

    long long begin = 0, end = static_cast<long long>(srcLen);
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0 != PROTO_NONE && a0->isInteger(ctx))
            begin = a0->asLong(ctx);
        else if (a0 && a0->isDouble(ctx))
            begin = static_cast<long long>(a0->asDouble(ctx));
    }
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx))
            end = a1->asLong(ctx);
        else if (a1 && a1->isDouble(ctx))
            end = static_cast<long long>(a1->asDouble(ctx));
    }

    long long sLen = static_cast<long long>(srcLen);
    if (begin < 0) begin = std::max(sLen + begin, 0LL);
    else begin = std::min(begin, sLen);
    if (end < 0) end = std::max(sLen + end, 0LL);
    else end = std::min(end, sLen);

    long long newLen = std::max(end - begin, 0LL);
    const proto::ProtoObject* result = createArrayBuffer(ctx, static_cast<unsigned long>(newLen));
    if (newLen > 0 && result && result != PROTO_NONE) {
        void* dstRaw = getArrayBufferRawPtr(ctx, result);
        if (dstRaw) memcpy(dstRaw, static_cast<char*>(srcRaw) + begin, static_cast<size_t>(newLen));
    }
    return result;
}

// ArrayBuffer.isView(arg) — static method: returns true if arg is a TypedArray or DataView.
// Implemented in JSPrototypes.cpp bootstrap (needs TypedArray symbols).
// Forward-declared here for use in ensureArrayBufferConstructor.
static const proto::ProtoObject* ab_isView(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* arg = args->getAt(ctx, 0);
    if (!arg || arg == PROTO_NONE) return PROTO_FALSE;
    // A TypedArray has __ta_element_type__; a DataView has __dv_buffer__.
    const proto::ProtoObject* taTag =
        arg->getAttribute(ctx, JSSymbols::taElementType(ctx), false);
    if (taTag && taTag != PROTO_NONE) return PROTO_TRUE;
    const proto::ProtoObject* dvBuf =
        arg->getAttribute(ctx, JSSymbols::dvBuffer(ctx), false);
    if (dvBuf && dvBuf != PROTO_NONE) return PROTO_TRUE;
    return PROTO_FALSE;
}

// ---- Bootstrap -----------------------------------------------------------

void ensureArrayBufferConstructor(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot) return;
    const proto::ProtoObject* root = *globalRoot;
    if (!root) return;

    // Idempotency check.
    const proto::ProtoObject* existing =
        root->getAttribute(ctx, JSSymbols::ArrayBuffer(ctx), true);
    if (existing && existing != PROTO_NONE) return;

    // Build ArrayBuffer.prototype.
    const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objProto ? objProto->newChild(ctx, false)
                                               : ctx->newObject(false);

    proto = proto->setAttribute(ctx, JSSymbols::byteLength(ctx),
                                ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), ab_get_byteLength));
    proto = proto->setAttribute(ctx, JSSymbols::slice(ctx),
                                ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), ab_slice));

    s_abProto = proto;

    // Build the ArrayBuffer constructor (a plain object with __ab_ctor__ marker).
    const proto::ProtoObject* ctor = ctx->newObject(false);
    ctor = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), proto);
    // Mark so OP_call_constructor can dispatch to createArrayBuffer.
    ctor = ctor->setAttribute(ctx, JSSymbols::taCtor(ctx),
                               ctx->fromUTF8String("ArrayBuffer"));
    // Static method: ArrayBuffer.isView(arg)
    ctor = ctor->setAttribute(ctx, JSSymbols::isView(ctx),
                               ctx->fromMethod(const_cast<proto::ProtoObject*>(ctor), ab_isView));

    root = root->setAttribute(ctx, JSSymbols::ArrayBuffer(ctx), ctor);
    *globalRoot = root;
}

} // namespace protojs
```

**Note:** `JSSymbols::slice` and `JSSymbols::isView` need to be added to JSSymbols. Add to `src/JSSymbols.h` (in the common JS property names section):
```cpp
const proto::ProtoString* slice(proto::ProtoContext* ctx);          // "slice"
const proto::ProtoString* isView(proto::ProtoContext* ctx);         // "isView"
```
And to `src/JSSymbols.cpp`:
```cpp
DEFINE_SYMBOL(slice,   "slice")
DEFINE_SYMBOL(isView,  "isView")
```
And register both in `getNameFromHash`.

- [ ] **Step 3: Call `ensureArrayBufferConstructor` from `src/JSPrototypes.cpp`**

Add `#include "ArrayBufferPrototype.h"` after the existing includes. Then in `BootstrapJSPrototypes`, after `BuildRegExpPrototype(...)`:

```cpp
    // ArrayBuffer is initialized lazily via ensureArrayBufferConstructor when first referenced.
    // We call it here so it is available from the start.
    // Note: BootstrapJSPrototypes does not have a globalRoot pointer — ArrayBuffer bootstrap
    // is triggered from ProtoInterpreter at first use via ensureArrayBufferConstructor call
    // in the OP_call_constructor ArrayBuffer path.
```

Actually, ArrayBuffer needs to be registered in the global root. Look at how `ensureArrayPrototype` is called — it takes `globalRoot` as a `const proto::ProtoObject**`. In `BootstrapJSPrototypes` we don't have `globalRoot`. Instead, add the call in `ExecutionEngine.cpp` or wherever `ensureArrayPrototype` is called.

Search for `ensureArrayPrototype` in the codebase:
```bash
grep -rn "ensureArrayPrototype\|ensureRegExpConstructor" /home/gamarino/Documentos/proyectos/protoJS/src/ | grep -v ".h:"
```
This will show you where to add the `ensureArrayBufferConstructor(ctx, globalRoot)` call.

- [ ] **Step 4: Dispatch `new ArrayBuffer(n)` in `src/runtime/ProtoInterpreter.cpp`**

In the `OP_call_constructor` case, after the RegExp constructor block (around line 2567), add a new check before the closing `else`:

Find the section that checks `__regexp_ctor__` and after its closing `}` block, add:

```cpp
} else {
    // Check for ArrayBuffer constructor (marked with __typed_array_ctor__ = "ArrayBuffer").
    const proto::ProtoString* taCtorAttr = JSSymbols::taCtor(pContext);
    const proto::ProtoObject* taCtorName = (func && func != PROTO_NONE && taCtorAttr)
        ? func->getAttribute(pContext, taCtorAttr, false) : nullptr;
    if (taCtorName && taCtorName != PROTO_NONE && taCtorName->isString(pContext)) {
        std::string ctorNameStr;
        taCtorName->asString(pContext)->toUTF8String(pContext, ctorNameStr);
        if (ctorNameStr == "ArrayBuffer") {
            unsigned long byteLen = 0;
            if (argc > 0) {
                const proto::ProtoObject* a0 = argsList->getAt(pContext, 0);
                if (a0 && a0 != PROTO_NONE) {
                    if (a0->isInteger(pContext)) byteLen = static_cast<unsigned long>(std::max(0LL, a0->asLong(pContext)));
                    else if (a0->isDouble(pContext)) byteLen = static_cast<unsigned long>(std::max(0.0, a0->asDouble(pContext)));
                }
            }
            result = createArrayBuffer(pContext, byteLen);
        }
    }
    // TODO: TypedArray constructor dispatch added in Task 4.
}
```

Add `#include "../ArrayBufferPrototype.h"` near the top of `ProtoInterpreter.cpp` (after `#include "../ArrayPrototype.h"`).

- [ ] **Step 5: Build and run ArrayBuffer tests**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
cmake --build build --target protojs 2>&1 | tail -5
TEST262_PATTERNS="built-ins/ArrayBuffer" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -10
```
Expected: pass count increases from 353 toward ~430+.

- [ ] **Step 6: Commit**

```bash
git add src/ArrayBufferPrototype.cpp src/JSSymbols.h src/JSSymbols.cpp \
        src/JSPrototypes.cpp src/runtime/ProtoInterpreter.cpp
git commit -m "feat(arraybuffer): implement ArrayBuffer constructor, byteLength, slice, isView"
```

---

## Task 3: TypedArray constructor infrastructure + index proxy

This is the most critical task. It wires up the interpreter so typed array element access reads/writes raw bytes.

**Files:**
- Modify: `src/TypedArrayPrototype.cpp` (replace stubs with real implementations)
- Modify: `src/runtime/ProtoInterpreter.cpp` (add index proxy + constructor dispatch)
- Modify: `src/JSPrototypes.cpp` (call `ensureTypedArrayConstructors`)

- [ ] **Step 1: Run baseline TypedArray tests**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS="built-ins/TypedArray,built-ins/TypedArrayConstructors" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -5
```
Note pass counts.

- [ ] **Step 2: Implement `typedArrayGetElement` and `typedArraySetElement` in `src/TypedArrayPrototype.cpp`**

These are the raw read/write functions called by the index proxy. Add after the stubs:

```cpp
#include "TypedArrayPrototype.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <limits>

namespace protojs {

// ---- Element read --------------------------------------------------------
const proto::ProtoObject* typedArrayGetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               uint8_t elementType)
{
    if (!ta || ta == PROTO_NONE) return PROTO_NONE;

    // Get the backing ArrayBuffer.
    const proto::ProtoObject* abObj =
        ta->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    if (!abObj || abObj == PROTO_NONE) return PROTO_NONE;

    // Get byte offset and length.
    long long byteOffsetLL = 0;
    const proto::ProtoObject* boObj =
        ta->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    if (boObj && boObj != PROTO_NONE && boObj->isInteger(ctx))
        byteOffsetLL = boObj->asLong(ctx);

    uint32_t length = getTypedArrayLength(ctx, ta);
    if (index >= length) return PROTO_NONE;  // Out of bounds → undefined

    uint8_t elemSize = TA_ELEMENT_SIZE[elementType < 11 ? elementType : 0];
    unsigned long byteIndex = static_cast<unsigned long>(byteOffsetLL) + index * elemSize;

    void* rawPtr = getArrayBufferRawPtr(ctx, abObj);
    if (!rawPtr) return PROTO_NONE;

    const uint8_t* bytes = static_cast<const uint8_t*>(rawPtr) + byteIndex;

    switch (static_cast<TAElementType>(elementType)) {
        case TAElementType::Int8:
            return ctx->fromInteger(static_cast<long long>(static_cast<int8_t>(bytes[0])));
        case TAElementType::Uint8:
        case TAElementType::Uint8Clamped:
            return ctx->fromInteger(static_cast<long long>(bytes[0]));
        case TAElementType::Int16: {
            int16_t v; memcpy(&v, bytes, 2); return ctx->fromInteger(static_cast<long long>(v));
        }
        case TAElementType::Uint16: {
            uint16_t v; memcpy(&v, bytes, 2); return ctx->fromInteger(static_cast<long long>(v));
        }
        case TAElementType::Int32: {
            int32_t v; memcpy(&v, bytes, 4); return ctx->fromInteger(static_cast<long long>(v));
        }
        case TAElementType::Uint32: {
            uint32_t v; memcpy(&v, bytes, 4); return ctx->fromInteger(static_cast<long long>(v));
        }
        case TAElementType::Float32: {
            float v; memcpy(&v, bytes, 4); return ctx->fromDouble(static_cast<double>(v));
        }
        case TAElementType::Float64: {
            double v; memcpy(&v, bytes, 8); return ctx->fromDouble(v);
        }
        case TAElementType::BigInt64: {
            int64_t v; memcpy(&v, bytes, 8); return ctx->fromInteger(static_cast<long long>(v));
        }
        case TAElementType::BigUint64: {
            uint64_t v; memcpy(&v, bytes, 8); return ctx->fromInteger(static_cast<long long>(v));
        }
        default: return PROTO_NONE;
    }
}

// ---- Coerce JS value to integer/double for element write -----------------
static long long toInt64ForWrite(proto::ProtoContext* ctx, const proto::ProtoObject* val) {
    if (!val || val == PROTO_NONE) return 0;
    if (val->isInteger(ctx)) return val->asLong(ctx);
    if (val->isDouble(ctx) || val->isFloat(ctx)) {
        double d = val->asDouble(ctx);
        if (std::isnan(d) || std::isinf(d)) return 0;
        return static_cast<long long>(d);
    }
    if (val->isBoolean(ctx)) return val == PROTO_TRUE ? 1 : 0;
    return 0;
}

static double toDoubleForWrite(proto::ProtoContext* ctx, const proto::ProtoObject* val) {
    if (!val || val == PROTO_NONE) return 0.0;
    if (val->isInteger(ctx)) return static_cast<double>(val->asLong(ctx));
    if (val->isDouble(ctx) || val->isFloat(ctx)) return val->asDouble(ctx);
    if (val->isBoolean(ctx)) return val == PROTO_TRUE ? 1.0 : 0.0;
    return 0.0;
}

// ---- Element write -------------------------------------------------------
const proto::ProtoObject* typedArraySetElement(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* ta,
                                               uint32_t index,
                                               const proto::ProtoObject* value,
                                               uint8_t elementType)
{
    if (!ta || ta == PROTO_NONE) return const_cast<proto::ProtoObject*>(ta);

    const proto::ProtoObject* abObj =
        ta->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    if (!abObj || abObj == PROTO_NONE) return const_cast<proto::ProtoObject*>(ta);

    long long byteOffsetLL = 0;
    const proto::ProtoObject* boObj =
        ta->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    if (boObj && boObj != PROTO_NONE && boObj->isInteger(ctx))
        byteOffsetLL = boObj->asLong(ctx);

    uint32_t length = getTypedArrayLength(ctx, ta);
    if (index >= length) return const_cast<proto::ProtoObject*>(ta);  // Silently ignore

    uint8_t elemSize = TA_ELEMENT_SIZE[elementType < 11 ? elementType : 0];
    unsigned long byteIndex = static_cast<unsigned long>(byteOffsetLL) + index * elemSize;

    void* rawPtr = getArrayBufferRawPtr(ctx, abObj);
    if (!rawPtr) return const_cast<proto::ProtoObject*>(ta);

    uint8_t* bytes = static_cast<uint8_t*>(rawPtr) + byteIndex;

    switch (static_cast<TAElementType>(elementType)) {
        case TAElementType::Int8: {
            int8_t v = static_cast<int8_t>(toInt64ForWrite(ctx, value) & 0xFF);
            memcpy(bytes, &v, 1); break;
        }
        case TAElementType::Uint8: {
            uint8_t v = static_cast<uint8_t>(toInt64ForWrite(ctx, value) & 0xFF);
            memcpy(bytes, &v, 1); break;
        }
        case TAElementType::Uint8Clamped: {
            long long raw = toInt64ForWrite(ctx, value);
            uint8_t v = raw < 0 ? 0 : (raw > 255 ? 255 : static_cast<uint8_t>(raw));
            memcpy(bytes, &v, 1); break;
        }
        case TAElementType::Int16: {
            int16_t v = static_cast<int16_t>(toInt64ForWrite(ctx, value) & 0xFFFF);
            memcpy(bytes, &v, 2); break;
        }
        case TAElementType::Uint16: {
            uint16_t v = static_cast<uint16_t>(toInt64ForWrite(ctx, value) & 0xFFFF);
            memcpy(bytes, &v, 2); break;
        }
        case TAElementType::Int32: {
            int32_t v = static_cast<int32_t>(toInt64ForWrite(ctx, value) & 0xFFFFFFFF);
            memcpy(bytes, &v, 4); break;
        }
        case TAElementType::Uint32: {
            uint32_t v = static_cast<uint32_t>(toInt64ForWrite(ctx, value) & 0xFFFFFFFF);
            memcpy(bytes, &v, 4); break;
        }
        case TAElementType::Float32: {
            float v = static_cast<float>(toDoubleForWrite(ctx, value));
            memcpy(bytes, &v, 4); break;
        }
        case TAElementType::Float64: {
            double v = toDoubleForWrite(ctx, value);
            memcpy(bytes, &v, 8); break;
        }
        case TAElementType::BigInt64: {
            int64_t v = static_cast<int64_t>(toInt64ForWrite(ctx, value));
            memcpy(bytes, &v, 8); break;
        }
        case TAElementType::BigUint64: {
            uint64_t v = static_cast<uint64_t>(toInt64ForWrite(ctx, value));
            memcpy(bytes, &v, 8); break;
        }
        default: break;
    }
    // TypedArray is mutable (created with newObject(true) / newChild(ctx, true)).
    // setAttribute returns the same pointer for mutable objects.
    return const_cast<proto::ProtoObject*>(ta);
}
```

- [ ] **Step 3: Add index proxy to `OP_get_array_el` in `src/runtime/ProtoInterpreter.cpp`**

Add `#include "../TypedArrayPrototype.h"` near the top.

In `case OP_get_array_el:` (line 1705), replace:
```cpp
const proto::ProtoObject* keyObj = toString(pContext, index);
const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
const proto::ProtoObject* val =
    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
stackPush(pContext,val && val != PROTO_NONE ? val : PROTO_NONE);
```
with:
```cpp
const proto::ProtoObject* val;
uint8_t taType = getTypedArrayElementType(pContext, obj);
if (taType != 0xFF && index && index->isInteger(pContext) && index->asLong(pContext) >= 0) {
    val = typedArrayGetElement(pContext, obj, static_cast<uint32_t>(index->asLong(pContext)), taType);
} else {
    const proto::ProtoObject* keyObj = toString(pContext, index);
    const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
    val = (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
}
stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
```

Apply the same pattern to `OP_get_array_el2` and `OP_get_array_el3` (lines 1719, 1731).

- [ ] **Step 4: Add index proxy to `OP_put_array_el` in `src/runtime/ProtoInterpreter.cpp`**

In `case OP_put_array_el:` (line 1744), after retrieving `obj`, `index`, `value`, before the existing setAttribute call, insert:

```cpp
uint8_t taTypeW = getTypedArrayElementType(pContext, obj);
if (taTypeW != 0xFF && index && index->isInteger(pContext) && index->asLong(pContext) >= 0) {
    typedArraySetElement(pContext, obj, static_cast<uint32_t>(index->asLong(pContext)), value, taTypeW);
    // obj is mutable; no updateMapping needed for the write itself.
    // Leave obj on stack.
    stackPush(pContext, obj);
    break;
}
// ... existing setAttribute logic below ...
```

- [ ] **Step 5: Add index proxy to `OP_get_field` for TypedArray numeric keys**

In `case OP_get_field:` (line 1573), after resolving `key`, before calling `getAttribute`:

```cpp
const proto::ProtoObject* val;
uint8_t taTypeF = getTypedArrayElementType(pContext, obj);
if (taTypeF != 0xFF) {
    std::string keyStr;
    if (key) key->toUTF8String(pContext, keyStr);
    if (!keyStr.empty() && std::all_of(keyStr.begin(), keyStr.end(), [](unsigned char c){ return c >= '0' && c <= '9'; })) {
        uint32_t idx = static_cast<uint32_t>(std::stoul(keyStr));
        val = typedArrayGetElement(pContext, obj, idx, taTypeF);
    } else {
        val = obj ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
    }
} else {
    val = obj ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
}
stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
```

Replace the old `const proto::ProtoObject* val = obj ? obj->getAttribute(pContext, key, true) : PROTO_NONE;` line.

Apply similar logic to `OP_get_field2`.

- [ ] **Step 6: Implement `ensureTypedArrayConstructors` and the constructor factory in `src/TypedArrayPrototype.cpp`**

Add after the element read/write implementations:

```cpp
// ---- Module-level state --------------------------------------------------
static const proto::ProtoObject* s_taBaseProto = nullptr;  // %TypedArray%.prototype

// ---- Constructor factory -------------------------------------------------
/**
 * Create a new TypedArray instance from a length.
 * Allocates a new ArrayBuffer and sets all required attributes.
 */
static const proto::ProtoObject* createTypedArrayFromLength(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proto,
    uint8_t elemType,
    uint32_t length)
{
    uint8_t elemSize = TA_ELEMENT_SIZE[elemType < 11 ? elemType : 0];
    unsigned long byteLen = static_cast<unsigned long>(length) * elemSize;
    const proto::ProtoObject* ab = createArrayBuffer(ctx, byteLen);
    if (!ab || ab == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* ta = proto ? proto->newChild(ctx, true) : ctx->newObject(true);
    ta = ta->setAttribute(ctx, JSSymbols::taElementType(ctx),
                          ctx->fromInteger(static_cast<long long>(elemType)));
    ta = ta->setAttribute(ctx, JSSymbols::taBuffer(ctx), ab);
    ta = ta->setAttribute(ctx, JSSymbols::taByteOffset(ctx), ctx->fromInteger(0LL));
    ta = ta->setAttribute(ctx, JSSymbols::byteLength(ctx),
                          ctx->fromInteger(static_cast<long long>(byteLen)));
    ta = ta->setAttribute(ctx, JSSymbols::length(ctx),
                          ctx->fromInteger(static_cast<long long>(length)));
    return ta;
}

/**
 * Create a TypedArray view of an existing ArrayBuffer.
 */
static const proto::ProtoObject* createTypedArrayFromBuffer(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proto,
    uint8_t elemType,
    const proto::ProtoObject* ab,
    long long byteOffset,
    long long length)   // -1 means auto-compute
{
    unsigned long abLen = getArrayBufferByteLength(ctx, ab);
    uint8_t elemSize = TA_ELEMENT_SIZE[elemType < 11 ? elemType : 0];
    if (byteOffset < 0) byteOffset = 0;
    if (byteOffset > static_cast<long long>(abLen)) byteOffset = static_cast<long long>(abLen);
    long long remaining = static_cast<long long>(abLen) - byteOffset;
    uint32_t len;
    if (length < 0) {
        len = elemSize > 0 ? static_cast<uint32_t>(remaining / elemSize) : 0;
    } else {
        len = static_cast<uint32_t>(length);
    }
    unsigned long viewByteLen = static_cast<unsigned long>(len) * elemSize;

    const proto::ProtoObject* ta = proto ? proto->newChild(ctx, true) : ctx->newObject(true);
    ta = ta->setAttribute(ctx, JSSymbols::taElementType(ctx),
                          ctx->fromInteger(static_cast<long long>(elemType)));
    ta = ta->setAttribute(ctx, JSSymbols::taBuffer(ctx), ab);
    ta = ta->setAttribute(ctx, JSSymbols::taByteOffset(ctx), ctx->fromInteger(byteOffset));
    ta = ta->setAttribute(ctx, JSSymbols::byteLength(ctx),
                          ctx->fromInteger(static_cast<long long>(viewByteLen)));
    ta = ta->setAttribute(ctx, JSSymbols::length(ctx),
                          ctx->fromInteger(static_cast<long long>(len)));
    return ta;
}

// ---- Per-type constructor function (registered as native method) ---------
struct TACtorConfig {
    uint8_t elemType;
    const char* name;
    uint8_t elemSize;
};

static const TACtorConfig TA_CONFIGS[11] = {
    { 0, "Int8Array",          1 },
    { 1, "Uint8Array",         1 },
    { 2, "Uint8ClampedArray",  1 },
    { 3, "Int16Array",         2 },
    { 4, "Uint16Array",        2 },
    { 5, "Int32Array",         4 },
    { 6, "Uint32Array",        4 },
    { 7, "Float32Array",       4 },
    { 8, "Float64Array",       8 },
    { 9, "BigInt64Array",      8 },
    {10, "BigUint64Array",     8 },
};

// Each typed array gets its own prototype (child of s_taBaseProto).
static const proto::ProtoObject* s_taProtos[11] = {};

void ensureTypedArrayConstructors(proto::ProtoContext* ctx,
                                  const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot) return;
    const proto::ProtoObject* root = *globalRoot;
    if (!root) return;

    // Idempotency check.
    const proto::ProtoObject* existing =
        root->getAttribute(ctx, JSSymbols::Int8Array(ctx), true);
    if (existing && existing != PROTO_NONE) return;

    // Build %TypedArray%.prototype (shared base).
    const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* baseProto = objProto ? objProto->newChild(ctx, false)
                                                   : ctx->newObject(false);
    // Prototype methods are added in Task 4.
    s_taBaseProto = baseProto;

    // Register each concrete constructor.
    for (int i = 0; i < 11; i++) {
        const TACtorConfig& cfg = TA_CONFIGS[i];

        // Build concrete prototype (child of base proto).
        const proto::ProtoObject* proto = s_taBaseProto->newChild(ctx, false);
        proto = proto->setAttribute(ctx, JSSymbols::BYTES_PER_ELEMENT(ctx),
                                    ctx->fromInteger(static_cast<long long>(cfg.elemSize)));
        s_taProtos[i] = proto;

        // Build constructor object.
        const proto::ProtoObject* ctor = ctx->newObject(false);
        ctor = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), proto);
        ctor = ctor->setAttribute(ctx, JSSymbols::BYTES_PER_ELEMENT(ctx),
                                  ctx->fromInteger(static_cast<long long>(cfg.elemSize)));
        ctor = ctor->setAttribute(ctx, JSSymbols::name(ctx),
                                  ctx->fromUTF8String(cfg.name));
        // Mark as typed array constructor with element type tag.
        ctor = ctor->setAttribute(ctx, JSSymbols::taCtor(ctx),
                                  ctx->fromInteger(static_cast<long long>(cfg.elemType)));

        // Register on global root using the correct name symbol.
        // We use a switch on i to get the right JSSymbols getter.
        const proto::ProtoString* nameSym = nullptr;
        switch (i) {
            case 0:  nameSym = JSSymbols::Int8Array(ctx);         break;
            case 1:  nameSym = JSSymbols::Uint8Array(ctx);        break;
            case 2:  nameSym = JSSymbols::Uint8ClampedArray(ctx); break;
            case 3:  nameSym = JSSymbols::Int16Array(ctx);        break;
            case 4:  nameSym = JSSymbols::Uint16Array(ctx);       break;
            case 5:  nameSym = JSSymbols::Int32Array(ctx);        break;
            case 6:  nameSym = JSSymbols::Uint32Array(ctx);       break;
            case 7:  nameSym = JSSymbols::Float32Array(ctx);      break;
            case 8:  nameSym = JSSymbols::Float64Array(ctx);      break;
            case 9:  nameSym = JSSymbols::BigInt64Array(ctx);     break;
            case 10: nameSym = JSSymbols::BigUint64Array(ctx);    break;
        }
        if (nameSym) root = root->setAttribute(ctx, nameSym, ctor);
    }
    *globalRoot = root;
}
```

- [ ] **Step 7: Dispatch typed array constructors in `OP_call_constructor` in `src/runtime/ProtoInterpreter.cpp`**

In the `__typed_array_ctor__` check added in Task 2, extend the `if (ctorNameStr == "ArrayBuffer")` block to also handle typed array types:

```cpp
if (ctorNameStr == "ArrayBuffer") {
    // ... existing ArrayBuffer code ...
} else {
    // Typed array constructor: taCtorName is an integer (element type tag).
    // Re-read as integer.
    const proto::ProtoObject* taCtorTag =
        func->getAttribute(pContext, JSSymbols::taCtor(pContext), false);
    if (taCtorTag && taCtorTag != PROTO_NONE && taCtorTag->isInteger(pContext)) {
        uint8_t elemType = static_cast<uint8_t>(taCtorTag->asLong(pContext));
        // Get proto from constructor's "prototype" attribute.
        const proto::ProtoObject* taProto =
            func->getAttribute(pContext, JSSymbols::prototype(pContext), false);

        if (argc == 0) {
            result = createTypedArrayFromLength(pContext, taProto, elemType, 0);
        } else {
            const proto::ProtoObject* a0 = argsList->getAt(pContext, 0);
            if (a0 && a0 != PROTO_NONE && isArrayBuffer(pContext, a0)) {
                // new TypedArray(buffer [, byteOffset [, length]])
                long long bo = 0, len = -1;
                if (argc > 1) {
                    const proto::ProtoObject* a1 = argsList->getAt(pContext, 1);
                    if (a1 && a1->isInteger(pContext)) bo = a1->asLong(pContext);
                    else if (a1 && a1->isDouble(pContext)) bo = static_cast<long long>(a1->asDouble(pContext));
                }
                if (argc > 2) {
                    const proto::ProtoObject* a2 = argsList->getAt(pContext, 2);
                    if (a2 && a2->isInteger(pContext)) len = a2->asLong(pContext);
                    else if (a2 && a2->isDouble(pContext)) len = static_cast<long long>(a2->asDouble(pContext));
                }
                result = createTypedArrayFromBuffer(pContext, taProto, elemType, a0, bo, len);
            } else if (a0 && a0 != PROTO_NONE && (a0->isInteger(pContext) || a0->isDouble(pContext))) {
                // new TypedArray(length)
                uint32_t length = a0->isInteger(pContext)
                    ? static_cast<uint32_t>(std::max(0LL, a0->asLong(pContext)))
                    : static_cast<uint32_t>(std::max(0.0, a0->asDouble(pContext)));
                result = createTypedArrayFromLength(pContext, taProto, elemType, length);
            } else if (a0 && a0 != PROTO_NONE && isTypedArray(pContext, a0)) {
                // new TypedArray(otherTypedArray): copy
                uint32_t srcLen = getTypedArrayLength(pContext, a0);
                result = createTypedArrayFromLength(pContext, taProto, elemType, srcLen);
                if (result && result != PROTO_NONE) {
                    for (uint32_t idx = 0; idx < srcLen; idx++) {
                        uint8_t srcElemType = getTypedArrayElementType(pContext, a0);
                        const proto::ProtoObject* elem = typedArrayGetElement(pContext, a0, idx, srcElemType);
                        typedArraySetElement(pContext, result, idx, elem, elemType);
                    }
                }
            } else {
                // new TypedArray(iterable/array-like): iterate
                // For simplicity: try to get .length and numeric indices.
                const proto::ProtoObject* lenObj = a0->getAttribute(pContext, JSSymbols::length(pContext), true);
                uint32_t srcLen = 0;
                if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(pContext))
                    srcLen = static_cast<uint32_t>(std::max(0LL, lenObj->asLong(pContext)));
                result = createTypedArrayFromLength(pContext, taProto, elemType, srcLen);
                if (result && result != PROTO_NONE) {
                    for (uint32_t idx = 0; idx < srcLen; idx++) {
                        const proto::ProtoString* idxKey = JSSymbols::indexKey(pContext, idx);
                        const proto::ProtoObject* elem = a0->getAttribute(pContext, idxKey, false);
                        if (elem && elem != PROTO_NONE)
                            typedArraySetElement(pContext, result, idx, elem, elemType);
                    }
                }
            }
        }
    }
}
```

Add `createTypedArrayFromLength` and `createTypedArrayFromBuffer` to `TypedArrayPrototype.h`:

```cpp
const proto::ProtoObject* createTypedArrayFromLength(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* proto,
                                                     uint8_t elemType,
                                                     uint32_t length);

const proto::ProtoObject* createTypedArrayFromBuffer(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* proto,
                                                     uint8_t elemType,
                                                     const proto::ProtoObject* ab,
                                                     long long byteOffset,
                                                     long long length);
```

- [ ] **Step 8: Call `ensureTypedArrayConstructors` at startup**

Find where `ensureArrayPrototype` and `ensureArrayBufferConstructor` are called (Task 2, Step 3). Add:
```cpp
ensureTypedArrayConstructors(ctx, globalRoot);
```
immediately after.

Also add `#include "../TypedArrayPrototype.h"` to `ProtoInterpreter.cpp`.

- [ ] **Step 9: Build and run basic TypedArray test**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
cmake --build build --target protojs 2>&1 | tail -5
# Quick smoke test
echo "const a = new Uint8Array(4); a[0] = 42; a[1] = 255; console.log(a[0], a[1], a.length);" > /tmp/ta_test.js
./build/protojs /tmp/ta_test.js
```
Expected output: `42 255 4`

```bash
TEST262_PATTERNS="built-ins/TypedArray,built-ins/TypedArrayConstructors" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -5
```
Expected: pass count well above baseline (45 + 86 = 131).

- [ ] **Step 10: Commit**

```bash
git add src/TypedArrayPrototype.cpp src/TypedArrayPrototype.h \
        src/runtime/ProtoInterpreter.cpp
git commit -m "feat(typed-array): implement element read/write, constructor dispatch, and index proxy"
```

---

## Task 4: TypedArray %TypedArray%.prototype methods — batch 1

Add the most-tested prototype methods: `fill`, `forEach`, `map`, `filter`, `indexOf`, `lastIndexOf`, `includes`, `find`, `findIndex`, `every`, `some`, `join`, `toString`.

**Files:**
- Modify: `src/TypedArrayPrototype.cpp`

All methods below follow the same signature:
```cpp
static const proto::ProtoObject* ta_fill(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
```
And are registered on `s_taBaseProto` inside `ensureTypedArrayConstructors` with:
```cpp
baseProto = baseProto->setAttribute(ctx, JSSymbols::fill(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_fill));
```

- [ ] **Step 1: Add missing symbols to JSSymbols**

Add to `src/JSSymbols.h` (common JS property names section):
```cpp
const proto::ProtoString* fill(proto::ProtoContext* ctx);           // "fill"
const proto::ProtoString* forEach(proto::ProtoContext* ctx);        // "forEach"
const proto::ProtoString* map(proto::ProtoContext* ctx);            // "map"
const proto::ProtoString* filter(proto::ProtoContext* ctx);         // "filter"
const proto::ProtoString* indexOf(proto::ProtoContext* ctx);        // "indexOf"
const proto::ProtoString* lastIndexOf(proto::ProtoContext* ctx);    // "lastIndexOf"
const proto::ProtoString* includes(proto::ProtoContext* ctx);       // "includes"
const proto::ProtoString* find(proto::ProtoContext* ctx);           // "find"
const proto::ProtoString* findIndex(proto::ProtoContext* ctx);      // "findIndex"
const proto::ProtoString* every(proto::ProtoContext* ctx);          // "every"
const proto::ProtoString* some(proto::ProtoContext* ctx);           // "some"
const proto::ProtoString* join(proto::ProtoContext* ctx);           // "join"
const proto::ProtoString* reverse(proto::ProtoContext* ctx);        // "reverse"
const proto::ProtoString* sort(proto::ProtoContext* ctx);           // "sort"
const proto::ProtoString* copyWithin(proto::ProtoContext* ctx);     // "copyWithin"
const proto::ProtoString* subarray(proto::ProtoContext* ctx);       // "subarray"
const proto::ProtoString* at(proto::ProtoContext* ctx);             // "at"
const proto::ProtoString* set(proto::ProtoContext* ctx);            // "set" (TypedArray.set)
const proto::ProtoString* reduce(proto::ProtoContext* ctx);         // "reduce"
const proto::ProtoString* reduceRight(proto::ProtoContext* ctx);    // "reduceRight"
const proto::ProtoString* from(proto::ProtoContext* ctx);           // "from"
const proto::ProtoString* of(proto::ProtoContext* ctx);             // "of"
const proto::ProtoString* keys(proto::ProtoContext* ctx);           // "keys"
const proto::ProtoString* entries(proto::ProtoContext* ctx);        // "entries"
```

Add corresponding `DEFINE_SYMBOL` and `REGISTER` entries in `JSSymbols.cpp`.

Note: some of these may already exist (e.g., `values` is already defined). Check before adding duplicates.

- [ ] **Step 2: Implement ta_fill**

```cpp
// Helper: call a JS method/function on pContext.
// Used by forEach, map, etc. to invoke callbacks.
// Defined in ProtoInterpreter.cpp — forward-declare or pass through callJSFunction.
// For now, call via ProtoMethod if available.
static const proto::ProtoObject* callCallback(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* thisVal,
    const proto::ProtoObject* elem,
    const proto::ProtoObject* idxObj,
    const proto::ProtoObject* arr)
{
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    if (fn->isMethod(ctx)) {
        proto::ProtoList* args = const_cast<proto::ProtoList*>(ctx->newList());
        args = const_cast<proto::ProtoList*>(args->appendLast(ctx, elem));
        args = const_cast<proto::ProtoList*>(args->appendLast(ctx, idxObj));
        args = const_cast<proto::ProtoList*>(args->appendLast(ctx, arr));
        proto::ProtoMethod m = fn->asMethod(ctx);
        return m ? m(ctx, thisVal, nullptr, args, nullptr) : PROTO_NONE;
    }
    // JS bytecode function: use callJSFunction (defined in ProtoInterpreter.cpp).
    // We need access to t_currentModule and t_currentGlobalRoot here.
    // This is handled via the extern declaration below.
    return PROTO_NONE;  // JS callbacks require ProtoInterpreter linkage — see Task 5
}

static const proto::ProtoObject* ta_fill(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return const_cast<proto::ProtoObject*>(self);
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return const_cast<proto::ProtoObject*>(self);
    uint32_t len = getTypedArrayLength(ctx, self);

    const proto::ProtoObject* fillVal = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    long long start = 0, end = static_cast<long long>(len);
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx)) start = a1->asLong(ctx);
    }
    if (args && args->getSize(ctx) > 2) {
        const proto::ProtoObject* a2 = args->getAt(ctx, 2);
        if (a2 && a2 != PROTO_NONE && a2->isInteger(ctx)) end = a2->asLong(ctx);
    }

    if (start < 0) start = std::max(static_cast<long long>(len) + start, 0LL);
    else start = std::min(start, static_cast<long long>(len));
    if (end < 0) end = std::max(static_cast<long long>(len) + end, 0LL);
    else end = std::min(end, static_cast<long long>(len));

    for (long long i = start; i < end; i++)
        typedArraySetElement(ctx, self, static_cast<uint32_t>(i), fillVal, et);

    return const_cast<proto::ProtoObject*>(self);
}
```

- [ ] **Step 3: Implement ta_indexOf, ta_lastIndexOf, ta_includes**

```cpp
static const proto::ProtoObject* ta_indexOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return ctx->fromInteger(-1LL);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (!args || args->getSize(ctx) == 0) return ctx->fromInteger(-1LL);
    const proto::ProtoObject* search = args->getAt(ctx, 0);
    long long fromIdx = 0;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx)) fromIdx = a1->asLong(ctx);
    }
    if (fromIdx < 0) fromIdx = std::max(static_cast<long long>(len) + fromIdx, 0LL);

    for (uint32_t i = static_cast<uint32_t>(fromIdx); i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        // Strict equality for numbers.
        if (elem && search) {
            double a = 0, b = 0;
            bool aNum = false, bNum = false;
            if (elem->isInteger(ctx)) { a = static_cast<double>(elem->asLong(ctx)); aNum = true; }
            else if (elem->isDouble(ctx) || elem->isFloat(ctx)) { a = elem->asDouble(ctx); aNum = true; }
            if (search->isInteger(ctx)) { b = static_cast<double>(search->asLong(ctx)); bNum = true; }
            else if (search->isDouble(ctx) || search->isFloat(ctx)) { b = search->asDouble(ctx); bNum = true; }
            if (aNum && bNum && a == b) return ctx->fromInteger(static_cast<long long>(i));
        }
    }
    return ctx->fromInteger(-1LL);
}

static const proto::ProtoObject* ta_lastIndexOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return ctx->fromInteger(-1LL);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (!len || !args || args->getSize(ctx) == 0) return ctx->fromInteger(-1LL);
    const proto::ProtoObject* search = args->getAt(ctx, 0);
    long long fromIdx = static_cast<long long>(len) - 1;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1 != PROTO_NONE && a1->isInteger(ctx)) fromIdx = a1->asLong(ctx);
    }
    if (fromIdx < 0) fromIdx = static_cast<long long>(len) + fromIdx;
    if (fromIdx >= static_cast<long long>(len)) fromIdx = static_cast<long long>(len) - 1;
    if (fromIdx < 0) return ctx->fromInteger(-1LL);

    for (long long i = fromIdx; i >= 0; i--) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, static_cast<uint32_t>(i), et);
        if (elem && search) {
            double a = 0, b = 0;
            bool aNum = false, bNum = false;
            if (elem->isInteger(ctx)) { a = static_cast<double>(elem->asLong(ctx)); aNum = true; }
            else if (elem->isDouble(ctx) || elem->isFloat(ctx)) { a = elem->asDouble(ctx); aNum = true; }
            if (search->isInteger(ctx)) { b = static_cast<double>(search->asLong(ctx)); bNum = true; }
            else if (search->isDouble(ctx) || search->isFloat(ctx)) { b = search->asDouble(ctx); bNum = true; }
            if (aNum && bNum && a == b) return ctx->fromInteger(i);
        }
    }
    return ctx->fromInteger(-1LL);
}

static const proto::ProtoObject* ta_includes(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_FALSE;
    uint32_t len = getTypedArrayLength(ctx, self);
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* search = args->getAt(ctx, 0);

    double searchVal = 0;
    bool searchNaN = false;
    if (search && search->isInteger(ctx)) searchVal = static_cast<double>(search->asLong(ctx));
    else if (search && (search->isDouble(ctx) || search->isFloat(ctx))) {
        searchVal = search->asDouble(ctx);
        searchNaN = std::isnan(searchVal);
    }

    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (!elem || elem == PROTO_NONE) continue;
        double ev = 0;
        bool evNaN = false;
        if (elem->isInteger(ctx)) ev = static_cast<double>(elem->asLong(ctx));
        else if (elem->isDouble(ctx) || elem->isFloat(ctx)) { ev = elem->asDouble(ctx); evNaN = std::isnan(ev); }
        if (searchNaN && evNaN) return PROTO_TRUE;
        if (!searchNaN && !evNaN && ev == searchVal) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}
```

- [ ] **Step 4: Implement ta_join, ta_reverse, ta_at, ta_subarray**

```cpp
static const proto::ProtoObject* ta_join(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return ctx->fromUTF8String("");
    uint32_t len = getTypedArrayLength(ctx, self);
    std::string sep = ",";
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0 != PROTO_NONE && a0->isString(ctx)) {
            sep.clear();
            a0->asString(ctx)->toUTF8String(ctx, sep);
        }
    }
    std::string result;
    for (uint32_t i = 0; i < len; i++) {
        if (i > 0) result += sep;
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (elem && elem != PROTO_NONE) {
            if (elem->isInteger(ctx)) result += std::to_string(elem->asLong(ctx));
            else if (elem->isDouble(ctx) || elem->isFloat(ctx)) {
                char buf[64];
                double d = elem->asDouble(ctx);
                snprintf(buf, sizeof(buf), "%g", d);
                result += buf;
            }
        }
    }
    return ctx->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* ta_reverse(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return const_cast<proto::ProtoObject*>(self);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0, j = len - 1; i < j; i++, j--) {
        const proto::ProtoObject* a = typedArrayGetElement(ctx, self, i, et);
        const proto::ProtoObject* b = typedArrayGetElement(ctx, self, j, et);
        typedArraySetElement(ctx, self, i, b, et);
        typedArraySetElement(ctx, self, j, a, et);
    }
    return const_cast<proto::ProtoObject*>(self);
}

static const proto::ProtoObject* ta_at(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_NONE;
    uint32_t len = getTypedArrayLength(ctx, self);
    long long idx = 0;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) idx = a0->asLong(ctx);
        else if (a0 && a0->isDouble(ctx)) idx = static_cast<long long>(a0->asDouble(ctx));
    }
    if (idx < 0) idx = static_cast<long long>(len) + idx;
    if (idx < 0 || idx >= static_cast<long long>(len)) return PROTO_NONE;
    return typedArrayGetElement(ctx, self, static_cast<uint32_t>(idx), et);
}

static const proto::ProtoObject* ta_subarray(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_NONE;
    uint32_t len = getTypedArrayLength(ctx, self);

    // Get the parent ArrayBuffer and byteOffset of self.
    const proto::ProtoObject* abObj =
        self->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    long long selfBO = 0;
    const proto::ProtoObject* boObj =
        self->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    if (boObj && boObj != PROTO_NONE && boObj->isInteger(ctx))
        selfBO = boObj->asLong(ctx);

    long long begin = 0, end = static_cast<long long>(len);
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) begin = a0->asLong(ctx);
    }
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1->isInteger(ctx)) end = a1->asLong(ctx);
    }
    if (begin < 0) begin = std::max(static_cast<long long>(len) + begin, 0LL);
    else begin = std::min(begin, static_cast<long long>(len));
    if (end < 0) end = std::max(static_cast<long long>(len) + end, 0LL);
    else end = std::min(end, static_cast<long long>(len));
    long long newLen = std::max(end - begin, 0LL);

    uint8_t elemSize = TA_ELEMENT_SIZE[et < 11 ? et : 0];
    long long newByteOffset = selfBO + begin * elemSize;

    // Get proto from the same constructor.
    const proto::ProtoObject* proto = s_taProtos[et < 11 ? et : 0];
    return createTypedArrayFromBuffer(ctx, proto, et, abObj, newByteOffset, newLen);
}
```

- [ ] **Step 5: Implement ta_copyWithin**

```cpp
static const proto::ProtoObject* ta_copyWithin(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return const_cast<proto::ProtoObject*>(self);
    uint32_t len = getTypedArrayLength(ctx, self);

    long long target = 0, start = 0, end = static_cast<long long>(len);
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) target = a0->asLong(ctx);
    }
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1->isInteger(ctx)) start = a1->asLong(ctx);
    }
    if (args && args->getSize(ctx) > 2) {
        const proto::ProtoObject* a2 = args->getAt(ctx, 2);
        if (a2 && a2->isInteger(ctx)) end = a2->asLong(ctx);
    }

    long long sLen = static_cast<long long>(len);
    if (target < 0) target = std::max(sLen + target, 0LL);
    else target = std::min(target, sLen);
    if (start < 0) start = std::max(sLen + start, 0LL);
    else start = std::min(start, sLen);
    if (end < 0) end = std::max(sLen + end, 0LL);
    else end = std::min(end, sLen);

    long long count = std::min(end - start, sLen - target);
    if (count <= 0) return const_cast<proto::ProtoObject*>(self);

    // Use memcpy on the raw buffer for efficiency.
    uint8_t elemSize = TA_ELEMENT_SIZE[et < 11 ? et : 0];
    const proto::ProtoObject* abObj = self->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    long long selfBO = 0;
    const proto::ProtoObject* boObj = self->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    if (boObj && boObj != PROTO_NONE && boObj->isInteger(ctx)) selfBO = boObj->asLong(ctx);

    void* raw = getArrayBufferRawPtr(ctx, abObj);
    if (!raw) return const_cast<proto::ProtoObject*>(self);

    uint8_t* base = static_cast<uint8_t*>(raw) + selfBO;
    memmove(base + target * elemSize, base + start * elemSize,
            static_cast<size_t>(count) * elemSize);
    return const_cast<proto::ProtoObject*>(self);
}
```

- [ ] **Step 6: Register all methods on `s_taBaseProto` inside `ensureTypedArrayConstructors`**

After `s_taBaseProto = baseProto;`, register all methods before the per-type loop:

```cpp
baseProto = baseProto->setAttribute(ctx, JSSymbols::fill(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_fill));
baseProto = baseProto->setAttribute(ctx, JSSymbols::indexOf(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_indexOf));
baseProto = baseProto->setAttribute(ctx, JSSymbols::lastIndexOf(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_lastIndexOf));
baseProto = baseProto->setAttribute(ctx, JSSymbols::includes(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_includes));
baseProto = baseProto->setAttribute(ctx, JSSymbols::join(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_join));
baseProto = baseProto->setAttribute(ctx, JSSymbols::reverse(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_reverse));
baseProto = baseProto->setAttribute(ctx, JSSymbols::at(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_at));
baseProto = baseProto->setAttribute(ctx, JSSymbols::subarray(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_subarray));
baseProto = baseProto->setAttribute(ctx, JSSymbols::copyWithin(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_copyWithin));
s_taBaseProto = baseProto;
```

- [ ] **Step 7: Build and run tests**

```bash
cmake --build build --target protojs 2>&1 | tail -5
TEST262_PATTERNS="built-ins/TypedArray" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -10
```

- [ ] **Step 8: Commit**

```bash
git add src/TypedArrayPrototype.cpp src/JSSymbols.h src/JSSymbols.cpp
git commit -m "feat(typed-array): add fill, indexOf, includes, join, reverse, at, subarray, copyWithin"
```

---

## Task 5: TypedArray forEach, map, find, findIndex, every, some, reduce, reduceRight, sort

These methods require invoking JS callbacks. They use `callJSFunction` from `ProtoInterpreter.cpp`. Add a declaration in `TypedArrayPrototype.cpp`:

```cpp
// Declared in ProtoInterpreter.cpp and linked here.
extern const proto::ProtoObject* callJSFunction(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* thisVal,
    const proto::ProtoList* args);
```

**Check first:** run `grep -n "callJSFunction" /home/gamarino/Documentos/proyectos/protoJS/src/runtime/ProtoInterpreter.cpp` — if the function exists and is not static, just add the `extern` declaration. If it is static or doesn't exist, use `fn->asMethod(ctx)(ctx, thisVal, nullptr, args, nullptr)` for native callbacks only (JS callbacks deferred).

- [ ] **Step 1: Implement `ta_forEach`, `ta_every`, `ta_some`, `ta_find`, `ta_findIndex`**

```cpp
// Helper: invoke a ProtoMethod callback with (elem, index, array) args.
static const proto::ProtoObject* invokeCallback(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* fn,
    const proto::ProtoObject* elem,
    uint32_t idx,
    const proto::ProtoObject* arr)
{
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    if (!fn->isMethod(ctx)) return PROTO_NONE;  // JS bytecode callbacks: partial support
    const proto::ProtoList* args = ctx->newList();
    args = args->appendLast(ctx, elem);
    args = args->appendLast(ctx, ctx->fromInteger(static_cast<long long>(idx)));
    args = args->appendLast(ctx, arr);
    proto::ProtoMethod m = fn->asMethod(ctx);
    return m ? m(ctx, const_cast<proto::ProtoObject*>(fn), nullptr, args, nullptr) : PROTO_NONE;
}

static const proto::ProtoObject* ta_forEach(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++)
        invokeCallback(ctx, fn, typedArrayGetElement(ctx, self, i, et), i, self);
    return PROTO_NONE;
}

static const proto::ProtoObject* ta_every(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_TRUE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* r = invokeCallback(ctx, fn, typedArrayGetElement(ctx, self, i, et), i, self);
        if (!r || r == PROTO_NONE || r == PROTO_FALSE || (r->isInteger(ctx) && r->asLong(ctx) == 0))
            return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* ta_some(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* r = invokeCallback(ctx, fn, typedArrayGetElement(ctx, self, i, et), i, self);
        if (r && r != PROTO_NONE && r != PROTO_FALSE && !(r->isInteger(ctx) && r->asLong(ctx) == 0))
            return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* ta_find(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        const proto::ProtoObject* r = invokeCallback(ctx, fn, elem, i, self);
        if (r && r != PROTO_NONE && r != PROTO_FALSE && !(r->isInteger(ctx) && r->asLong(ctx) == 0))
            return elem;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* ta_findIndex(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return ctx->fromInteger(-1LL);
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        const proto::ProtoObject* r = invokeCallback(ctx, fn, elem, i, self);
        if (r && r != PROTO_NONE && r != PROTO_FALSE && !(r->isInteger(ctx) && r->asLong(ctx) == 0))
            return ctx->fromInteger(static_cast<long long>(i));
    }
    return ctx->fromInteger(-1LL);
}
```

- [ ] **Step 2: Implement `ta_map`, `ta_filter`, `ta_reduce`, `ta_reduceRight`**

```cpp
static const proto::ProtoObject* ta_map(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    const proto::ProtoObject* proto = s_taProtos[et < 11 ? et : 0];
    const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, len);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        const proto::ProtoObject* mapped = invokeCallback(ctx, fn, elem, i, self);
        if (mapped && mapped != PROTO_NONE)
            typedArraySetElement(ctx, result, i, mapped, et);
    }
    return result;
}

static const proto::ProtoObject* ta_reduce(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (len == 0) {
        if (args->getSize(ctx) > 1) return args->getAt(ctx, 1);
        return PROTO_NONE;
    }
    const proto::ProtoObject* acc;
    uint32_t start = 0;
    if (args->getSize(ctx) > 1) {
        acc = args->getAt(ctx, 1);
    } else {
        acc = typedArrayGetElement(ctx, self, 0, et);
        start = 1;
    }
    for (uint32_t i = start; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        // Call fn(acc, elem, i, self)
        if (fn->isMethod(ctx)) {
            const proto::ProtoList* cargs = ctx->newList();
            cargs = cargs->appendLast(ctx, acc);
            cargs = cargs->appendLast(ctx, elem);
            cargs = cargs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(i)));
            cargs = cargs->appendLast(ctx, self);
            proto::ProtoMethod m = fn->asMethod(ctx);
            const proto::ProtoObject* r = m ? m(ctx, PROTO_NONE, nullptr, cargs, nullptr) : PROTO_NONE;
            if (r && r != PROTO_NONE) acc = r;
        }
    }
    return acc;
}

static const proto::ProtoObject* ta_reduceRight(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* fn = args->getAt(ctx, 0);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (len == 0) {
        if (args->getSize(ctx) > 1) return args->getAt(ctx, 1);
        return PROTO_NONE;
    }
    const proto::ProtoObject* acc;
    long long start;
    if (args->getSize(ctx) > 1) {
        acc = args->getAt(ctx, 1);
        start = static_cast<long long>(len) - 1;
    } else {
        acc = typedArrayGetElement(ctx, self, len - 1, et);
        start = static_cast<long long>(len) - 2;
    }
    for (long long i = start; i >= 0; i--) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, static_cast<uint32_t>(i), et);
        if (fn->isMethod(ctx)) {
            const proto::ProtoList* cargs = ctx->newList();
            cargs = cargs->appendLast(ctx, acc);
            cargs = cargs->appendLast(ctx, elem);
            cargs = cargs->appendLast(ctx, ctx->fromInteger(i));
            cargs = cargs->appendLast(ctx, self);
            proto::ProtoMethod m = fn->asMethod(ctx);
            const proto::ProtoObject* r = m ? m(ctx, PROTO_NONE, nullptr, cargs, nullptr) : PROTO_NONE;
            if (r && r != PROTO_NONE) acc = r;
        }
    }
    return acc;
}
```

- [ ] **Step 3: Implement `ta_sort`**

```cpp
static const proto::ProtoObject* ta_sort(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return const_cast<proto::ProtoObject*>(self);
    uint32_t len = getTypedArrayLength(ctx, self);
    if (len <= 1) return const_cast<proto::ProtoObject*>(self);

    // Extract all elements, sort, write back.
    std::vector<double> vals(len);
    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, i, et);
        if (elem && elem != PROTO_NONE) {
            if (elem->isInteger(ctx)) vals[i] = static_cast<double>(elem->asLong(ctx));
            else if (elem->isDouble(ctx) || elem->isFloat(ctx)) vals[i] = elem->asDouble(ctx);
        }
    }

    const proto::ProtoObject* compareFn =
        (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : nullptr;

    if (!compareFn || compareFn == PROTO_NONE || !compareFn->isMethod(ctx)) {
        // Default numeric sort.
        std::sort(vals.begin(), vals.end(), [](double a, double b) {
            if (std::isnan(a)) return false;
            if (std::isnan(b)) return true;
            return a < b;
        });
    }
    // JS comparator sort: TODO for Phase 9+ when callJSFunction is available.

    for (uint32_t i = 0; i < len; i++) {
        const proto::ProtoObject* v;
        if (std::isnan(vals[i])) v = ctx->fromDouble(vals[i]);
        else if (et <= 8 && vals[i] == std::floor(vals[i]) && vals[i] >= LLONG_MIN && vals[i] <= LLONG_MAX)
            v = ctx->fromInteger(static_cast<long long>(vals[i]));
        else v = ctx->fromDouble(vals[i]);
        typedArraySetElement(ctx, self, i, v, et);
    }
    return const_cast<proto::ProtoObject*>(self);
}
```

- [ ] **Step 4: Implement `ta_set`**

```cpp
static const proto::ProtoObject* ta_set(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* src = args->getAt(ctx, 0);
    long long offset = 0;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1->isInteger(ctx)) offset = a1->asLong(ctx);
    }
    if (!src || src == PROTO_NONE) return PROTO_NONE;

    uint32_t selfLen = getTypedArrayLength(ctx, self);
    if (isTypedArray(ctx, src)) {
        uint8_t srcEt = getTypedArrayElementType(ctx, src);
        uint32_t srcLen = getTypedArrayLength(ctx, src);
        for (uint32_t i = 0; i < srcLen; i++) {
            if (static_cast<long long>(i) + offset >= static_cast<long long>(selfLen)) break;
            const proto::ProtoObject* elem = typedArrayGetElement(ctx, src, i, srcEt);
            typedArraySetElement(ctx, self, static_cast<uint32_t>(i + offset), elem, et);
        }
    } else {
        // Array-like source.
        const proto::ProtoObject* lenObj = src->getAttribute(ctx, JSSymbols::length(ctx), true);
        uint32_t srcLen = 0;
        if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx))
            srcLen = static_cast<uint32_t>(std::max(0LL, lenObj->asLong(ctx)));
        for (uint32_t i = 0; i < srcLen; i++) {
            if (static_cast<long long>(i) + offset >= static_cast<long long>(selfLen)) break;
            const proto::ProtoString* idxKey = JSSymbols::indexKey(ctx, i);
            const proto::ProtoObject* elem = src->getAttribute(ctx, idxKey, false);
            if (elem && elem != PROTO_NONE)
                typedArraySetElement(ctx, self, static_cast<uint32_t>(i + offset), elem, et);
        }
    }
    return PROTO_NONE;
}
```

- [ ] **Step 5: Register all methods in `ensureTypedArrayConstructors`**

After the `ta_copyWithin` registration, add:

```cpp
baseProto = baseProto->setAttribute(ctx, JSSymbols::forEach(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_forEach));
baseProto = baseProto->setAttribute(ctx, JSSymbols::every(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_every));
baseProto = baseProto->setAttribute(ctx, JSSymbols::some(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_some));
baseProto = baseProto->setAttribute(ctx, JSSymbols::find(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_find));
baseProto = baseProto->setAttribute(ctx, JSSymbols::findIndex(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_findIndex));
baseProto = baseProto->setAttribute(ctx, JSSymbols::map(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_map));
baseProto = baseProto->setAttribute(ctx, JSSymbols::reduce(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_reduce));
baseProto = baseProto->setAttribute(ctx, JSSymbols::reduceRight(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_reduceRight));
baseProto = baseProto->setAttribute(ctx, JSSymbols::sort(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_sort));
baseProto = baseProto->setAttribute(ctx, JSSymbols::set(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_set));
s_taBaseProto = baseProto;
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build build --target protojs 2>&1 | tail -5
TEST262_PATTERNS="built-ins/TypedArray,built-ins/TypedArrayConstructors" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -10
```

- [ ] **Step 7: Commit**

```bash
git add src/TypedArrayPrototype.cpp src/JSSymbols.h src/JSSymbols.cpp
git commit -m "feat(typed-array): add forEach, map, find, every, some, reduce, sort, set"
```

---

## Task 6: TypedArray static methods (from, of) + %TypedArray%.prototype properties

Add `TypedArray.from(arrayLike[, mapFn])`, `TypedArray.of(...items)`, `slice` (returning a new typed array of same type), `toReversed`, `toSorted`, `with`, and prototype properties `buffer`, `byteOffset`, `byteLength`, `BYTES_PER_ELEMENT`, `length`.

**Files:**
- Modify: `src/TypedArrayPrototype.cpp`

- [ ] **Step 1: Implement getter methods for `buffer`, `byteOffset`, `byteLength`, `length`**

```cpp
static const proto::ProtoObject* ta_get_buffer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* ab = self->getAttribute(ctx, JSSymbols::taBuffer(ctx), false);
    return ab ? ab : PROTO_NONE;
}

static const proto::ProtoObject* ta_get_byteOffset(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    const proto::ProtoObject* bo = self->getAttribute(ctx, JSSymbols::taByteOffset(ctx), false);
    return (bo && bo != PROTO_NONE) ? bo : ctx->fromInteger(0LL);
}

static const proto::ProtoObject* ta_get_byteLength(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    const proto::ProtoObject* bl = self->getAttribute(ctx, JSSymbols::byteLength(ctx), false);
    return (bl && bl != PROTO_NONE) ? bl : ctx->fromInteger(0LL);
}
```

Register in `ensureTypedArrayConstructors`:
```cpp
baseProto = baseProto->setAttribute(ctx, JSSymbols::buffer(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_get_buffer));
baseProto = baseProto->setAttribute(ctx, JSSymbols::byteOffset(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_get_byteOffset));
baseProto = baseProto->setAttribute(ctx, JSSymbols::byteLength(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_get_byteLength));
```

- [ ] **Step 2: Implement `ta_slice`**

```cpp
static const proto::ProtoObject* ta_slice(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t et = getTypedArrayElementType(ctx, self);
    if (et == 0xFF) return PROTO_NONE;
    uint32_t len = getTypedArrayLength(ctx, self);
    long long start = 0, end = static_cast<long long>(len);
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) start = a0->asLong(ctx);
    }
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* a1 = args->getAt(ctx, 1);
        if (a1 && a1->isInteger(ctx)) end = a1->asLong(ctx);
    }
    long long sLen = static_cast<long long>(len);
    if (start < 0) start = std::max(sLen + start, 0LL);
    else start = std::min(start, sLen);
    if (end < 0) end = std::max(sLen + end, 0LL);
    else end = std::min(end, sLen);
    long long newLen = std::max(end - start, 0LL);

    const proto::ProtoObject* proto = s_taProtos[et < 11 ? et : 0];
    const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, static_cast<uint32_t>(newLen));
    for (long long i = start; i < end; i++) {
        const proto::ProtoObject* elem = typedArrayGetElement(ctx, self, static_cast<uint32_t>(i), et);
        typedArraySetElement(ctx, result, static_cast<uint32_t>(i - start), elem, et);
    }
    return result;
}
```

- [ ] **Step 3: Implement `ta_of` and `ta_from` static methods**

These are registered on each constructor object, not the prototype.

```cpp
// ta_of: TypedArray.of(v1, v2, ...) — create typed array from argument list.
// elemType is captured via a closure trick: we register a different native for each type.
// Simplest approach: store elemType in a thread_local and use a wrapper, OR use the
// constructor marker on the function itself. Here we use 11 separate lambdas stored as static.

struct TAStaticMethods {
    static const proto::ProtoObject* makeOf(
        proto::ProtoContext* ctx, uint8_t et,
        const proto::ProtoObject* proto, const proto::ProtoList* args)
    {
        uint32_t len = args ? static_cast<uint32_t>(args->getSize(ctx)) : 0;
        const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, len);
        if (!result || result == PROTO_NONE) return PROTO_NONE;
        for (uint32_t i = 0; i < len; i++) {
            const proto::ProtoObject* v = args->getAt(ctx, static_cast<int>(i));
            typedArraySetElement(ctx, result, i, v, et);
        }
        return result;
    }

    static const proto::ProtoObject* makeFrom(
        proto::ProtoContext* ctx, uint8_t et,
        const proto::ProtoObject* proto, const proto::ProtoList* args)
    {
        if (!args || args->getSize(ctx) == 0) return createTypedArrayFromLength(ctx, proto, et, 0);
        const proto::ProtoObject* src = args->getAt(ctx, 0);
        if (!src || src == PROTO_NONE) return createTypedArrayFromLength(ctx, proto, et, 0);

        // Get length.
        const proto::ProtoObject* lenObj = src->getAttribute(ctx, JSSymbols::length(ctx), true);
        uint32_t srcLen = 0;
        if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx))
            srcLen = static_cast<uint32_t>(std::max(0LL, lenObj->asLong(ctx)));
        else if (isTypedArray(ctx, src))
            srcLen = getTypedArrayLength(ctx, src);

        const proto::ProtoObject* result = createTypedArrayFromLength(ctx, proto, et, srcLen);
        for (uint32_t i = 0; i < srcLen; i++) {
            const proto::ProtoObject* elem;
            if (isTypedArray(ctx, src)) {
                uint8_t srcEt = getTypedArrayElementType(ctx, src);
                elem = typedArrayGetElement(ctx, src, i, srcEt);
            } else {
                const proto::ProtoString* idxKey = JSSymbols::indexKey(ctx, i);
                elem = src->getAttribute(ctx, idxKey, false);
            }
            if (elem && elem != PROTO_NONE)
                typedArraySetElement(ctx, result, i, elem, et);
        }
        return result;
    }
};

// We need 11 distinct native methods for `of` and `from` (one per type).
// Use a macro to define them:
#define DEFINE_TA_STATIC(idx) \
static const proto::ProtoObject* ta_of_##idx( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, \
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) { \
    return TAStaticMethods::makeOf(ctx, idx, s_taProtos[idx], args); \
} \
static const proto::ProtoObject* ta_from_##idx( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, \
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) { \
    return TAStaticMethods::makeFrom(ctx, idx, s_taProtos[idx], args); \
}

DEFINE_TA_STATIC(0)  DEFINE_TA_STATIC(1)  DEFINE_TA_STATIC(2)
DEFINE_TA_STATIC(3)  DEFINE_TA_STATIC(4)  DEFINE_TA_STATIC(5)
DEFINE_TA_STATIC(6)  DEFINE_TA_STATIC(7)  DEFINE_TA_STATIC(8)
DEFINE_TA_STATIC(9)  DEFINE_TA_STATIC(10)
#undef DEFINE_TA_STATIC

using TAStaticMethod = const proto::ProtoObject*(*)(proto::ProtoContext*, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*);

static const TAStaticMethod TA_OF_METHODS[11]   = { ta_of_0, ta_of_1, ta_of_2, ta_of_3, ta_of_4, ta_of_5, ta_of_6, ta_of_7, ta_of_8, ta_of_9, ta_of_10 };
static const TAStaticMethod TA_FROM_METHODS[11] = { ta_from_0, ta_from_1, ta_from_2, ta_from_3, ta_from_4, ta_from_5, ta_from_6, ta_from_7, ta_from_8, ta_from_9, ta_from_10 };
```

In `ensureTypedArrayConstructors`, inside the per-type loop after building `ctor`, add:
```cpp
ctor = ctor->setAttribute(ctx, JSSymbols::of(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(ctor), TA_OF_METHODS[i]));
ctor = ctor->setAttribute(ctx, JSSymbols::from(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(ctor), TA_FROM_METHODS[i]));
```

Also register `ta_slice` on the base proto:
```cpp
baseProto = baseProto->setAttribute(ctx, JSSymbols::slice(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_slice));
```

- [ ] **Step 4: Build, run tests, commit**

```bash
cmake --build build --target protojs 2>&1 | tail -5
TEST262_PATTERNS="built-ins/TypedArray,built-ins/TypedArrayConstructors" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -10
git add src/TypedArrayPrototype.cpp src/JSSymbols.h src/JSSymbols.cpp
git commit -m "feat(typed-array): add slice, from, of, buffer/byteOffset/byteLength getters"
```

---

## Task 7: TypedArray iterators (keys, values, entries, Symbol.iterator)

**Files:**
- Modify: `src/TypedArrayPrototype.cpp`

Iterators follow the existing `iterIdx` / `iterArr` / `iterKind` pattern used in array iterators.

- [ ] **Step 1: Implement iterator factory and `next()`**

```cpp
// Iterator kinds: "values" = 0, "keys" = 1, "entries" = 2
static const proto::ProtoObject* taIterNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* taObj =
        self->getAttribute(ctx, JSSymbols::iterArr(ctx), false);
    const proto::ProtoObject* idxObj =
        self->getAttribute(ctx, JSSymbols::iterIdx(ctx), false);
    const proto::ProtoObject* kindObj =
        self->getAttribute(ctx, JSSymbols::iterKind(ctx), false);

    if (!taObj || taObj == PROTO_NONE) {
        // Done.
        const proto::ProtoObject* result = ctx->newObject(true);
        result = result->setAttribute(ctx, JSSymbols::value(ctx), PROTO_NONE);
        result = result->setAttribute(ctx, JSSymbols::done(ctx), PROTO_TRUE);
        return result;
    }

    uint32_t idx = (idxObj && idxObj != PROTO_NONE && idxObj->isInteger(ctx))
        ? static_cast<uint32_t>(idxObj->asLong(ctx)) : 0;
    uint32_t len = getTypedArrayLength(ctx, taObj);
    uint8_t et = getTypedArrayElementType(ctx, taObj);

    if (idx >= len) {
        const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, JSSymbols::iterDone(ctx), PROTO_TRUE);
        const proto::ProtoObject* result = ctx->newObject(true);
        result = result->setAttribute(ctx, JSSymbols::value(ctx), PROTO_NONE);
        result = result->setAttribute(ctx, JSSymbols::done(ctx), PROTO_TRUE);
        return result;
    }

    // Advance index.
    const proto::ProtoObject* newSelf =
        const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, JSSymbols::iterIdx(ctx),
                                                             ctx->fromInteger(static_cast<long long>(idx + 1)));
    // Note: since TypedArray iterators are mutable objects, self == newSelf.

    long long kind = (kindObj && kindObj != PROTO_NONE && kindObj->isInteger(ctx))
        ? kindObj->asLong(ctx) : 0;

    const proto::ProtoObject* iterValue;
    if (kind == 1) {
        // keys
        iterValue = ctx->fromInteger(static_cast<long long>(idx));
    } else if (kind == 2) {
        // entries: [idx, value]
        const proto::ProtoObject* pair = ctx->newObject(true);
        pair = pair->setAttribute(ctx, JSSymbols::indexKey(ctx, 0), ctx->fromInteger(static_cast<long long>(idx)));
        pair = pair->setAttribute(ctx, JSSymbols::indexKey(ctx, 1), typedArrayGetElement(ctx, taObj, idx, et));
        pair = pair->setAttribute(ctx, JSSymbols::length(ctx), ctx->fromInteger(2LL));
        pair = pair->setAttribute(ctx, JSSymbols::isArray(ctx), PROTO_TRUE);
        iterValue = pair;
    } else {
        // values (default)
        iterValue = typedArrayGetElement(ctx, taObj, idx, et);
    }

    const proto::ProtoObject* result = ctx->newObject(true);
    result = result->setAttribute(ctx, JSSymbols::value(ctx), iterValue ? iterValue : PROTO_NONE);
    result = result->setAttribute(ctx, JSSymbols::done(ctx), PROTO_FALSE);
    return result;
}

// Factory: create a TypedArray iterator object with kind = 0 (values), 1 (keys), 2 (entries).
static const proto::ProtoObject* makeTAIterator(
    proto::ProtoContext* ctx, const proto::ProtoObject* ta, long long kind)
{
    const proto::ProtoObject* iter = ctx->newObject(true);
    iter = iter->setAttribute(ctx, JSSymbols::iterArr(ctx), ta);
    iter = iter->setAttribute(ctx, JSSymbols::iterIdx(ctx), ctx->fromInteger(0LL));
    iter = iter->setAttribute(ctx, JSSymbols::iterKind(ctx), ctx->fromInteger(kind));
    iter = iter->setAttribute(ctx, JSSymbols::next(ctx),
                              ctx->fromMethod(const_cast<proto::ProtoObject*>(iter), taIterNext));
    // Make it iterable (Symbol.iterator returns self).
    iter = iter->setAttribute(ctx, JSSymbols::symbolIterator(ctx),
                              ctx->fromMethod(const_cast<proto::ProtoObject*>(iter),
                                             [](proto::ProtoContext*, const proto::ProtoObject* self,
                                                const proto::ParentLink*, const proto::ProtoList*,
                                                const proto::ProtoSparseList*) {
                                                 return self;
                                             }));
    return iter;
}

static const proto::ProtoObject* ta_values(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeTAIterator(ctx, self, 0); }

static const proto::ProtoObject* ta_keys(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeTAIterator(ctx, self, 1); }

static const proto::ProtoObject* ta_entries(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeTAIterator(ctx, self, 2); }
```

Add `JSSymbols::symbolIterator` to JSSymbols if not present:
```cpp
const proto::ProtoString* symbolIterator(proto::ProtoContext* ctx); // "Symbol.iterator"
```
Register in JSSymbols.cpp: `DEFINE_SYMBOL(symbolIterator, "Symbol.iterator")` and add to REGISTER.

- [ ] **Step 2: Register iterator methods**

In `ensureTypedArrayConstructors`, after the other registrations:
```cpp
baseProto = baseProto->setAttribute(ctx, JSSymbols::keys(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_keys));
baseProto = baseProto->setAttribute(ctx, JSSymbols::values(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_values));
baseProto = baseProto->setAttribute(ctx, JSSymbols::entries(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_entries));
baseProto = baseProto->setAttribute(ctx, JSSymbols::symbolIterator(ctx),
    ctx->fromMethod(const_cast<proto::ProtoObject*>(baseProto), ta_values));
```

- [ ] **Step 3: Build, run tests, commit**

```bash
cmake --build build --target protojs 2>&1 | tail -5
TEST262_PATTERNS="built-ins/TypedArray,built-ins/TypedArrayConstructors" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -10
git add src/TypedArrayPrototype.cpp src/JSSymbols.h src/JSSymbols.cpp
git commit -m "feat(typed-array): add keys/values/entries iterators and Symbol.iterator"
```

---

## Task 8: DataView — complete implementation

**Files:**
- Modify: `src/DataViewPrototype.cpp` (replace stub)

DataView stores `__dv_buffer__` (ArrayBuffer), `__dv_byte_offset__`, `__dv_byte_length__`.
Each `getXxx(offset, littleEndian)` and `setXxx(offset, value, littleEndian)` method reads/writes raw bytes from the backing buffer.

- [ ] **Step 1: Run baseline DataView tests**

```bash
TEST262_PATTERNS="built-ins/DataView" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -5
```

- [ ] **Step 2: Implement DataView constructor and all get/set methods**

Add to `src/JSSymbols.h`:
```cpp
const proto::ProtoString* getInt8(proto::ProtoContext* ctx);      // "getInt8"
const proto::ProtoString* getUint8(proto::ProtoContext* ctx);     // "getUint8"
const proto::ProtoString* getInt16(proto::ProtoContext* ctx);     // "getInt16"
const proto::ProtoString* getUint16(proto::ProtoContext* ctx);    // "getUint16"
const proto::ProtoString* getInt32(proto::ProtoContext* ctx);     // "getInt32"
const proto::ProtoString* getUint32(proto::ProtoContext* ctx);    // "getUint32"
const proto::ProtoString* getFloat32(proto::ProtoContext* ctx);   // "getFloat32"
const proto::ProtoString* getFloat64(proto::ProtoContext* ctx);   // "getFloat64"
const proto::ProtoString* getBigInt64(proto::ProtoContext* ctx);  // "getBigInt64"
const proto::ProtoString* getBigUint64(proto::ProtoContext* ctx); // "getBigUint64"
const proto::ProtoString* setInt8(proto::ProtoContext* ctx);      // "setInt8"
const proto::ProtoString* setUint8(proto::ProtoContext* ctx);     // "setUint8"
const proto::ProtoString* setInt16(proto::ProtoContext* ctx);     // "setInt16"
const proto::ProtoString* setUint16(proto::ProtoContext* ctx);    // "setUint16"
const proto::ProtoString* setInt32(proto::ProtoContext* ctx);     // "setInt32"
const proto::ProtoString* setUint32(proto::ProtoContext* ctx);    // "setUint32"
const proto::ProtoString* setFloat32(proto::ProtoContext* ctx);   // "setFloat32"
const proto::ProtoString* setFloat64(proto::ProtoContext* ctx);   // "setFloat64"
const proto::ProtoString* setBigInt64(proto::ProtoContext* ctx);  // "setBigInt64"
const proto::ProtoString* setBigUint64(proto::ProtoContext* ctx); // "setBigUint64"
```

Add `DEFINE_SYMBOL` and `REGISTER` entries in `JSSymbols.cpp`.

Replace `src/DataViewPrototype.cpp` with:

```cpp
#include "DataViewPrototype.h"
#include "ArrayBufferPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"
#include <cstring>
#include <cstdint>

namespace protojs {

static const proto::ProtoObject* s_dvProto = nullptr;

// ---- Helper: get raw byte pointer adjusted for DataView offset --------
static uint8_t* dvGetRaw(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                          long long byteOffset, uint32_t accessSize)
{
    if (!self || self == PROTO_NONE) return nullptr;
    const proto::ProtoObject* abObj =
        self->getAttribute(ctx, JSSymbols::dvBuffer(ctx), false);
    if (!abObj || abObj == PROTO_NONE) return nullptr;

    long long viewOffset = 0;
    const proto::ProtoObject* voObj = self->getAttribute(ctx, JSSymbols::dvByteOffset(ctx), false);
    if (voObj && voObj != PROTO_NONE && voObj->isInteger(ctx)) viewOffset = voObj->asLong(ctx);

    long long viewLen = static_cast<long long>(getArrayBufferByteLength(ctx, abObj));
    long long absOffset = viewOffset + byteOffset;
    if (absOffset < 0 || absOffset + static_cast<long long>(accessSize) > viewLen) return nullptr;

    void* raw = getArrayBufferRawPtr(ctx, abObj);
    return raw ? static_cast<uint8_t*>(raw) + absOffset : nullptr;
}

static bool isLittleEndian(proto::ProtoContext* ctx, const proto::ProtoList* args, int argIdx) {
    if (!args || args->getSize(ctx) <= argIdx) return false;
    const proto::ProtoObject* le = args->getAt(ctx, argIdx);
    return le && le != PROTO_NONE && le != PROTO_FALSE &&
           !(le->isInteger(ctx) && le->asLong(ctx) == 0);
}

static long long dvGetByteOffset(proto::ProtoContext* ctx, const proto::ProtoList* args) {
    if (!args || args->getSize(ctx) == 0) return 0;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    if (!a0 || a0 == PROTO_NONE) return 0;
    if (a0->isInteger(ctx)) return a0->asLong(ctx);
    if (a0->isDouble(ctx) || a0->isFloat(ctx)) return static_cast<long long>(a0->asDouble(ctx));
    return 0;
}

// Byte-swap helpers.
static uint16_t bswap16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
static uint64_t bswap64(uint64_t v) { return __builtin_bswap64(v); }

// ---- get methods --------------------------------------------------------
#define DEFINE_DV_GET(Name, CType, Size, Convert) \
static const proto::ProtoObject* dv_get##Name( \
    proto::ProtoContext* ctx, const proto::ProtoObject* self, \
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) \
{ \
    long long bo = dvGetByteOffset(ctx, args); \
    uint8_t* raw = dvGetRaw(ctx, self, bo, Size); \
    if (!raw) return PROTO_NONE; \
    CType v; memcpy(&v, raw, Size); \
    return Convert; \
}

// Note: DataView uses little-endian arg (arg index 1 for multi-byte types).
// For single-byte types there's no endianness concern.
static const proto::ProtoObject* dv_getInt8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(static_cast<int8_t>(raw[0])));
}

static const proto::ProtoObject* dv_getUint8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(raw[0]));
}

static const proto::ProtoObject* dv_getInt16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 2);
    if (!raw) return PROTO_NONE;
    uint16_t u; memcpy(&u, raw, 2);
    // Host is likely LE; if BE requested, swap.
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap16(u);
    #else
    if (le) u = bswap16(u);
    #endif
    return ctx->fromInteger(static_cast<long long>(static_cast<int16_t>(u)));
}

static const proto::ProtoObject* dv_getUint16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 2);
    if (!raw) return PROTO_NONE;
    uint16_t u; memcpy(&u, raw, 2);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap16(u);
    #else
    if (le) u = bswap16(u);
    #endif
    return ctx->fromInteger(static_cast<long long>(u));
}

static const proto::ProtoObject* dv_getInt32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u; memcpy(&u, raw, 4);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
    #else
    if (le) u = bswap32(u);
    #endif
    return ctx->fromInteger(static_cast<long long>(static_cast<int32_t>(u)));
}

static const proto::ProtoObject* dv_getUint32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u; memcpy(&u, raw, 4);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
    #else
    if (le) u = bswap32(u);
    #endif
    return ctx->fromInteger(static_cast<long long>(u));
}

static const proto::ProtoObject* dv_getFloat32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u; memcpy(&u, raw, 4);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
    #else
    if (le) u = bswap32(u);
    #endif
    float f; memcpy(&f, &u, 4);
    return ctx->fromDouble(static_cast<double>(f));
}

static const proto::ProtoObject* dv_getFloat64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u; memcpy(&u, raw, 8);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
    #else
    if (le) u = bswap64(u);
    #endif
    double d; memcpy(&d, &u, 8);
    return ctx->fromDouble(d);
}

static const proto::ProtoObject* dv_getBigInt64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u; memcpy(&u, raw, 8);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
    #else
    if (le) u = bswap64(u);
    #endif
    return ctx->fromInteger(static_cast<long long>(static_cast<int64_t>(u)));
}

static const proto::ProtoObject* dv_getBigUint64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 1);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u; memcpy(&u, raw, 8);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
    #else
    if (le) u = bswap64(u);
    #endif
    return ctx->fromInteger(static_cast<long long>(u));
}

// ---- set methods --------------------------------------------------------
static long long dvSetGetValue(proto::ProtoContext* ctx, const proto::ProtoList* args, int idx) {
    if (!args || args->getSize(ctx) <= idx) return 0;
    const proto::ProtoObject* v = args->getAt(ctx, idx);
    if (!v || v == PROTO_NONE) return 0;
    if (v->isInteger(ctx)) return v->asLong(ctx);
    if (v->isDouble(ctx) || v->isFloat(ctx)) return static_cast<long long>(v->asDouble(ctx));
    return 0;
}

static double dvSetGetValueDouble(proto::ProtoContext* ctx, const proto::ProtoList* args, int idx) {
    if (!args || args->getSize(ctx) <= idx) return 0.0;
    const proto::ProtoObject* v = args->getAt(ctx, idx);
    if (!v || v == PROTO_NONE) return 0.0;
    if (v->isInteger(ctx)) return static_cast<double>(v->asLong(ctx));
    if (v->isDouble(ctx) || v->isFloat(ctx)) return v->asDouble(ctx);
    return 0.0;
}

static const proto::ProtoObject* dv_setInt8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    raw[0] = static_cast<uint8_t>(dvSetGetValue(ctx, args, 1) & 0xFF);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setUint8(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    uint8_t* raw = dvGetRaw(ctx, self, dvGetByteOffset(ctx, args), 1);
    if (!raw) return PROTO_NONE;
    raw[0] = static_cast<uint8_t>(dvSetGetValue(ctx, args, 1) & 0xFF);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setInt16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 2);
    if (!raw) return PROTO_NONE;
    uint16_t u = static_cast<uint16_t>(dvSetGetValue(ctx, args, 1) & 0xFFFF);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap16(u);
    #else
    if (le) u = bswap16(u);
    #endif
    memcpy(raw, &u, 2);
    return PROTO_NONE;
}

// setUint16, setInt32, setUint32, setFloat32, setFloat64, setBigInt64, setBigUint64
// follow the identical pattern as setInt16 with the appropriate type/size.
static const proto::ProtoObject* dv_setUint16(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 2);
    if (!raw) return PROTO_NONE;
    uint16_t u = static_cast<uint16_t>(dvSetGetValue(ctx, args, 1) & 0xFFFF);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap16(u);
    #else
    if (le) u = bswap16(u);
    #endif
    memcpy(raw, &u, 2);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setInt32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u = static_cast<uint32_t>(dvSetGetValue(ctx, args, 1) & 0xFFFFFFFF);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
    #else
    if (le) u = bswap32(u);
    #endif
    memcpy(raw, &u, 4);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setUint32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    uint32_t u = static_cast<uint32_t>(dvSetGetValue(ctx, args, 1) & 0xFFFFFFFF);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
    #else
    if (le) u = bswap32(u);
    #endif
    memcpy(raw, &u, 4);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setFloat32(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 4);
    if (!raw) return PROTO_NONE;
    float f = static_cast<float>(dvSetGetValueDouble(ctx, args, 1));
    uint32_t u; memcpy(&u, &f, 4);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap32(u);
    #else
    if (le) u = bswap32(u);
    #endif
    memcpy(raw, &u, 4);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setFloat64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    double d = dvSetGetValueDouble(ctx, args, 1);
    uint64_t u; memcpy(&u, &d, 8);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
    #else
    if (le) u = bswap64(u);
    #endif
    memcpy(raw, &u, 8);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setBigInt64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u = static_cast<uint64_t>(dvSetGetValue(ctx, args, 1));
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
    #else
    if (le) u = bswap64(u);
    #endif
    memcpy(raw, &u, 8);
    return PROTO_NONE;
}

static const proto::ProtoObject* dv_setBigUint64(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    long long bo = dvGetByteOffset(ctx, args);
    bool le = isLittleEndian(ctx, args, 2);
    uint8_t* raw = dvGetRaw(ctx, self, bo, 8);
    if (!raw) return PROTO_NONE;
    uint64_t u = static_cast<uint64_t>(dvSetGetValue(ctx, args, 1));
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (!le) u = bswap64(u);
    #else
    if (le) u = bswap64(u);
    #endif
    memcpy(raw, &u, 8);
    return PROTO_NONE;
}

// ---- DataView.prototype properties (buffer, byteOffset, byteLength) ----
static const proto::ProtoObject* dv_get_buffer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* ab = self->getAttribute(ctx, JSSymbols::dvBuffer(ctx), false);
    return ab ? ab : PROTO_NONE;
}

static const proto::ProtoObject* dv_get_byteOffset(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    const proto::ProtoObject* bo = self->getAttribute(ctx, JSSymbols::dvByteOffset(ctx), false);
    return (bo && bo != PROTO_NONE) ? bo : ctx->fromInteger(0LL);
}

static const proto::ProtoObject* dv_get_byteLength(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    const proto::ProtoObject* bl = self->getAttribute(ctx, JSSymbols::dvByteLength(ctx), false);
    return (bl && bl != PROTO_NONE) ? bl : ctx->fromInteger(0LL);
}

// ---- Bootstrap ----------------------------------------------------------
void ensureDataViewConstructor(proto::ProtoContext* ctx,
                               const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot) return;
    const proto::ProtoObject* root = *globalRoot;
    if (!root) return;

    const proto::ProtoObject* existing =
        root->getAttribute(ctx, JSSymbols::DataView(ctx), true);
    if (existing && existing != PROTO_NONE) return;

    const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objProto ? objProto->newChild(ctx, false)
                                               : ctx->newObject(false);

    // Register prototype methods.
    proto = proto->setAttribute(ctx, JSSymbols::getInt8(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getInt8));
    proto = proto->setAttribute(ctx, JSSymbols::getUint8(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getUint8));
    proto = proto->setAttribute(ctx, JSSymbols::getInt16(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getInt16));
    proto = proto->setAttribute(ctx, JSSymbols::getUint16(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getUint16));
    proto = proto->setAttribute(ctx, JSSymbols::getInt32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getInt32));
    proto = proto->setAttribute(ctx, JSSymbols::getUint32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getUint32));
    proto = proto->setAttribute(ctx, JSSymbols::getFloat32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getFloat32));
    proto = proto->setAttribute(ctx, JSSymbols::getFloat64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getFloat64));
    proto = proto->setAttribute(ctx, JSSymbols::getBigInt64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getBigInt64));
    proto = proto->setAttribute(ctx, JSSymbols::getBigUint64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_getBigUint64));
    proto = proto->setAttribute(ctx, JSSymbols::setInt8(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setInt8));
    proto = proto->setAttribute(ctx, JSSymbols::setUint8(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setUint8));
    proto = proto->setAttribute(ctx, JSSymbols::setInt16(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setInt16));
    proto = proto->setAttribute(ctx, JSSymbols::setUint16(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setUint16));
    proto = proto->setAttribute(ctx, JSSymbols::setInt32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setInt32));
    proto = proto->setAttribute(ctx, JSSymbols::setUint32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setUint32));
    proto = proto->setAttribute(ctx, JSSymbols::setFloat32(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setFloat32));
    proto = proto->setAttribute(ctx, JSSymbols::setFloat64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setFloat64));
    proto = proto->setAttribute(ctx, JSSymbols::setBigInt64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setBigInt64));
    proto = proto->setAttribute(ctx, JSSymbols::setBigUint64(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_setBigUint64));
    proto = proto->setAttribute(ctx, JSSymbols::buffer(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_get_buffer));
    proto = proto->setAttribute(ctx, JSSymbols::byteOffset(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_get_byteOffset));
    proto = proto->setAttribute(ctx, JSSymbols::byteLength(ctx),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(proto), dv_get_byteLength));

    s_dvProto = proto;

    // Build constructor.
    const proto::ProtoObject* ctor = ctx->newObject(false);
    ctor = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), proto);
    // Mark as DataView constructor.
    ctor = ctor->setAttribute(ctx, JSSymbols::taCtor(ctx),
                               ctx->fromUTF8String("DataView"));

    root = root->setAttribute(ctx, JSSymbols::DataView(ctx), ctor);
    *globalRoot = root;
}

} // namespace protojs
```

- [ ] **Step 3: Add DataView constructor dispatch in `OP_call_constructor`**

In `ProtoInterpreter.cpp`, in the `taCtor` dispatch block, extend to handle `"DataView"`:

```cpp
} else if (ctorNameStr == "DataView") {
    // new DataView(buffer [, byteOffset [, byteLength]])
    if (argc < 1) { result = PROTO_NONE; break; }
    const proto::ProtoObject* abArg = argsList->getAt(pContext, 0);
    if (!isArrayBuffer(pContext, abArg)) { result = PROTO_NONE; break; }

    unsigned long abLen = getArrayBufferByteLength(pContext, abArg);
    long long bo = 0, bl = static_cast<long long>(abLen);
    if (argc > 1) {
        const proto::ProtoObject* a1 = argsList->getAt(pContext, 1);
        if (a1 && a1->isInteger(pContext)) bo = a1->asLong(pContext);
    }
    if (argc > 2) {
        const proto::ProtoObject* a2 = argsList->getAt(pContext, 2);
        if (a2 && a2->isInteger(pContext)) bl = a2->asLong(pContext);
        else bl = static_cast<long long>(abLen) - bo;
    } else {
        bl = static_cast<long long>(abLen) - bo;
    }

    // Validate range.
    if (bo < 0 || bo > static_cast<long long>(abLen) ||
        bl < 0 || bo + bl > static_cast<long long>(abLen)) {
        result = PROTO_NONE; break;
    }

    // Build DataView instance.
    const proto::ProtoObject* dvCtorObj =
        (*pGlobalRoot)->getAttribute(pContext, JSSymbols::DataView(pContext), true);
    const proto::ProtoObject* dvProtoObj = dvCtorObj
        ? dvCtorObj->getAttribute(pContext, JSSymbols::prototype(pContext), false)
        : nullptr;
    const proto::ProtoObject* dv = dvProtoObj ? dvProtoObj->newChild(pContext, true)
                                              : pContext->newObject(true);
    dv = dv->setAttribute(pContext, JSSymbols::dvBuffer(pContext), abArg);
    dv = dv->setAttribute(pContext, JSSymbols::dvByteOffset(pContext), pContext->fromInteger(bo));
    dv = dv->setAttribute(pContext, JSSymbols::dvByteLength(pContext), pContext->fromInteger(bl));
    result = dv;
}
```

Add `#include "../DataViewPrototype.h"` to ProtoInterpreter.cpp.

- [ ] **Step 4: Build, run DataView tests, commit**

```bash
cmake --build build --target protojs 2>&1 | tail -5
TEST262_PATTERNS="built-ins/DataView" node tests/test262/runner/test262_runner.js 2>/dev/null | tail -10
git add src/DataViewPrototype.cpp src/JSSymbols.h src/JSSymbols.cpp src/runtime/ProtoInterpreter.cpp
git commit -m "feat(dataview): implement DataView constructor and all get/set methods"
```

---

## Task 9: Update TEST262_STATUS.md + full Phase 8 conformance snapshot

**Files:**
- Modify: `docs/TEST262_STATUS.md`

- [ ] **Step 1: Run full Phase 8 test suite**

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS="built-ins/ArrayBuffer,built-ins/TypedArray,built-ins/TypedArrayConstructors,built-ins/DataView" \
    node tests/test262/runner/test262_runner.js 2>/dev/null | tail -20
```

- [ ] **Step 2: Update `docs/TEST262_STATUS.md`**

Add a new snapshot section at the top of the Phase 8 results with:
- Date
- Patterns run
- Pass/total/% for each area
- Root causes of remaining failures (run with `2>&1 | grep "FAIL"` to sample)

- [ ] **Step 3: Commit**

```bash
git add docs/TEST262_STATUS.md
git commit -m "docs(test262): Phase 8 snapshot — TypedArray + ArrayBuffer + DataView"
```

---

## Verification

After all 9 tasks, run the complete conformance check:

```bash
cd /home/gamarino/Documentos/proyectos/protoJS
TEST262_PATTERNS="built-ins/ArrayBuffer,built-ins/TypedArray,built-ins/TypedArrayConstructors,built-ins/DataView" \
    node tests/test262/runner/test262_runner.js 2>/dev/null | grep -E "passed|failed|total"
```

**Done criteria:**
- `built-ins/TypedArray`: ≥ 4,000 / 4,742 (84%+)
- `built-ins/TypedArrayConstructors`: ≥ 1,800 / 2,199 (82%+)
- `built-ins/ArrayBuffer`: ≥ 470 / 509 (92%+)
- `built-ins/DataView`: ≥ 1,300 / 1,558 (83%+)
- No previously-passing tests regressed (run `TEST262_PATTERNS="built-ins/Array,built-ins/RegExp,built-ins/Date"` to verify)
