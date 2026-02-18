#ifndef PROTOJS_INTEGRATEDDEBUGGER_H
#define PROTOJS_INTEGRATEDDEBUGGER_H

#include "quickjs.h"
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

namespace protojs {

/**
 * @brief Integrated Debugger with Chrome DevTools Protocol support
 * 
 * Provides debugging capabilities including breakpoints, variable inspection,
 * call stack inspection, and step debugging.
 */
class IntegratedDebugger {
public:
    /**
     * @brief Breakpoint information
     */
    struct Breakpoint {
        std::string scriptId;
        int lineNumber;
        int columnNumber;
        std::string condition; // Optional condition
        bool enabled;
        std::string id;
    };
    
    /**
     * @brief Call frame information
     */
    struct CallFrame {
        std::string functionName;
        std::string scriptId;
        int lineNumber;
        int columnNumber;
        std::map<std::string, JSValue> scopeChain;
    };
    
    /**
     * @brief Initialize the debugger module
     */
    static void init(JSContext* ctx);
    
    /**
     * @brief Start the CDP server
     */
    static JSValue startCDPServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Stop the CDP server
     */
    static JSValue stopCDPServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Set a breakpoint
     */
    static JSValue setBreakpoint(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Remove a breakpoint
     */
    static JSValue removeBreakpoint(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Get call stack
     */
    static JSValue getCallStack(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Evaluate expression in current context
     */
    static JSValue evaluate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Step over (next line)
     */
    static JSValue stepOver(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Step into (enter function)
     */
    static JSValue stepInto(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Step out (exit function)
     */
    static JSValue stepOut(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Continue execution
     */
    static JSValue continueExecution(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    /**
     * @brief Push current execution frame (file/line from ProtoContext) for call stack.
     * Called by the runtime when entering script eval.
     */
    static void pushFrame(JSContext* ctx);

    /**
     * @brief Pop the top call stack frame. Called when leaving script eval.
     */
    static void popFrame();

    /**
     * @brief Check whether a breakpoint is set for the given script and line.
     * Used by the runtime to decide whether to pause (e.g. at script entry).
     */
    static bool checkBreakpoint(const std::string& scriptId, int lineNumber);

    /**
     * @brief Pause execution (e.g. when a breakpoint is hit). Called by the runtime.
     */
    static void pauseExecution();

private:
    static void cdpServerThread(int port);
    static void handleCDPMessage(const std::string& message);
    static std::string processCDPRequest(const std::string& method, const std::string& params);
    static std::string generateCDPResponse(int id, const std::string& result);
    static std::string generateCDPError(int id, const std::string& error);
    
    static std::vector<Breakpoint> breakpoints;
    static std::vector<CallFrame> callStack;
    static std::atomic<bool> serverRunning;
    static std::thread serverThread;
    static std::mutex breakpointsMutex;
    static std::mutex callStackMutex;
    static JSContext* debugContext;
    static int nextBreakpointId;

    static void resumeExecution();
};

} // namespace protojs

#endif // PROTOJS_INTEGRATEDDEBUGGER_H
