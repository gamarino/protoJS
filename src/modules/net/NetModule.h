#ifndef PROTOJS_NETMODULE_H
#define PROTOJS_NETMODULE_H

#include "quickjs.h"
#include <vector>
#include <string>

namespace protojs {

class NetModule {
public:
    static void init(JSContext* ctx);

private:
    // Server methods
    static JSValue createServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue serverListen(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue serverClose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue serverAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void ServerFinalizer(JSRuntime* rt, JSValue val);
    
    // Socket methods
    static JSValue createConnection(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketConnect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketWrite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketEnd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketDestroy(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void SocketFinalizer(JSRuntime* rt, JSValue val);
    
    // Helper functions
    static std::vector<uint8_t> getDataFromJSValue(JSContext* ctx, JSValueConst val, const char* encoding = "utf8");
};

} // namespace protojs

#endif // PROTOJS_NETMODULE_H
