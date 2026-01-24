# Phase 3: Complete Stream Module Design

**Priority:** High  
**Timeline:** Month 2, Week 3-4  
**Dependencies:** Events module, Buffer module

---

## Overview

The Stream module provides streaming data handling for protoJS, equivalent to Node.js `stream` module. It implements Readable, Writable, Transform, and Duplex streams with backpressure handling and pipe functionality.

---

## Architecture

### Design Principles

1. **EventEmitter-Based:** All streams extend EventEmitter
2. **Backpressure Handling:** High/low watermarks for flow control
3. **Efficient Data Handling:** Use protoCore for data structures
4. **Node.js Compatibility:** Match Node.js stream API

### Stream Hierarchy

```
EventEmitter
    ↓
Stream (base class)
    ↓
    ├── Readable
    │   └── Transform (extends both Readable and Writable)
    ├── Writable
    └── Duplex (extends both Readable and Writable)
```

---

## API Specification

### Readable Stream

#### `new stream.Readable(options)`

Creates readable stream.

**Options:**
- `highWaterMark`: Buffer size (default: 16KB)
- `encoding`: Encoding for string data
- `objectMode`: Object mode (default: false)

#### `readable.read(size)`

Reads data from stream.

**Returns:** Buffer, string, or null

#### `readable.pipe(destination, options)`

Pipes data to writable stream.

**Options:**
- `end`: End destination when source ends (default: true)

**Returns:** Destination stream

#### `readable.on('data', callback)`

Event emitted when data available.

#### `readable.on('end', callback)`

Event emitted when stream ends.

#### `readable.on('error', callback)`

Event emitted on error.

**Internal Methods (to implement):**
- `_read(size)`: Read implementation
- `push(chunk, encoding)`: Push data to stream

### Writable Stream

#### `new stream.Writable(options)`

Creates writable stream.

**Options:**
- `highWaterMark`: Buffer size (default: 16KB)
- `encoding`: Encoding for string data
- `objectMode`: Object mode (default: false)

#### `writable.write(chunk, encoding, callback)`

Writes data to stream.

**Returns:** Boolean (false if should wait for 'drain')

#### `writable.end(chunk, encoding, callback)`

Ends stream writing.

#### `writable.on('drain', callback)`

Event emitted when buffer empty.

#### `writable.on('finish', callback)`

Event emitted when stream finished.

#### `writable.on('error', callback)`

Event emitted on error.

**Internal Methods (to implement):**
- `_write(chunk, encoding, callback)`: Write implementation
- `_final(callback)`: Finalization

### Transform Stream

#### `new stream.Transform(options)`

Creates transform stream.

**Extends:** Both Readable and Writable

**Internal Methods (to implement):**
- `_transform(chunk, encoding, callback)`: Transform data
- `_flush(callback)`: Flush remaining data

### Duplex Stream

#### `new stream.Duplex(options)`

Creates duplex stream.

**Extends:** Both Readable and Writable

**Options:**
- `readableObjectMode`: Object mode for readable side
- `writableObjectMode`: Object mode for writable side

---

## Implementation Details

### File Structure

```
src/modules/stream/
├── StreamModule.h
├── StreamModule.cpp
├── Readable.h
├── Readable.cpp
├── Writable.h
├── Writable.cpp
├── Transform.h
├── Transform.cpp
├── Duplex.h
└── Duplex.cpp
```

### Stream Base Class

**Stream Class:**
```cpp
class Stream : public EventEmitter {
protected:
    bool destroyed;
    bool readable;
    bool writable;
    
public:
    virtual void destroy();
    bool isDestroyed() const;
};
```

### Readable Stream Implementation

**Readable Class:**
```cpp
class Readable : public Stream {
private:
    std::queue<std::vector<uint8_t>> buffer;
    size_t highWaterMark;
    bool reading;
    bool ended;
    std::string encoding;
    
public:
    std::vector<uint8_t> read(size_t size);
    Readable* pipe(Writable* destination, const std::map<std::string, JSValue>& options);
    void push(const std::vector<uint8_t>& chunk, const std::string& encoding);
    void pushNull();
    
protected:
    virtual void _read(size_t size) = 0;
};
```

