#ifndef PROTOJS_HTTPMODULE_H
#define PROTOJS_HTTPMODULE_H
#include "quickjs.h"
namespace protojs {
class HTTPModule {
public:
    static void init(JSContext* ctx);
private:
    static JSValue createServer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue request(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};
} // namespace protojs
#endif
