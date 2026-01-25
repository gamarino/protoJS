#ifndef PROTOJS_HTTPMODULE_H
#define PROTOJS_HTTPMODULE_H

#include "quickjs.h"
#include <string>
#include <map>

namespace protojs {

class HTTPModule {
public:
    static void init(JSContext* ctx);

private:
    // Server methods
    static JSValue createServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue serverListen(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue serverClose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void ServerFinalizer(JSRuntime* rt, JSValue val);
    
    // Request methods
    static JSValue request(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue requestWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue requestEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void RequestFinalizer(JSRuntime* rt, JSValue val);
    
    // Response methods
    static JSValue responseWriteHead(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue responseWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue responseEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void ResponseFinalizer(JSRuntime* rt, JSValue val);
    
    // IncomingMessage methods
    static JSValue incomingMessageGetHeader(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void IncomingMessageFinalizer(JSRuntime* rt, JSValue val);
    
    // Helper functions
    static void parseHeaders(const std::string& headerStr, std::map<std::string, std::string>& headers);
    static std::string formatHeaders(const std::map<std::string, std::string>& headers);
};

} // namespace protojs

#endif // PROTOJS_HTTPMODULE_H
