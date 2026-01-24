# Phase 3: Complete HTTP Module Design

**Priority:** High  
**Timeline:** Month 2, Week 1-2  
**Dependencies:** Net module, Stream module, Buffer module

---

## Overview

The HTTP module provides HTTP server and client functionality for protoJS, equivalent to Node.js `http` module. It builds on the Net module for TCP communication and implements HTTP protocol parsing and formatting.

---

## Architecture

### Design Principles

1. **Build on Net Module:** Use Net module for TCP communication
2. **Stream Support:** Use Stream module for request/response bodies
3. **Event-Driven:** EventEmitter-based architecture
4. **Node.js Compatibility:** Match Node.js HTTP API

### Component Structure

```
HTTP Module
├── Server (extends EventEmitter)
│   ├── HTTP Request Parser
│   ├── HTTP Response Formatter
│   ├── Request Handler
│   └── Connection Management
├── ClientRequest (extends EventEmitter)
│   ├── HTTP Request Formatter
│   ├── HTTP Response Parser
│   └── Connection Management
├── IncomingMessage (extends Readable Stream)
│   └── Request/Response Data
└── ServerResponse (extends Writable Stream)
    └── Response Data
```

---

## API Specification

### HTTP Server

#### `http.createServer(options, requestListener)`

Creates HTTP server.

**Options:**
- `IncomingMessage`: Custom IncomingMessage class
- `ServerResponse`: Custom ServerResponse class
- `keepAlive`: Enable keep-alive
- `keepAliveTimeout`: Keep-alive timeout
- `maxHeadersCount`: Maximum header count

**Implementation:**
```cpp
JSValue HTTPModule::createServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Parse options
    // Create Server object
    // Set up request listener
    // Return server object
}
```

#### `server.listen(port, host, backlog, callback)`

Starts HTTP server listening.

**Implementation:**
- Create Net server
- Handle HTTP protocol on connections
- Parse HTTP requests
- Emit 'request' events

#### `server.close(callback)`

Stops server from accepting connections.

### HTTP Client

#### `http.request(options, callback)`

Creates HTTP client request.

**Options:**
- `protocol`: 'http:' or 'https:'
- `host`: Server hostname
- `hostname`: Server hostname (alias)
- `port`: Server port
- `path`: Request path
- `method`: HTTP method (default: 'GET')
- `headers`: Request headers
- `timeout`: Request timeout

**Returns:** ClientRequest object

#### `http.get(options, callback)`

Convenience method for GET requests.

**Implementation:**
- Calls `http.request()` with method='GET'
- Automatically calls `req.end()`

### Request Object (IncomingMessage)

**Properties:**
- `request.method`: HTTP method
- `request.url`: Request URL
- `request.headers`: Request headers object
- `request.httpVersion`: HTTP version
- `request.socket`: Underlying socket

**Methods:**
- `request.setTimeout(timeout, callback)`
- Stream methods (inherited from Readable)

**Events:**
- `'data'`: Request body data
- `'end'`: Request complete
- `'error'`: Request error

### Response Object (ServerResponse)

**Properties:**
- `response.statusCode`: Status code
- `response.statusMessage`: Status message
- `response.headers`: Response headers object
- `response.socket`: Underlying socket

**Methods:**
- `response.writeHead(statusCode, statusMessage, headers)`
- `response.write(chunk, encoding, callback)`
- `response.end(chunk, encoding, callback)`
- `response.setHeader(name, value)`
- `response.getHeader(name)`
- `response.removeHeader(name)`
- `response.setTimeout(timeout, callback)`

**Events:**
- `'finish'`: Response sent
- `'error'`: Response error

---

## Implementation Details

### File Structure

```
src/modules/http/
├── HTTPModule.h
├── HTTPModule.cpp
├── HTTPServer.h
├── HTTPServer.cpp
├── HTTPClient.h
├── HTTPClient.cpp
├── HTTPParser.h
├── HTTPParser.cpp
├── IncomingMessage.h
├── IncomingMessage.cpp
├── ServerResponse.h
└── ServerResponse.cpp
```

### HTTP Parser

**Responsibilities:**
- Parse HTTP request line (method, path, version)
- Parse HTTP headers
- Parse HTTP response line (version, status, message)
- Handle chunked transfer encoding
- Handle content-length

