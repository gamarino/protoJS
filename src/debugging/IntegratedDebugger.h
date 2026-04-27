#ifndef PROTOJS_INTEGRATEDDEBUGGER_H
#define PROTOJS_INTEGRATEDDEBUGGER_H

#include "headers/protoCore.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

namespace protojs {

class JSContextWrapper;

/**
 * @brief Chrome DevTools-protocol-flavoured debugger surface.
 *        Migrated to protoCore-native; CDP TCP listener and the
 *        breakpoint / call-stack bookkeeping stay unchanged.  The
 *        previously-stored JSValue scope chain in CallFrame is
 *        dropped (it was never actually populated).
 */
class IntegratedDebugger {
public:
    struct Breakpoint {
        std::string scriptId;
        int lineNumber;
        int columnNumber;
        std::string condition;
        bool enabled;
        std::string id;
    };

    struct CallFrame {
        std::string functionName;
        std::string scriptId;
        int lineNumber;
        int columnNumber;
    };

    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    static void pushFrame(proto::ProtoContext* pCtx);
    static void popFrame();
    static bool checkBreakpoint(const std::string& scriptId, int lineNumber);
    static void pauseExecution();

    // For the migrated `evaluate` ProtoMethod we need to reach the
    // active wrapper; set this at startup if the embedder wants
    // `debugger.evaluate(expr)` to work.
    static void setActiveWrapper(JSContextWrapper* w) { activeWrapper_ = w; }

    static std::vector<Breakpoint> breakpoints;
    static std::vector<CallFrame> callStack;
    static std::atomic<bool> serverRunning;
    static std::thread serverThread;
    static std::mutex breakpointsMutex;
    static std::mutex callStackMutex;
    static int nextBreakpointId;
    static JSContextWrapper* activeWrapper_;

    static void cdpServerThread(int port);
    static std::string processCDPRequest(const std::string& method,
                                          const std::string& params);
    static std::string generateCDPResponse(int id, const std::string& result);
    static std::string generateCDPError(int id, const std::string& error);
    static void resumeExecution();
};

} // namespace protojs

#endif // PROTOJS_INTEGRATEDDEBUGGER_H
