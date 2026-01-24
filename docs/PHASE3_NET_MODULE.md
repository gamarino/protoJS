# Phase 3: Net Module Design

**Priority:** High  
**Timeline:** Month 1, Week 3-4  
**Dependencies:** Buffer module, Events module, IOThreadPool, EventLoop

---

## Overview

The Net module provides TCP networking capabilities for protoJS, equivalent to Node.js `net` module. It implements TCP sockets and servers using an event-driven architecture with async I/O through IOThreadPool.

---

## Architecture

### Design Principles

1. **Event-Driven:** Use EventEmitter for all events
2. **Async I/O:** All socket operations use IOThreadPool
3. **Node.js Compatibility:** Match Node.js net API
4. **Performance:** Efficient data handling with Buffer

### Component Structure

```
Net Module
├── Server (extends EventEmitter)
│   ├── TCP Server Socket
│   ├── Connection Handling
│   └── Event Emission
└── Socket (extends EventEmitter)
    ├── TCP Socket
    ├── Read/Write Operations
    └── Connection Management
```

---

## API Specification

### Server API

#### `net.createServer(options, connectionListener)`

Creates a TCP server.

**Options:**
- `allowHalfOpen`: Allow half-open connections
- `pauseOnConnect`: Pause socket on connection

**Implementation:**
```cpp
JSValue NetModule::createServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Parse options
    // Create Server object (extends EventEmitter)
    // Set up connection listener
    // Return server object
}
```

#### `server.listen(port, host, backlog, callback)`

Starts server listening for connections.

**Implementation:**
- Create TCP socket (socket, bind, listen)
- Execute in IOThreadPool
- Emit 'listening' event when ready
- Accept connections in IOThreadPool

#### `server.close(callback)`

Stops server from accepting new connections.

#### `server.address()`

Returns bound address and port.

### Socket API

#### `net.createConnection(options, callback)`

Creates TCP client connection.

**Options:**
- `port`: Port number (required)
- `host`: Hostname (default: 'localhost')
- `family`: IP family (4 or 6, default: 4)
- `timeout`: Connection timeout

**Implementation:**
- Create Socket object
- Connect in IOThreadPool
- Emit 'connect' event when connected

#### `socket.connect(port, host, connectListener)`

Connects socket to server.

#### `socket.write(data, encoding, callback)`

Writes data to socket.

**Data types:**
- Buffer
- String (with encoding)
- TypedArray

**Implementation:**
- Convert data to bytes
- Write to socket in IOThreadPool
- Emit 'drain' when buffer empty
- Call callback when written

#### `socket.end(data, encoding, callback)`

Half-closes socket connection.

#### `socket.destroy()`

Destroys socket immediately.

#### `socket.address()`

Returns local address and port.

#### Socket Properties

- `socket.remoteAddress`: Remote IP address
- `socket.remotePort`: Remote port number
- `socket.localAddress`: Local IP address
- `socket.localPort`: Local port number
- `socket.readyState`: Connection state

### Events

#### Server Events
- `'listening'`: Server started listening
- `'connection'`: New client connection
- `'close'`: Server closed
- `'error'`: Server error

#### Socket Events
- `'connect'`: Socket connected
- `'data'`: Data received
- `'end'`: Remote end closed
- `'error'`: Socket error
- `'close'`: Socket closed
- `'timeout'`: Connection timeout
- `'drain'`: Write buffer empty

---

## Implementation Details

### File Structure

```
src/modules/net/
├── NetModule.h
├── NetModule.cpp
├── Socket.h
├── Socket.cpp
├── Server.h
└── Server.cpp
```

### Socket Implementation

**Socket Class:**
```cpp
class Socket : public EventEmitter {
private:
    int socketFd;
    bool connected;
    bool destroyed;
    std::string remoteAddress;
    int remotePort;
    std::string localAddress;
    int localPort;
    
    // Read buffer
    std::vector<uint8_t> readBuffer;
    
    // Write queue
    std::queue<std::vector<uint8_t>> writeQueue;
    
public:
    void connect(int port, const std::string& host);
    void write(const std::vector<uint8_t>& data);
    void end();
    void destroy();
};
```

