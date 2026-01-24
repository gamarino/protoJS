#ifndef PROTOJS_CRYPTOMODULE_H
#define PROTOJS_CRYPTOMODULE_H
#include "quickjs.h"
namespace protojs {
class CryptoModule {
public:
    static void init(JSContext* ctx);
private:
    static JSValue createHash(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue randomBytes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};
} // namespace protojs
#endif
