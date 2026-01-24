# Phase 3: Error Handling System Design

**Priority:** Critical  
**Timeline:** Month 5, Week 1-2  
**Dependencies:** All modules

---

## Overview

The Error Handling System provides standardized error handling across protoJS, with error types, stack traces, error codes, and recovery mechanisms. It ensures consistent error reporting and debugging capabilities.

---

## Architecture

### Design Principles

1. **Standardized Error Types:** Consistent error classes
2. **Rich Error Information:** Stack traces, context, codes
3. **Error Recovery:** Mechanisms for error recovery
4. **Integration:** Seamless integration with all modules

### Error Hierarchy

```
Error (base class)
├── SystemError
│   ├── EACCES
│   ├── ENOENT
│   ├── EADDRINUSE
│   └── ...
├── TypeError
├── RangeError
├── ReferenceError
├── SyntaxError
├── ModuleNotFoundError
└── NetworkError
```

---

## Implementation Details

### File Structure

```
src/
├── ErrorHandler.h
├── ErrorHandler.cpp
└── errors/
    ├── SystemError.h
    ├── SystemError.cpp
    ├── TypeError.h
    ├── TypeError.cpp
    └── ...
```

### Core API

```cpp
class ErrorHandler {
public:
    // Error creation
    static JSValue createError(JSContext* ctx, ErrorType type, const std::string& message, int code = 0);
    static JSValue createErrorWithStack(JSContext* ctx, ErrorType type, const std::string& message, const std::string& stack);
    static JSValue createSystemError(JSContext* ctx, int errno, const std::string& syscall, const std::string& path = "");
    
    // Context attachment
    static void attachContext(JSValue error, const std::string& file, int line, const std::string& function);
    static void attachStack(JSValue error, const std::string& stack);
    
    // Stack trace generation
    static std::string getStackTrace(JSContext* ctx);
    static std::string formatStackFrame(const std::string& file, int line, const std::string& function);
    
    // Error information
    static ErrorType getErrorType(JSValue error, JSContext* ctx);
    static std::string getErrorMessage(JSValue error, JSContext* ctx);
    static int getErrorCode(JSValue error, JSContext* ctx);
    static std::string getErrorStack(JSValue error, JSContext* ctx);
    
    // Error recovery
    static bool canRecover(JSValue error, JSContext* ctx);
    static JSValue recover(JSValue error, JSContext* ctx, const std::function<JSValue()>& recovery);
};
```

### Error Types

**SystemError:**
- Maps system errno to JavaScript errors
- Includes syscall and path information
- Common codes: EACCES, ENOENT, EADDRINUSE, etc.

**TypeError:**
- Invalid type operations
- Type mismatches
- Invalid arguments

**RangeError:**
- Out of range values
- Index out of bounds
- Invalid array length

**ReferenceError:**
- Undefined variables
- Invalid references
- Module not found

**SyntaxError:**
- Parse errors
- Invalid syntax
- Compilation errors

**ModuleNotFoundError:**
- Module resolution failures
- Package not found
- File not found

**NetworkError:**
- Connection errors
- Timeout errors
- Network failures

---

## Error Creation

### Standard Error Creation

```cpp
JSValue ErrorHandler::createError(JSContext* ctx, ErrorType type, const std::string& message, int code) {
    JSValue error = JS_NewError(ctx);
    
    // Set error properties
    JS_SetPropertyStr(ctx, error, "name", JS_NewString(ctx, getErrorTypeName(type).c_str()));
    JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, message.c_str()));
    JS_SetPropertyStr(ctx, error, "code", JS_NewInt32(ctx, code));
    JS_SetPropertyStr(ctx, error, "type", JS_NewString(ctx, getErrorTypeName(type).c_str()));
    
    // Generate stack trace
    std::string stack = getStackTrace(ctx);
    JS_SetPropertyStr(ctx, error, "stack", JS_NewString(ctx, stack.c_str()));
    
    return error;
}
```

### System Error Creation

```cpp
JSValue ErrorHandler::createSystemError(JSContext* ctx, int errno, const std::string& syscall, const std::string& path) {
    std::string message = syscall;
    if (!path.empty()) {
        message += " " + path;
    }
    message += ": " + getSystemErrorMessage(errno);
    
    JSValue error = createError(ctx, ErrorType::SystemError, message, errno);
    JS_SetPropertyStr(ctx, error, "syscall", JS_NewString(ctx, syscall.c_str()));
    JS_SetPropertyStr(ctx, error, "errno", JS_NewInt32(ctx, errno));
    if (!path.empty()) {
        JS_SetPropertyStr(ctx, error, "path", JS_NewString(ctx, path.c_str()));
    }
    
    return error;
}
```

