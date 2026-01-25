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
    
    // Sync API
    static JSValue readFileSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue writeFileSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue readdirSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue mkdirSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue statSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue unlinkSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue rmdirSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue renameSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue copyFileSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Stream API
    static JSValue createReadStream(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue createWriteStream(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_FSMODULE_H
