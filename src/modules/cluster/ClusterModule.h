#ifndef PROTOJS_CLUSTERMODULE_H
#define PROTOJS_CLUSTERMODULE_H

#include "quickjs.h"
#include <string>
#include <vector>
#include <map>
#include <sys/types.h>

namespace protojs {

class ClusterModule {
public:
    static void init(JSContext* ctx);

private:
    // Cluster methods
    static JSValue setupMaster(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue fork(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue isMasterGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue isWorkerGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Worker methods
    static JSValue workerSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue workerDisconnect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue workerKill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void WorkerFinalizer(JSRuntime* rt, JSValue val);
    
    // Helper functions
    static void workerProcessExecution(JSContext* mainCtx, JSValue workerObj, int workerId, int ipcFd);
    static void sendMessageToWorker(JSContext* mainCtx, JSValue workerObj, JSValue message);
};

} // namespace protojs

#endif // PROTOJS_CLUSTERMODULE_H
