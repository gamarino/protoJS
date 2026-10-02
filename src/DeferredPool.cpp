#include "DeferredPool.h"

#include "JSContext.h"
#include "MicrotaskQueue.h"
#include "PromisePrototype.h"
#include "ProtoDeferred.h"
#include "ThreadProtoContext.h"
#include "runtime/ProtoBytecodeModule.h"
#include "runtime/ProtoInterpreter.h"

#include "quickjs.h"

#include <iostream>

namespace protojs {

DeferredPool::DeferredPool(JSContextWrapper* owner, std::size_t threads)
    : owner_(owner), wanted_(threads > 0 ? threads : 1) {}

DeferredPool::~DeferredPool() {
    shutdown();
}

bool DeferredPool::start(proto::ProtoContext* ctx) {
    startAttempted_ = true;
    proto::ProtoSpace* space = ctx ? ctx->space : nullptr;
    proto::ProtoRootSet* rs = owner_ ? owner_->getRootSet() : nullptr;
    if (!space || !rs) return false;
    const proto::ProtoString* name = proto::ProtoString::createSymbol(ctx, "deferred-pool");
    for (std::size_t i = 0; i < wanted_; ++i) {
        // The pool reaches the thread through an external pointer argument;
        // the argument list is kept alive by the thread object.
        const proto::ProtoObject* self = ctx->fromExternalPointer(this, /*finalizer=*/nullptr);
        if (!self) break;
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, self);
        const proto::ProtoThread* t = space->newThread(ctx, name, &DeferredPool::threadMain,
                                                       args, nullptr);
        if (!t) break;
        // A ProtoThread is a cell: pin it until it has been joined. Its
        // handle is an object pointer (protoCore's thread_main passes it as
        // the entry method's receiver the same way).
        threadPins_.push_back(rs->add(reinterpret_cast<const proto::ProtoObject*>(t)));
        threads_.push_back(t);
        ++started_;
    }
    if (started_ == 0) {
        std::cerr << "protojs: Deferred: no pool thread could be started" << std::endl;
        return false;
    }
    return true;
}

bool DeferredPool::submit(proto::ProtoContext* ctx, const Task& task) {
    if (!startAttempted_) start(ctx);
    if (started_ == 0) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        queue_.push_back(task);
    }
    cv_.notify_one();
    return true;
}

std::size_t DeferredPool::completedTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return completed_;
}

void DeferredPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ && threads_.empty()) return;
        stopping_ = true;
        queue_.clear();
    }
    proto::ProtoContext* ctx = owner_ ? owner_->getProtoContext() : nullptr;
    // The threads leave one at a time: each exit token lets one thread out,
    // and the next token is issued only once that thread has been joined.
    // A protoCore thread removes itself from its space's thread list on exit,
    // and in protoCore 2.8.0 two threads doing that at once race on the list
    // (read outside globalMutex; fixed upstream in d258d4b5, "Snapshot
    // space->threads under globalMutex"). Twelve pool threads stopping
    // together hit it every time under ThreadSanitizer.
    //
    // ProtoThread::join leaves protoCore's running set for as long as it
    // blocks (protoCore >= 2.3), so a pool thread that needs a collection to
    // finish its current task can get one while the owner waits here; the
    // condition wait below is bracketed for the same reason.
    for (std::size_t i = 0; i < threads_.size(); ++i) {
        proto::ProtoThread* leaving = nullptr;
        {
            ThreadUnmanagedScope parked(ctx);
            std::unique_lock<std::mutex> lock(mutex_);
            ++exitTokens_;
            cv_.notify_all();
            cv_.wait(lock, [this] { return !exited_.empty(); });
            leaving = exited_.back();
            exited_.pop_back();
        }
        if (leaving) leaving->join(ctx);
    }
    proto::ProtoRootSet* rs = owner_ ? owner_->getRootSet() : nullptr;
    if (rs) {
        for (proto::ProtoRootSet::Handle h : threadPins_) rs->remove(h);
    }
    threadPins_.clear();
    threads_.clear();
}

