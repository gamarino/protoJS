#include "IntegratedDebugger.h"
#include <sstream>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace protojs {

std::vector<IntegratedDebugger::Breakpoint> IntegratedDebugger::breakpoints;
std::vector<IntegratedDebugger::CallFrame> IntegratedDebugger::callStack;
std::atomic<bool> IntegratedDebugger::serverRunning(false);
std::thread IntegratedDebugger::serverThread;
std::mutex IntegratedDebugger::breakpointsMutex;
std::mutex IntegratedDebugger::callStackMutex;
JSContext* IntegratedDebugger::debugContext = nullptr;
int IntegratedDebugger::nextBreakpointId = 1;

void IntegratedDebugger::init(JSContext* ctx) {
    debugContext = ctx;
    
    JSValue debugger = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, debugger, "startCDPServer", JS_NewCFunction(ctx, startCDPServer, "startCDPServer", 1));
    JS_SetPropertyStr(ctx, debugger, "stopCDPServer", JS_NewCFunction(ctx, stopCDPServer, "stopCDPServer", 0));
    JS_SetPropertyStr(ctx, debugger, "setBreakpoint", JS_NewCFunction(ctx, setBreakpoint, "setBreakpoint", 3));
    JS_SetPropertyStr(ctx, debugger, "removeBreakpoint", JS_NewCFunction(ctx, removeBreakpoint, "removeBreakpoint", 1));
    JS_SetPropertyStr(ctx, debugger, "getCallStack", JS_NewCFunction(ctx, getCallStack, "getCallStack", 0));
    JS_SetPropertyStr(ctx, debugger, "evaluate", JS_NewCFunction(ctx, evaluate, "evaluate", 1));
    JS_SetPropertyStr(ctx, debugger, "stepOver", JS_NewCFunction(ctx, stepOver, "stepOver", 0));
    JS_SetPropertyStr(ctx, debugger, "stepInto", JS_NewCFunction(ctx, stepInto, "stepInto", 0));
    JS_SetPropertyStr(ctx, debugger, "stepOut", JS_NewCFunction(ctx, stepOut, "stepOut", 0));
    JS_SetPropertyStr(ctx, debugger, "continue", JS_NewCFunction(ctx, continueExecution, "continue", 0));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "debugger", debugger);
    JS_FreeValue(ctx, global_obj);
}

JSValue IntegratedDebugger::startCDPServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (serverRunning.load()) {
        return JS_NewBool(ctx, false);
    }
    
    int port = 9229; // Default CDP port
    if (argc > 0) {
        int64_t portVal;
        if (JS_ToInt64(ctx, &portVal, argv[0]) >= 0) {
            port = static_cast<int>(portVal);
        }
    }
    
    serverRunning.store(true);
    serverThread = std::thread(cdpServerThread, port);
    
    return JS_NewBool(ctx, true);
}

JSValue IntegratedDebugger::stopCDPServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!serverRunning.load()) {
        return JS_NewBool(ctx, false);
    }
    
    serverRunning.store(false);
    if (serverThread.joinable()) {
        serverThread.join();
    }
    
    return JS_NewBool(ctx, true);
}

JSValue IntegratedDebugger::setBreakpoint(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "setBreakpoint expects scriptId and lineNumber");
    }
    
    const char* scriptId = JS_ToCString(ctx, argv[0]);
    if (!scriptId) return JS_EXCEPTION;
    
    int64_t lineNumber;
    if (JS_ToInt64(ctx, &lineNumber, argv[1]) < 0) {
        JS_FreeCString(ctx, scriptId);
        return JS_EXCEPTION;
    }
    
    std::string condition;
    if (argc > 2) {
        const char* cond = JS_ToCString(ctx, argv[2]);
        if (cond) {
            condition = cond;
            JS_FreeCString(ctx, cond);
        }
    }
    
    std::lock_guard<std::mutex> lock(breakpointsMutex);
    
    Breakpoint bp;
    bp.scriptId = scriptId;
    bp.lineNumber = static_cast<int>(lineNumber);
    bp.columnNumber = 0;
    bp.condition = condition;
    bp.enabled = true;
    bp.id = "bp" + std::to_string(nextBreakpointId++);
    
    breakpoints.push_back(bp);
    
    JS_FreeCString(ctx, scriptId);
    
    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "id", JS_NewString(ctx, bp.id.c_str()));
    JS_SetPropertyStr(ctx, result, "lineNumber", JS_NewInt64(ctx, bp.lineNumber));
    
    return result;
}