**Backpressure:**
- When buffer size > highWaterMark, pause reading
- When buffer size < highWaterMark, resume reading
- Emit 'drain' event when buffer empty

### Writable Stream Implementation

**Writable Class:**
```cpp
class Writable : public Stream {
private:
    std::queue<std::vector<uint8_t>> buffer;
    size_t highWaterMark;
    bool writing;
    bool ended;
    std::string encoding;
    
public:
    bool write(const std::vector<uint8_t>& chunk, const std::string& encoding);
    void end(const std::vector<uint8_t>& chunk, const std::string& encoding);
    
protected:
    virtual void _write(const std::vector<uint8_t>& chunk, const std::string& encoding, std::function<void()> callback) = 0;
    virtual void _final(std::function<void()> callback);
};
```

**Backpressure:**
- When buffer size > highWaterMark, return false from write()
- Wait for 'drain' event before writing more
- Emit 'drain' when buffer size < highWaterMark

### Transform Stream Implementation

**Transform Class:**
```cpp
class Transform : public Readable, public Writable {
protected:
    virtual void _transform(const std::vector<uint8_t>& chunk, 
                          const std::string& encoding, 
                          std::function<void()> callback) = 0;
    virtual void _flush(std::function<void()> callback);
    
public:
    // Inherits from both Readable and Writable
};
```

**Transform Flow:**
1. Data written to Transform
2. `_transform()` called with chunk
3. Transform processes data
4. Push transformed data to readable side
5. Call callback when done

### Duplex Stream Implementation

**Duplex Class:**
```cpp
class Duplex : public Readable, public Writable {
    // Independent readable and writable sides
    // Can have different highWaterMark for each side
};
```

### Pipe Implementation

**Pipe Functionality:**
```cpp
Readable* Readable::pipe(Writable* destination, const std::map<std::string, JSValue>& options) {
    // Set up data flow
    this->on('data', [destination](const std::vector<uint8_t>& chunk) {
        if (!destination->write(chunk)) {
            this->pause(); // Backpressure
        }
    });
    
    destination->on('drain', [this]() {
        this->resume(); // Resume on drain
    });
    
    this->on('end', [destination, options]() {
        if (options["end"] != false) {
            destination->end();
        }
    });
    
    return destination;
}
```

---

## Integration Points

### FS Module
- `fs.createReadStream()` → Readable stream
- `fs.createWriteStream()` → Writable stream
- File reading/writing as streams

### Net Module
- Socket as Duplex stream
- Network data as streams

### HTTP Module
- Request body as Readable stream
- Response body as Writable stream
- Efficient data handling

---

## Backpressure Handling

### Readable Stream
- Pause when buffer full
- Resume when buffer drained
- Automatic flow control

### Writable Stream
- Return false when buffer full
- Wait for 'drain' event
- Prevent memory overflow

### Pipe Backpressure
- Automatic backpressure in pipe()
- Pause source when destination full
- Resume when destination ready

---

## Testing Strategy

### Unit Tests
- Stream creation
- Read/write operations
- Backpressure handling
- Pipe functionality
- Transform operations
- Error handling

### Integration Tests
- Stream with fs module
- Stream with net module
- Stream with http module
- Large data streaming
- Multiple streams

### Performance Tests
- Throughput benchmarks
- Memory usage
- Backpressure efficiency
- Comparison with Node.js

---

## Dependencies

- **Events Module:** EventEmitter base class
- **Buffer Module:** Binary data handling
- **protoCore:** Efficient data structures

---

## Success Criteria

1. ✅ All stream types implemented
2. ✅ Backpressure handling
3. ✅ Pipe functionality
4. ✅ Integration with fs, net, http
5. ✅ Performance comparable to Node.js
6. ✅ Memory efficient

---

## Implementation Order

1. **Week 3:**
   - Stream base class
   - Readable stream
   - Writable stream
   - Basic operations

2. **Week 4:**
   - Transform stream
   - Duplex stream
   - Pipe functionality
   - Backpressure handling
   - Integration with other modules
   - Testing and optimization

---

## Notes

- Use protoCore data structures for efficient buffering
- Implement proper backpressure to prevent memory issues
- Support object mode for non-binary data
- Efficient pipe implementation for data flow
- Handle errors gracefully in all stream operations
