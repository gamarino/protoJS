# Phase 3: Debugging Support Design

**Priority:** High  
**Timeline:** Month 7, Week 1-2  
**Dependencies:** Source maps, Error handling

---

## Overview

The Debugging Support provides Chrome DevTools Protocol (CDP) support for debugging protoJS applications, enabling breakpoints, variable inspection, call stack inspection, and step debugging.

---

## Architecture

### Design Principles

1. **Chrome DevTools Protocol:** Standard debugging protocol
2. **VS Code Integration:** Support for VS Code debugging
3. **Breakpoint Support:** Line and conditional breakpoints
4. **Variable Inspection:** Inspect variables and objects
5. **Step Debugging:** Step over, into, out

### Component Structure

```
Debugging Support
├── Inspector (CDP server)
├── Breakpoint Manager
├── Variable Inspector
├── Call Stack Manager
└── Source Map Integration
```

---

## Chrome DevTools Protocol

### Protocol Overview

**CDP Domains:**
- Runtime: Evaluate expressions, get properties
- Debugger: Breakpoints, stepping, call frames
- Console: Console messages
- Profiler: Performance profiling

### Inspector Implementation

**File Structure:**
```
src/debugging/
├── Inspector.h
├── Inspector.cpp
├── CDPServer.h
├── CDPServer.cpp
├── BreakpointManager.h
├── BreakpointManager.cpp
├── VariableInspector.h
└── VariableInspector.cpp
```

### CDP Server

**Implementation:**
```cpp
class CDPServer {
private:
    int serverSocket;
    std::thread serverThread;
    std::map<int, CDPConnection> connections;
    
public:
    void start(int port = 9229);
    void stop();
    void handleConnection(int clientSocket);
    void processMessage(int clientSocket, const std::string& message);
};
```

**Message Format:**
```json
{
  "id": 1,
  "method": "Debugger.setBreakpoint",
  "params": {
    "location": {
      "scriptId": "script1",
      "lineNumber": 10
    }
  }
}
```

---

## Breakpoint Support

### Breakpoint Types

**Line Breakpoints:**
- Break at specific line number
- Conditional breakpoints
- Logpoint (log without breaking)

**Implementation:**
```cpp
class BreakpointManager {
private:
    struct Breakpoint {
        std::string scriptId;
        int lineNumber;
        int columnNumber;
        std::string condition;
        bool enabled;
    };
    
    std::vector<Breakpoint> breakpoints;
    
public:
    int setBreakpoint(const std::string& scriptId, int lineNumber, const std::string& condition = "");
    void removeBreakpoint(int breakpointId);
    bool checkBreakpoint(const std::string& scriptId, int lineNumber);
    void pauseAtBreakpoint(const std::string& scriptId, int lineNumber);
};
```

### Breakpoint Integration

**QuickJS Integration:**
- Intercept bytecode execution
- Check breakpoints before instruction execution
- Pause execution when breakpoint hit
- Resume on continue command

---

## Variable Inspection

### Variable Access

**Implementation:**
```cpp
class VariableInspector {
public:
    struct Variable {
        std::string name;
        JSValue value;
        std::string type;
        std::string valueString;
        std::vector<Variable> properties;
    };
    
    static std::vector<Variable> getLocalVariables(JSContext* ctx);
    static std::vector<Variable> getScopeChain(JSContext* ctx);
    static Variable getVariable(JSContext* ctx, const std::string& name);
    static Variable getProperty(JSContext* ctx, JSValue object, const std::string& property);
    static std::string formatValue(JSContext* ctx, JSValue value);
};
```

### Object Inspection

**Object Properties:**
- Enumerate object properties
- Get property values
- Handle prototype chain
- Support for getters/setters

---

## Call Stack Inspection

### Call Stack

**Implementation:**
```cpp
struct CallFrame {
    std::string functionName;
    std::string scriptId;
    int lineNumber;
    int columnNumber;
    std::map<std::string, JSValue> localVariables;
    JSValue thisValue;
};

class CallStackManager {
public:
    static std::vector<CallFrame> getCallStack(JSContext* ctx);
    static CallFrame getCurrentFrame(JSContext* ctx);
    static void setCurrentFrame(JSContext* ctx, size_t frameIndex);
};
```

---

## Step Debugging

### Step Commands

**Step Over:**
- Execute current line
- Stop at next line
- Don't step into function calls

**Step Into:**
- Step into function calls
- Stop at first line of function

**Step Out:**
- Execute until function returns
- Stop at caller

**Continue:**
- Continue execution
- Stop at next breakpoint

**Implementation:**
```cpp
class StepManager {
private:
    enum class StepMode {
        NONE,
        OVER,
        INTO,
        OUT
    };
    
    StepMode currentMode;
    size_t targetFrameDepth;
    
public:
    void stepOver();
    void stepInto();
    void stepOut();
    void continueExecution();
    bool shouldPause(const std::string& scriptId, int lineNumber, size_t frameDepth);
};
```

---

## Source Map Integration

### Source Map Support

**Integration:**
- Map generated code to source code
- Map breakpoints to source locations
- Map stack traces to source locations
- Display source code in debugger

---

## VS Code Integration

### Launch Configuration

**`.vscode/launch.json`:**
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "node",
      "request": "launch",
      "name": "Debug protoJS",
      "program": "${workspaceFolder}/app.js",
      "runtimeExecutable": "${workspaceFolder}/build/protojs",
      "protocol": "inspector",
      "port": 9229
    }
  ]
}
```

---

## Testing Strategy

### Unit Tests
- Breakpoint setting/removal
- Variable inspection
- Call stack inspection
- Step commands

### Integration Tests
- Full debugging session
- VS Code integration
- Chrome DevTools integration
- Source map integration

---

## Dependencies

- **Source Maps:** Source mapping
- **Error Handling:** Error reporting in debugger
- **QuickJS:** Bytecode interception

---

## Success Criteria

1. ✅ Chrome DevTools Protocol support
2. ✅ Breakpoint support (line, conditional)
3. ✅ Variable inspection
4. ✅ Call stack inspection
5. ✅ Step debugging (over, into, out)
6. ✅ VS Code integration
7. ✅ Source map integration

---

## Implementation Order

1. **Week 1:**
   - CDP server implementation
   - Breakpoint manager
   - Basic debugging support

2. **Week 2:**
   - Variable inspection
   - Call stack inspection
   - Step debugging
   - VS Code integration
   - Testing and optimization

---

## Notes

- Debugging support essential for development
- CDP is standard protocol
- VS Code integration improves developer experience
- Source maps essential for debugging
- Performance impact should be minimal when not debugging
