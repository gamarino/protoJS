#ifndef PROTOJS_CONSOLE_H
#define PROTOJS_CONSOLE_H

#include "quickjs.h"

namespace protojs {

class Console {
public:
    static void init(JSContext* ctx);

private:
    static JSValue log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue error(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue warn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_CONSOLE_H
