#ifndef PROTOJS_FSMODULE_H
#define PROTOJS_FSMODULE_H

#include "quickjs.h"

namespace protojs {

class FSModule {
public:
    static void init(JSContext* ctx);

private:
    // Promises API
    static JSValue promisesReadFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue promisesWriteFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue promisesReaddir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue promisesMkdir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue promisesStat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_FSMODULE_H
