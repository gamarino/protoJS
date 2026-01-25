#ifndef PROTOJS_UTILMODULE_H
#define PROTOJS_UTILMODULE_H

#include "quickjs.h"

namespace protojs {

class UtilModule {
public:
    static void init(JSContext* ctx);

private:
    static JSValue promisify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue typesIsArray(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue typesIsString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue typesIsNumber(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue typesIsObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue typesIsFunction(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue typesIsDate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue format(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_UTILMODULE_H
