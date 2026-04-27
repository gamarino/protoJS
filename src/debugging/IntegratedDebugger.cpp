#include "IntegratedDebugger.h"
#include "../ProtoNativeModule.h"
#include "../ArrayElementsStorage.h"
#include "../ArrayPrototype.h"
#include "../JSContext.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <string>
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
int IntegratedDebugger::nextBreakpointId = 1;
JSContextWrapper* IntegratedDebugger::activeWrapper_ = nullptr;

namespace {

bool argString(proto::ProtoContext* ctx, const proto::ProtoList* args,
                int idx, std::string& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isString(ctx)) return false;
    a->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

bool argInt(proto::ProtoContext* ctx, const proto::ProtoList* args,
             int idx, long long& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isInteger(ctx)) return false;
    out = a->asLong(ctx);
    return true;
}

const proto::ProtoObject* startCDPServerImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (IntegratedDebugger::serverRunning.load()) return PROTO_FALSE;
    long long port = 9229;
    argInt(ctx, args, 0, port);
    IntegratedDebugger::serverRunning.store(true);
    IntegratedDebugger::serverThread =
        std::thread(IntegratedDebugger::cdpServerThread, static_cast<int>(port));
    IntegratedDebugger::serverThread.detach();
    return PROTO_TRUE;
}

const proto::ProtoObject* stopCDPServerImpl(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    IntegratedDebugger::serverRunning.store(false);
    return PROTO_TRUE;
}

const proto::ProtoObject* setBreakpointImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string scriptId;
    long long line = 0;
    long long col = 0;
    if (!argString(ctx, args, 0, scriptId)) return PROTO_NONE;
    argInt(ctx, args, 1, line);
    argInt(ctx, args, 2, col);

    IntegratedDebugger::Breakpoint bp;
    bp.scriptId = scriptId;
    bp.lineNumber = static_cast<int>(line);
    bp.columnNumber = static_cast<int>(col);
    bp.enabled = true;
    {
        std::lock_guard<std::mutex> lock(IntegratedDebugger::breakpointsMutex);
        bp.id = std::to_string(IntegratedDebugger::nextBreakpointId++);
        IntegratedDebugger::breakpoints.push_back(bp);
    }
    return ctx->fromUTF8String(bp.id.c_str());
}

const proto::ProtoObject* removeBreakpointImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string id;
    if (!argString(ctx, args, 0, id)) return PROTO_FALSE;
    std::lock_guard<std::mutex> lock(IntegratedDebugger::breakpointsMutex);
    auto& bps = IntegratedDebugger::breakpoints;
    auto it = std::remove_if(bps.begin(), bps.end(),
        [&id](const IntegratedDebugger::Breakpoint& bp) { return bp.id == id; });
    bool found = (it != bps.end());
    if (found) bps.erase(it, bps.end());
    return found ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* getCallStackImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    std::lock_guard<std::mutex> lock(IntegratedDebugger::callStackMutex);
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    const proto::ProtoList* els = ctx->newList();
    for (const auto& f : IntegratedDebugger::callStack) {
        const proto::ProtoObject* frame = ctx->newObject(/*mutable=*/true);
        auto setStr = [&](const char* k, const std::string& v) {
            const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
            if (sk) frame->setAttribute(ctx, sk, ctx->fromUTF8String(v.c_str()));
        };
        auto setLong = [&](const char* k, long long v) {
            const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
            if (sk) frame->setAttribute(ctx, sk, ctx->fromInteger(v));
        };
        setStr("functionName", f.functionName);
        setStr("scriptId",     f.scriptId);
        setLong("lineNumber",   f.lineNumber);
        setLong("columnNumber", f.columnNumber);
        els = els->appendLast(ctx, frame);
    }
    setArrayElements(ctx, arr, els);
    return arr;
}

const proto::ProtoObject* evaluateImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string expr;
    if (!argString(ctx, args, 0, expr)) return PROTO_NONE;
    JSContextWrapper* wrapper = IntegratedDebugger::activeWrapper_;
    if (!wrapper) wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;
    // The eval path goes through QuickJS-side compilation but emits
    // bytecode that ProtoInterpreter executes — the result is a
    // QuickJS JSValue today.  For the protoCore-native binding
    // surface we surface a stringified result; richer round-trip
    // (returning the actual evaluated ProtoObject) is tracked
    // separately and would require exposing the protoCore eval
    // path directly from JSContextWrapper.
    JSValue r = wrapper->eval(expr, "<debugger.evaluate>");
    JSContext* qctx = wrapper->getJSContext();
    const char* s = JS_ToCString(qctx, r);
    const proto::ProtoObject* out = ctx->fromUTF8String(s ? s : "");
    if (s) JS_FreeCString(qctx, s);
    JS_FreeValue(qctx, r);
    return out;
}

const proto::ProtoObject* stepOrContinueImpl(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    IntegratedDebugger::resumeExecution();
    return PROTO_TRUE;
}

}  // namespace

void IntegratedDebugger::pushFrame(proto::ProtoContext* pCtx) {
    CallFrame frame;
    frame.functionName = "(global)";
    frame.scriptId = (pCtx && pCtx->currentFileName) ? pCtx->currentFileName : "";
    frame.lineNumber = pCtx ? pCtx->currentLineNumber : 0;
    frame.columnNumber = 0;
    std::lock_guard<std::mutex> lock(callStackMutex);
    callStack.push_back(frame);
}

void IntegratedDebugger::popFrame() {
    std::lock_guard<std::mutex> lock(callStackMutex);
    if (!callStack.empty()) callStack.pop_back();
}

bool IntegratedDebugger::checkBreakpoint(const std::string& scriptId,
                                          int lineNumber) {
    std::lock_guard<std::mutex> lock(breakpointsMutex);
    for (const auto& bp : breakpoints) {
        if (bp.enabled && bp.scriptId == scriptId && bp.lineNumber == lineNumber)
            return true;
    }
    return false;
}

void IntegratedDebugger::cdpServerThread(int port) {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) return;
    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0 || listen(serverSocket, 1) < 0) {
        close(serverSocket);
        return;
    }
    while (serverRunning.load()) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientSocket < 0) continue;
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

std::string IntegratedDebugger::processCDPRequest(const std::string&,
                                                    const std::string&) {
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

void IntegratedDebugger::pauseExecution() {}
void IntegratedDebugger::resumeExecution() {}

const proto::ProtoObject* IntegratedDebugger::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"startCDPServer",   startCDPServerImpl},
        {"stopCDPServer",    stopCDPServerImpl},
        {"setBreakpoint",    setBreakpointImpl},
        {"removeBreakpoint", removeBreakpointImpl},
        {"getCallStack",     getCallStackImpl},
        {"evaluate",         evaluateImpl},
        {"stepOver",         stepOrContinueImpl},
        {"stepInto",         stepOrContinueImpl},
        {"stepOut",          stepOrContinueImpl},
        {"continue",         stepOrContinueImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 10);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "debugger", mod);
}

} // namespace protojs