**Socket Operations:**
- All socket operations (connect, read, write, close) execute in IOThreadPool
- Read operations: Poll socket, read data, emit 'data' event
- Write operations: Queue data, write in IOThreadPool, emit 'drain' when done
- Connection: Create socket, connect, emit 'connect' event

### Server Implementation

**Server Class:**
```cpp
class Server : public EventEmitter {
private:
    int serverFd;
    bool listening;
    int port;
    std::string host;
    
public:
    void listen(int port, const std::string& host, int backlog);
    void close();
    void acceptConnection(); // Called in IOThreadPool
};
```

**Server Operations:**
- Listen: Create socket, bind, listen in IOThreadPool
- Accept: Accept connections in IOThreadPool, create Socket, emit 'connection'
- Close: Stop accepting, close server socket

### Platform-Specific Code

**POSIX Sockets:**
- `socket()`: Create socket
- `bind()`: Bind to address
- `listen()`: Start listening
- `accept()`: Accept connection
- `connect()`: Connect to server
- `send()`: Send data
- `recv()`: Receive data
- `close()`: Close socket

**Error Handling:**
- Map errno to JavaScript errors
- Emit 'error' events
- Handle connection errors gracefully

### Buffer Integration

- Read data into Buffer objects
- Write Buffer objects to socket
- Efficient binary data handling
- Support for string encoding

### Event Loop Integration

- Socket events emitted on main thread
- Callbacks executed via EventLoop
- Thread-safe event emission

---

## Threading Model

### I/O Operations

All socket I/O operations execute in IOThreadPool:
- `connect()`: Blocking connect in I/O thread
- `read()`: Blocking read in I/O thread
- `write()`: Blocking write in I/O thread
- `accept()`: Blocking accept in I/O thread

### Event Emission

Events emitted on main thread:
- After I/O operation completes
- Via EventLoop callback
- Thread-safe with JSContext

---

## Error Handling

**Error Types:**
- `ECONNREFUSED`: Connection refused
- `ETIMEDOUT`: Connection timeout
- `ENOTFOUND`: Host not found
- `EADDRINUSE`: Address already in use
- `EACCES`: Permission denied

**Error Objects:**
- SystemError with errno code
- Error message from system
- Stack trace

---

## Testing Strategy

### Unit Tests
- Socket creation and connection
- Server creation and listening
- Data transmission
- Error handling
- Event emission

### Integration Tests
- Client-server communication
- Multiple connections
- Large data transfer
- Connection lifecycle
- Error scenarios

### Performance Tests
- Throughput benchmarks
- Latency measurements
- Concurrent connections
- Comparison with Node.js

---

## Dependencies

- **Buffer Module:** For binary data
- **Events Module:** EventEmitter base class
- **IOThreadPool:** For async I/O
- **EventLoop:** For callback execution
- **Platform Sockets:** POSIX sockets API

---

## Success Criteria

1. ✅ Server creation and listening
2. ✅ Client connection
3. ✅ Data transmission (read/write)
4. ✅ Event emission (connect, data, end, error, close)
5. ✅ Error handling
6. ✅ Integration with HTTP module
7. ✅ Performance comparable to Node.js

---

## Implementation Order

1. **Week 3:**
   - Socket class structure
   - Basic connection (connect, close)
   - Basic I/O (read, write)
   - Event emission

2. **Week 4:**
   - Server class
   - Connection acceptance
   - Error handling
   - Integration with HTTP module
   - Testing and optimization

---

## Notes

- Use non-blocking sockets with select/poll for better performance (future optimization)
- Support IPv4 and IPv6
- Handle half-open connections
- Support connection timeouts
- Efficient buffer management for read/write operations