**Implementation:**
```cpp
class HTTPParser {
public:
    struct ParseResult {
        std::string method;
        std::string path;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string body;
        bool complete;
    };
    
    static ParseResult parseRequest(const std::vector<uint8_t>& data);
    static ParseResult parseResponse(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> formatRequest(const std::string& method, 
                                               const std::string& path,
                                               const std::map<std::string, std::string>& headers,
                                               const std::vector<uint8_t>& body);
    static std::vector<uint8_t> formatResponse(int statusCode,
                                                const std::string& statusMessage,
                                                const std::map<std::string, std::string>& headers,
                                                const std::vector<uint8_t>& body);
};
```

### Server Implementation

**HTTPServer Class:**
```cpp
class HTTPServer : public EventEmitter {
private:
    NetServer* netServer;
    std::map<std::string, std::string> defaultHeaders;
    
public:
    void listen(int port, const std::string& host, int backlog);
    void handleConnection(NetSocket* socket);
    void parseRequest(NetSocket* socket, const std::vector<uint8_t>& data);
    void sendResponse(ServerResponse* response);
};
```

**Request Handling Flow:**
1. Net server accepts connection
2. Read HTTP request from socket
3. Parse request (method, path, headers, body)
4. Create IncomingMessage object
5. Create ServerResponse object
6. Emit 'request' event with both objects
7. User handler processes request
8. Send response via ServerResponse

### Client Implementation

**HTTPClient Class:**
```cpp
class HTTPClient {
public:
    static ClientRequest* request(const std::string& url, const std::map<std::string, std::string>& options);
};
```

**ClientRequest Class:**
```cpp
class ClientRequest : public EventEmitter {
private:
    NetSocket* socket;
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    
public:
    void write(const std::vector<uint8_t>& data);
    void end(const std::vector<uint8_t>& data);
    void abort();
};
```

**Request Flow:**
1. Create ClientRequest
2. Connect to server via Net module
3. Format HTTP request
4. Send request
5. Read response
6. Parse response
7. Emit 'response' event
8. Stream response body

### Stream Integration

**IncomingMessage (Readable Stream):**
- Extends Readable stream
- Request/response body as stream
- Backpressure handling
- Chunked transfer encoding support

**ServerResponse (Writable Stream):**
- Extends Writable stream
- Response body as stream
- Backpressure handling
- Automatic chunked encoding for large responses

---

## HTTP Protocol Support

### Request Methods
- GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS

### Headers
- Standard HTTP headers
- Custom headers
- Header parsing and formatting
- Case-insensitive header names

### Status Codes
- All standard HTTP status codes
- Custom status messages

### Transfer Encoding
- Content-Length
- Chunked transfer encoding
- Connection: keep-alive

---

## Error Handling

**Error Types:**
- `ECONNREFUSED`: Connection refused
- `ETIMEDOUT`: Request timeout
- `EPARSE`: HTTP parsing error
- `EINVALID`: Invalid HTTP format

**Error Handling:**
- Parse errors emit 'error' event
- Connection errors handled gracefully
- Timeout errors with callbacks

---

## Testing Strategy

### Unit Tests
- HTTP parsing (request/response)
- Server creation and listening
- Client request creation
- Header handling
- Error handling

### Integration Tests
- Full HTTP request/response cycle
- Multiple concurrent requests
- Large request/response bodies
- Chunked transfer encoding
- Keep-alive connections
- Error scenarios

### Performance Tests
- Request/response throughput
- Concurrent connections
- Large payload handling
- Comparison with Node.js

---

## Dependencies

- **Net Module:** TCP communication
- **Stream Module:** Request/response bodies
- **Buffer Module:** Binary data handling
- **Events Module:** EventEmitter base
- **IOThreadPool:** Async I/O operations

---

## Success Criteria

1. ✅ HTTP server creation and listening
2. ✅ HTTP client request/response
3. ✅ Request/response parsing
4. ✅ Header handling
5. ✅ Stream support for bodies
6. ✅ Error handling
7. ✅ Keep-alive support
8. ✅ Performance comparable to Node.js

---

## Implementation Order

1. **Week 1:**
   - HTTP parser (request/response)
   - Server structure
   - Basic request handling
   - Response formatting

2. **Week 2:**
   - Client implementation
   - Stream integration
   - Header handling
   - Error handling
   - Testing and optimization

---

## Notes

- Support HTTP/1.1 (HTTP/2 in future phase)
- Handle keep-alive connections efficiently
- Support chunked transfer encoding
- Efficient header parsing and formatting
- Stream support for large bodies
- Integration with URL module for request parsing
