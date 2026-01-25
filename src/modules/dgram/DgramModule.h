#ifndef PROTOJS_DGRAMMODULE_H
#define PROTOJS_DGRAMMODULE_H

#include "quickjs.h"
#include <string>
#include <vector>

namespace protojs {

class DgramModule {
public:
    static void init(JSContext* ctx);

private:
    // Socket methods
    static JSValue createSocket(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketBind(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketClose(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketAddMembership(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketSetBroadcast(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue socketAddress(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void SocketFinalizer(JSRuntime* rt, JSValue val);
    
    // Helper functions
    static std::vector<uint8_t> getDataFromJSValue(JSContext* ctx, JSValueConst val);
};

} // namespace protojs

#endif // PROTOJS_DGRAMMODULE_H