---

## Stack Trace Generation

### Stack Trace Format

**Format:**
```
Error: message
    at functionName (file:line:column)
    at functionName (file:line:column)
    ...
```

**Implementation:**
```cpp
std::string ErrorHandler::getStackTrace(JSContext* ctx) {
    std::string stack;
    
    // Get call stack from QuickJS
    JSValue stackObj = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "Error");
    // Extract stack trace
    
    // Format stack frames
    for (auto& frame : frames) {
        stack += "    at " + frame.function + " (" + frame.file + ":" + 
                 std::to_string(frame.line) + ":" + std::to_string(frame.column) + ")\n";
    }
    
    return stack;
}
```

---

## Error Context

### Context Attachment

**Context Information:**
- File name
- Line number
- Column number
- Function name
- Module name

**Implementation:**
```cpp
void ErrorHandler::attachContext(JSValue error, const std::string& file, int line, const std::string& function) {
    JS_SetPropertyStr(ctx, error, "fileName", JS_NewString(ctx, file.c_str()));
    JS_SetPropertyStr(ctx, error, "lineNumber", JS_NewInt32(ctx, line));
    JS_SetPropertyStr(ctx, error, "functionName", JS_NewString(ctx, function.c_str()));
}
```

---

## Error Recovery

### Recovery Mechanisms

**Recovery Strategies:**
- Retry with backoff
- Fallback to default value
- Graceful degradation
- Error reporting and continuation

**Implementation:**
```cpp
JSValue ErrorHandler::recover(JSValue error, JSContext* ctx, const std::function<JSValue()>& recovery) {
    if (canRecover(error, ctx)) {
        try {
            return recovery();
        } catch (...) {
            // Recovery failed, rethrow original error
            return JS_Throw(ctx, error);
        }
    }
    return JS_Throw(ctx, error);
}
```

---

## Integration with Modules

### Module Error Handling

**FS Module:**
```cpp
JSValue FSModule::readFile(JSContext* ctx, ...) {
    try {
        // File operation
    } catch (const std::system_error& e) {
        return ErrorHandler::createSystemError(ctx, e.code().value(), "readFile", filePath);
    } catch (const std::exception& e) {
        return ErrorHandler::createError(ctx, ErrorType::SystemError, e.what());
    }
}
```

**Net Module:**
```cpp
JSValue NetModule::connect(JSContext* ctx, ...) {
    if (connectResult < 0) {
        return ErrorHandler::createSystemError(ctx, errno, "connect", host + ":" + std::to_string(port));
    }
}
```

**Module System:**
```cpp
JSValue ModuleResolver::resolve(JSContext* ctx, ...) {
    if (filePath.empty()) {
        return ErrorHandler::createError(ctx, ErrorType::ModuleNotFoundError, 
                                        "Cannot find module: " + specifier);
    }
}
```

---

## Error Codes

### System Error Codes

**POSIX Error Codes:**
- EACCES (13): Permission denied
- ENOENT (2): No such file or directory
- EADDRINUSE (98): Address already in use
- ECONNREFUSED (111): Connection refused
- ETIMEDOUT (110): Connection timed out
- EINVAL (22): Invalid argument

### Custom Error Codes

**Module System:**
- MODULE_NOT_FOUND (1000)
- MODULE_LOAD_ERROR (1001)
- CIRCULAR_DEPENDENCY (1002)

**Network:**
- CONNECTION_ERROR (2000)
- TIMEOUT_ERROR (2001)
- DNS_ERROR (2002)

---

## Testing Strategy

### Unit Tests
- Error creation
- Stack trace generation
- Context attachment
- Error recovery
- Error codes

### Integration Tests
- Error handling in modules
- Error propagation
- Error recovery scenarios
- Error reporting

---

## Dependencies

- **QuickJS:** Error objects, stack traces
- **All Modules:** Error integration
- **Logging System:** Error logging

---

## Success Criteria

1. ✅ All error types implemented
2. ✅ Stack traces generated correctly
3. ✅ Error codes mapped properly
4. ✅ Integration with all modules
5. ✅ Error recovery mechanisms working
6. ✅ Comprehensive error information

---

## Implementation Order

1. **Week 1:**
   - Error base classes
   - Standard error types
   - Stack trace generation
   - Error creation API

2. **Week 2:**
   - System error mapping
   - Error recovery
   - Integration with modules
   - Testing and validation

---

## Notes

- Error handling is critical for production use
- Rich error information aids debugging
- Consistent error format across all modules
- Stack traces essential for debugging
- Error recovery enables resilient applications
