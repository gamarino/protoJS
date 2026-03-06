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
    /** Number of worker threads currently running (so main loop can wait for them). */
    static int getActiveWorkerCount();
    /** Runs the worker script in the current thread (protoCore path). Used by ProtoThread entry and by std::thread fallback. */
    static void workerThreadExecution(JSContext* mainCtx, const std::string& filename, const std::string& workerDataJson, JSValue workerObj);

private:
    // Worker class methods
    static JSValue WorkerConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue workerOn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue workerEmit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue workerPostMessage(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue workerTerminate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void WorkerFinalizer(JSRuntime* rt, JSValue val);
    
    // Module-level functions
    static JSValue isMainThread(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue parentPortGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue workerDataGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Helper functions (workerThreadExecution is public for ProtoThread entry)
    /** Sends a message to the main thread. Message must be serialized to JSON in the worker context before calling. */
    static void sendMessageToMainJson(JSContext* mainCtx, JSValue workerObj, const std::string& jsonMessage);
    /** Worker's parentPort.postMessage: serializes argv[0] to JSON and calls sendMessageToMainJson. */
    static JSValue workerParentPortPostMessage(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
};

} // namespace protojs

#endif // PROTOJS_WORKERTHREADSMODULE_H
