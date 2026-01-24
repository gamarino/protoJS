#ifndef PROTOJS_PATHMODULE_H
#define PROTOJS_PATHMODULE_H

#include "quickjs.h"

namespace protojs {

class PathModule {
public:
    static void init(JSContext* ctx);

private:
    static JSValue join(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue normalize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue dirname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue basename(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue extname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue isAbsolute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue relative(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_PATHMODULE_H
