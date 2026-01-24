#ifndef PROTOJS_URLMODULE_H
#define PROTOJS_URLMODULE_H
#include "quickjs.h"
namespace protojs {
class URLModule {
public:
    static void init(JSContext* ctx);
private:
    static JSValue URLConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue URLToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};
} // namespace protojs
#endif