JSValue IntegratedDebugger::removeBreakpoint(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "removeBreakpoint expects breakpoint id");
    }
    
    const char* id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_EXCEPTION;
    
    std::lock_guard<std::mutex> lock(breakpointsMutex);
    
    auto it = std::remove_if(breakpoints.begin(), breakpoints.end(),
        [id](const Breakpoint& bp) { return bp.id == id; });
    
    bool found = (it != breakpoints.end());
    if (found) {
        breakpoints.erase(it, breakpoints.end());
    }
    
    JS_FreeCString(ctx, id);
    
    return JS_NewBool(ctx, found);
}

JSValue IntegratedDebugger::getCallStack(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::lock_guard<std::mutex> lock(callStackMutex);
    
    JSValue stack = JS_NewArray(ctx);
    
    for (size_t i = 0; i < callStack.size(); i++) {
        JSValue frame = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, frame, "functionName", JS_NewString(ctx, callStack[i].functionName.c_str()));
        JS_SetPropertyStr(ctx, frame, "scriptId", JS_NewString(ctx, callStack[i].scriptId.c_str()));
        JS_SetPropertyStr(ctx, frame, "lineNumber", JS_NewInt64(ctx, callStack[i].lineNumber));
        JS_SetPropertyStr(ctx, frame, "columnNumber", JS_NewInt64(ctx, callStack[i].columnNumber));
        
        JS_SetPropertyUint32(ctx, stack, i, frame);
    }
    
    return stack;
}

JSValue IntegratedDebugger::evaluate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "evaluate expects expression");
    }
    
    const char* expression = JS_ToCString(ctx, argv[0]);
    if (!expression) return JS_EXCEPTION;
    
    JSValue result = JS_Eval(ctx, expression, strlen(expression), "<eval>", JS_EVAL_TYPE_GLOBAL);
    
    JS_FreeCString(ctx, expression);
    
    return result;
}

JSValue IntegratedDebugger::stepOver(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    resumeExecution();
    return JS_NewBool(ctx, true);
}

JSValue IntegratedDebugger::stepInto(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    resumeExecution();
    return JS_NewBool(ctx, true);
}

JSValue IntegratedDebugger::stepOut(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    resumeExecution();
    return JS_NewBool(ctx, true);
}

JSValue IntegratedDebugger::continueExecution(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    resumeExecution();
    return JS_NewBool(ctx, true);
}

void IntegratedDebugger::cdpServerThread(int port) {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return;
    }
    
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port " << port << std::endl;
        close(serverSocket);
        return;
    }
    
    if (listen(serverSocket, 1) < 0) {
        std::cerr << "Failed to listen" << std::endl;
        close(serverSocket);
        return;
    }
    
    std::cout << "CDP Server listening on port " << port << std::endl;
    
    while (serverRunning.load()) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientLen);
        
        if (clientSocket < 0) {
            if (serverRunning.load()) {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }
        
        char buffer[4096];
        ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string message(buffer);
            std::string response = processCDPRequest("", message);
            send(clientSocket, response.c_str(), response.length(), 0);
        }
        
        close(clientSocket);
    }
    
    close(serverSocket);
}

void IntegratedDebugger::handleCDPMessage(const std::string& message) {
    // Parse JSON message and route to appropriate handler
    // This is a simplified implementation
    processCDPRequest("", message);
}

std::string IntegratedDebugger::processCDPRequest(const std::string& method, const std::string& params) {
    // Simplified CDP request processing
    // In a full implementation, this would parse JSON and route to appropriate handlers
    return generateCDPResponse(1, "{}");
}

std::string IntegratedDebugger::generateCDPResponse(int id, const std::string& result) {
    std::stringstream ss;
    ss << "{\"id\":" << id << ",\"result\":" << result << "}";
    return ss.str();
}

std::string IntegratedDebugger::generateCDPError(int id, const std::string& error) {
    std::stringstream ss;
    ss << "{\"id\":" << id << ",\"error\":{\"message\":\"" << error << "\"}}";
    return ss.str();
}

bool IntegratedDebugger::checkBreakpoint(const std::string& scriptId, int lineNumber) {
    std::lock_guard<std::mutex> lock(breakpointsMutex);
    
    for (const auto& bp : breakpoints) {
        if (bp.enabled && bp.scriptId == scriptId && bp.lineNumber == lineNumber) {
            return true;
        }
    }
    
    return false;
}

void IntegratedDebugger::pauseExecution() {
    // Implementation would pause execution at breakpoint
    // This is a placeholder
}

void IntegratedDebugger::resumeExecution() {
    // Implementation would resume execution
    // This is a placeholder
}

} // namespace protojs