const proto::ProtoObject* DeferredPool::threadMain(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* /*self*/,
                                                   const proto::ParentLink*,
                                                   const proto::ProtoList* args,
                                                   const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* p = args->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ep = p ? p->asExternalPointer(ctx) : nullptr;
    DeferredPool* pool = ep ? static_cast<DeferredPool*>(ep->getPointer(ctx)) : nullptr;
    if (pool) pool->run(ctx);
    return PROTO_NONE;
}

void DeferredPool::run(proto::ProtoContext* root) {
    // Blocking helpers that find their context through the thread slot
    // (ThreadUnmanagedScope) must find this thread's.
    setThreadProtoContext(root);

    // This thread's QuickJS context: the owner's runtime is single-threaded.
    // Its opaque is the owner wrapper, which TypeBridge reads prototypes from.
    JSRuntime* qrt = JS_NewRuntime();
    JSContext* qctx = qrt ? JS_NewContext(qrt) : nullptr;
    if (qctx) JS_SetContextOpaque(qctx, owner_);

    const proto::ProtoObject* globalSlot = nullptr;
    JSContextWrapper::ThreadView view;
    view.globalSlot = &globalSlot;
    view.quickjs = qctx;
    view.context = root;
    {
        JSContextWrapper::CurrentScope current(owner_);
        JSContextWrapper::ThreadViewScope viewScope(&view);
        MicrotaskQueue jobs(owner_);
        MicrotaskQueue::ThreadOverride jobsScope(&jobs);

        for (;;) {
            Task task;
            {
                // Waiting is a blocking region: leave the running set, so an
                // idle pool thread never holds up a stop-the-world. Nothing in
                // this block touches a ProtoObject.
                proto::ProtoContext::UnmanagedScope idle(root);
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] {
                    return (stopping_ && exitTokens_ > 0) || (!stopping_ && !queue_.empty());
                });
                if (stopping_) {
                    // Take the exit token and say which thread is leaving.
                    --exitTokens_;
                    exited_.push_back(root->thread);
                    cv_.notify_all();
                    break;
                }
                task = queue_.front();
                queue_.pop_front();
            }
            runTask(root, task, globalSlot, jobs);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++completed_;
            }
        }
    }
    if (qctx) JS_FreeContext(qctx);
    if (qrt) JS_FreeRuntime(qrt);
    clearThreadProtoContext(root);
}

void DeferredPool::runTask(proto::ProtoContext* root, const Task& task,
                           const proto::ProtoObject*& globalSlot, MicrotaskQueue& jobs) {
    adoptThreadIdentity(task.identity);
    globalSlot = task.global;

    proto::ProtoRootSet* rs = owner_->getRootSet();
    proto::ProtoContext taskCtx(root->space, root, nullptr, nullptr, nullptr, nullptr);

    // The function stays pinned until it has returned.
    const proto::ProtoObject* fn = rs->resolve(task.fn);
    const proto::ProtoObject* undefinedValue = getUndefinedSentinel();
    const proto::ProtoObject* result = callJSFunctionFromAsync(
        &taskCtx, fn, undefinedValue ? undefinedValue : PROTO_NONE, taskCtx.newList(),
        task.module, &globalSlot);
    bool threw = false;
    if (hasCallException()) {
        threw = true;
        result = consumeCallException();
    }
    if (!result) result = PROTO_NONE;
    // Pin the outcome before anything else allocates.
    const proto::ProtoRootSet::Handle outcome = rs->add(result);
    rs->remove(task.fn);

    // Jobs the function queued on this thread (an async helper that settled,
    // a queueMicrotask) run now, before the outcome is handed over.
    jobs.checkpoint(&taskCtx);

    // Settle the Deferred on the owner thread, as a microtask.
    const proto::ProtoRootSet::Handle deferredHandle = task.deferred;
    owner_->microtasks().enqueueFromAnyThread(
        [rs, deferredHandle, outcome, threw](proto::ProtoContext* c) {
            const proto::ProtoObject* d = rs->resolve(deferredHandle);
            const proto::ProtoObject* v = rs->resolve(outcome);
            if (d) {
                if (threw) rejectPromise(c, d, v ? v : PROTO_NONE);
                else resolvePromise(c, d, v ? v : PROTO_NONE);
            }
            rs->remove(deferredHandle);
            rs->remove(outcome);
            ProtoDeferred::taskSettled();
        });
}

}  // namespace protojs
