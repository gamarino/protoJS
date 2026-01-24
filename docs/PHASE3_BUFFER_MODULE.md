# Phase 3: Buffer Module Design

**Priority:** Critical  
**Timeline:** Month 1, Week 1-2  
**Dependencies:** protoCore ProtoByteBuffer, TypeBridge

---

## Overview

The Buffer module provides efficient binary data handling in protoJS, equivalent to Node.js Buffer. It uses protoCore's ProtoByteBuffer for underlying storage, providing a TypedArray-like API for JavaScript.

---

## Architecture

### Design Principles

1. **Efficient Memory Management:** Use protoCore ProtoByteBuffer for storage, leveraging GC
2. **Node.js Compatibility:** Match Node.js Buffer API as closely as possible
3. **Performance:** Minimize conversions, use protoCore efficiently
4. **Integration:** Seamless integration with fs, net, http modules

### Internal Structure

```
Buffer (JavaScript Object)
    ↓
TypeBridge conversion
    ↓
ProtoByteBuffer (protoCore)
    ↓
Raw byte array (char*)
```

---

## API Specification

### Static Methods

#### `Buffer.from(array)`
Creates a Buffer from an array of bytes.

**Implementation:**
- Convert JS array to ProtoList
- Create ProtoByteBuffer from list
- Wrap in Buffer JS object

#### `Buffer.from(string, encoding)`
Creates a Buffer from a string with specified encoding.

**Supported encodings:**
- `'utf8'` (default)
- `'base64'`
- `'hex'`
- `'ascii'`
- `'latin1'`

**Implementation:**
- Decode string using encoding
- Create ProtoByteBuffer from bytes
- Wrap in Buffer JS object

#### `Buffer.alloc(size, fill, encoding)`
Creates a zero-filled Buffer of specified size.

**Parameters:**
- `size`: Number of bytes
- `fill`: Fill value (number, string, or Buffer)
- `encoding`: Encoding for string fill (default: 'utf8')

**Implementation:**
- Allocate ProtoByteBuffer of size
- Fill with specified value
- Wrap in Buffer JS object

#### `Buffer.concat(buffers, totalLength)`
Concatenates array of Buffers.

**Implementation:**
- Calculate total length
- Create new ProtoByteBuffer
- Copy all buffers into new buffer
- Wrap in Buffer JS object

#### `Buffer.isBuffer(obj)`
Checks if object is a Buffer instance.

**Implementation:**
- Check JS object class/type
- Verify it wraps ProtoByteBuffer

### Instance Methods

#### `buffer.toString(encoding, start, end)`
Converts buffer to string with encoding.

**Implementation:**
- Extract bytes from ProtoByteBuffer
- Encode using specified encoding
- Return JS string

#### `buffer.slice(start, end)`
Returns new Buffer referencing same memory.

**Implementation:**
- Create view into ProtoByteBuffer
- Wrap in new Buffer object
- Share underlying data (immutable)

#### `buffer.copy(target, targetStart, sourceStart, sourceEnd)`
Copies buffer data to target buffer.

**Implementation:**
- Get bytes from source ProtoByteBuffer
- Write to target ProtoByteBuffer
- Handle bounds checking

#### `buffer.fill(value, offset, end, encoding)`
Fills buffer with specified value.

**Implementation:**
- Convert value to bytes
- Write to ProtoByteBuffer
- Handle encoding for string values

#### `buffer.indexOf(value, byteOffset, encoding)`
Searches for value in buffer.

**Implementation:**
- Convert value to bytes
- Search in ProtoByteBuffer
- Return index or -1

#### `buffer.includes(value, byteOffset, encoding)`
Checks if buffer includes value.

**Implementation:**
- Use indexOf internally
- Return boolean

### Properties

- `buffer.length`: Number of bytes (read-only)
- `buffer.byteLength`: Same as length (read-only)

---

## Implementation Details

### File Structure

```
src/modules/buffer/
├── BufferModule.h
├── BufferModule.cpp
└── BufferEncoding.h (encoding utilities)
```

### Buffer Class Implementation

**JSClass Definition:**
```cpp
static JSClassID buffer_class_id;

struct BufferData {
    proto::ProtoByteBuffer* byteBuffer;
    proto::ProtoContext* pContext;
    JSRuntime* rt;
};
```

**Key Methods:**
- `BufferConstructor`: Create new Buffer
- `BufferToString`: Convert to string
- `BufferSlice`: Create slice
- `BufferCopy`: Copy to target
- `BufferFill`: Fill with value
- `BufferFinalizer`: Cleanup on GC

### Encoding Support

**Encoding Implementation:**
- UTF-8: Use standard library or iconv
- Base64: Use standard library or custom implementation
- Hex: Simple conversion (2 chars per byte)
- ASCII: Direct byte mapping
- Latin1: ISO-8859-1 encoding

**Encoding Utilities:**
```cpp
namespace BufferEncoding {
    std::vector<uint8_t> decode(const std::string& str, const std::string& encoding);
    std::string encode(const uint8_t* data, size_t length, const std::string& encoding);
}
```

### TypeBridge Integration

**JS → protoCore:**
- Buffer → ProtoByteBuffer (direct mapping)
- TypedArray → ProtoByteBuffer (copy bytes)
- Array → ProtoByteBuffer (convert to bytes)

**protoCore → JS:**
- ProtoByteBuffer → Buffer (wrap in JS object)
- ProtoByteBuffer → TypedArray (create TypedArray view)

### Memory Management

- Buffer objects hold reference to ProtoByteBuffer
- ProtoByteBuffer managed by protoCore GC
- No manual memory management needed
- Weak references for temporary buffers

---

## Integration Points

### FS Module
- `fs.readFile()` can return Buffer
- `fs.writeFile()` accepts Buffer
- File streams use Buffer for data

### Net Module
- Socket read/write uses Buffer
- Network data in binary format
- Efficient data transfer

### HTTP Module
- Request/response bodies as Buffer
- Binary content handling
- Efficient data streaming

---

## Testing Strategy

### Unit Tests
- Buffer creation (from array, string, alloc)
- Encoding/decoding operations
- Buffer operations (slice, copy, fill)
- Memory management (GC integration)

### Integration Tests
- Buffer with fs module
- Buffer with net module
- Buffer with http module
- Large buffer handling

### Performance Tests
- Buffer creation performance
- Encoding/decoding performance
- Memory usage benchmarks
- Comparison with Node.js Buffer

---

## Error Handling

- Invalid encoding: TypeError
- Invalid size: RangeError
- Out of bounds: RangeError
- Memory allocation failure: SystemError

---

## Dependencies

- protoCore: ProtoByteBuffer
- TypeBridge: Conversions
- Encoding libraries: For base64, hex, etc.
- Standard library: For UTF-8, ASCII

---

## Success Criteria

1. ✅ All Buffer API methods implemented
2. ✅ All encodings supported
3. ✅ Integration with fs, net, http modules
4. ✅ Performance comparable to Node.js
5. ✅ Memory management through GC
6. ✅ Comprehensive test coverage

---

## Implementation Order

1. **Week 1:**
   - Buffer class structure
   - Basic creation methods (from, alloc)
   - Encoding/decoding (utf8, hex, base64)
   - Basic operations (toString, slice)

2. **Week 2:**
   - Advanced operations (copy, fill, indexOf)
   - Integration with TypeBridge
   - Integration with fs module
   - Testing and optimization

---

## Notes

- Buffer shares memory when possible (slice creates view)
- Immutable buffers can be shared between threads safely
- Mutable buffers require synchronization
- Use protoCore GC for automatic memory management
