#ifndef PROTOJS_WORKERTHREADSMODULE_H
#define PROTOJS_WORKERTHREADSMODULE_H

#include "quickjs.h"
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

namespace protojs {

class WorkerThreadsModule {
public:
    static void init(JSContext* ctx);

private:
    // Worker class methods
    static JSValue WorkerConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue workerPostMessage(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue workerTerminate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void WorkerFinalizer(JSRuntime* rt, JSValue val);
    
    // Module-level functions
    static JSValue isMainThread(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue parentPortGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue workerDataGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Helper functions
    static void workerThreadExecution(JSContext* mainCtx, const std::string& filename, JSValue workerData, JSValue workerObj);
    static void sendMessageToMain(JSContext* mainCtx, JSValue workerObj, JSValue message);
};

} // namespace protojs

#endif // PROTOJS_WORKERTHREADSMODULE_H
