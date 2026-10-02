#include "MicrotaskQueue.h"

#include "EventLoop.h"
#include "JSContext.h"
#include "PromisePrototype.h"
#include "ProtoDeferred.h"
#include "runtime/ProtoBytecodeModule.h"
#include "runtime/ProtoInterpreter.h"

#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <vector>

namespace protojs {

namespace {

// Live queues, so that a pointer recorded on some thread (the per-thread list
// of queues to drain, the event-loop wake-up of enqueueFromAnyThread) is used
// only while its queue exists.  A worker's wrapper -- and with it its queue --
// can be destroyed by another thread.
std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}
std::unordered_set<const MicrotaskQueue*>& registry() {
    static std::unordered_set<const MicrotaskQueue*> s;
    return s;
}
bool isLive(const MicrotaskQueue* q) {
    std::lock_guard<std::mutex> lock(registryMutex());
    return registry().count(q) != 0;
}

// The queue of the wrapper this thread constructed: the fallback for
// enqueueing when no wrapper is current.
thread_local MicrotaskQueue* t_threadQueue = nullptr;

// Queues that received a job from this thread since the last
// checkpointThread().  Small: one entry per wrapper touched in the turn.
thread_local std::vector<MicrotaskQueue*> t_scheduled;

std::atomic<bool> g_unhandledRejectionsAreFatal{true};

// Holder attribute keys.  Interned symbols are perennial (GC_BRIDGING.md,
// Mechanism A), so caching them process-wide is safe.
const proto::ProtoString* keyJobs(proto::ProtoContext* ctx) {
    static const proto::ProtoString* k = proto::ProtoString::createSymbol(ctx, "__mtq_jobs__");
    return k;
}
const proto::ProtoString* keyDraining(proto::ProtoContext* ctx) {
    static const proto::ProtoString* k = proto::ProtoString::createSymbol(ctx, "__mtq_draining__");
    return k;
}
const proto::ProtoString* keyRejections(proto::ProtoContext* ctx) {
    static const proto::ProtoString* k = proto::ProtoString::createSymbol(ctx, "__mtq_rejections__");
    return k;
}
const proto::ProtoString* keyReporting(proto::ProtoContext* ctx) {
    static const proto::ProtoString* k = proto::ProtoString::createSymbol(ctx, "__mtq_reporting__");
    return k;
}

}  // namespace

MicrotaskQueue::MicrotaskQueue(JSContextWrapper* owner) : owner_(owner) {
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().insert(this);
    }
    if (!t_threadQueue) t_threadQueue = this;
}

MicrotaskQueue::~MicrotaskQueue() {
    {
        std::lock_guard<std::mutex> lock(registryMutex());
        registry().erase(this);
    }
    if (t_threadQueue == this) t_threadQueue = nullptr;
    t_scheduled.erase(std::remove(t_scheduled.begin(), t_scheduled.end(), this),
                      t_scheduled.end());
    // The holder's pin is not released here: the wrapper destroys its whole
    // root set right after this queue, and a destructor that may run from a
    // finalizer path must not call ProtoRootSet::remove (GC_BRIDGING.md,
    // "Finalizers").
}

MicrotaskQueue* MicrotaskQueue::current() {
    if (JSContextWrapper* w = JSContextWrapper::current()) return &w->microtasks();
    if (t_threadQueue && isLive(t_threadQueue)) return t_threadQueue;
    return nullptr;
}

void MicrotaskQueue::ensureHolder(proto::ProtoContext* ctx) {
    if (holder_) return;
    proto::ProtoRootSet* rs = owner_ ? owner_->getRootSet() : nullptr;
    const proto::ProtoObject* h = ctx->newObject(true);
    if (!h) return;
    // Pin before the first write: from here on the holder and everything it
    // references are roots for the life of the wrapper.
    if (rs) rs->add(h);
    holder_ = h;
}

const proto::ProtoList* MicrotaskQueue::holderList(proto::ProtoContext* ctx,
                                                   const proto::ProtoString* key) const {
    if (!holder_ || !key) return nullptr;
    const proto::ProtoObject* v = holder_->getOwnAttributeDirect(ctx, key);
    if (!v || v == PROTO_NONE) return nullptr;
    return v->asList(ctx);
}

void MicrotaskQueue::setHolderList(proto::ProtoContext* ctx, const proto::ProtoString* key,
                                   const proto::ProtoList* list) {
    if (!holder_ || !key) return;
    holder_->setAttribute(ctx, key, list ? list->asObject(ctx) : PROTO_NONE);
}

void MicrotaskQueue::scheduleOnThisThread() {
    if (std::find(t_scheduled.begin(), t_scheduled.end(), this) == t_scheduled.end())
        t_scheduled.push_back(this);
}

void MicrotaskQueue::enqueue(proto::ProtoContext* ctx, const proto::ProtoList* job) {
    if (!ctx || !job) return;
    ensureHolder(ctx);
    if (!holder_) return;
    const proto::ProtoString* k = keyJobs(ctx);
    const proto::ProtoList* jobs = holderList(ctx, k);
    if (!jobs) jobs = ctx->newList();
    jobs = jobs->appendLast(ctx, job->asObject(ctx));
    setHolderList(ctx, k, jobs);
    ++pendingJobs_;
    scheduleOnThisThread();
}

void MicrotaskQueue::enqueueCallback(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* callback) {
    const proto::ProtoObject* items[2] = {
        ctx->fromInteger(kCallbackJob), callback ? callback : PROTO_NONE};
    enqueue(ctx, ctx->newList(2, items));
}

void MicrotaskQueue::enqueueFromAnyThread(NativeJob job) {
    {
        std::lock_guard<std::mutex> lock(inboxMutex_);
        inbox_.push_back(std::move(job));
        inboxNonEmpty_.store(true, std::memory_order_release);
    }
    // Wake the event loop.  The callback itself does nothing: the checkpoint
    // EventLoop::processCallbacks performs after it drains this queue.
    MicrotaskQueue* self = this;
    EventLoop::getInstance().enqueueCallback([self]() {
        if (isLive(self)) self->scheduleOnThisThread();
    });
}

void MicrotaskQueue::takeInbox(proto::ProtoContext* ctx) {
    if (!inboxNonEmpty_.load(std::memory_order_acquire)) return;
    std::deque<NativeJob> taken;
    {
        std::lock_guard<std::mutex> lock(inboxMutex_);
        taken.swap(inbox_);
        inboxNonEmpty_.store(false, std::memory_order_release);
    }
    for (auto& job : taken) {
        const long long id = nextNativeId_++;
        nativeJobs_.emplace(id, std::move(job));
        const proto::ProtoObject* items[2] = {
            ctx->fromInteger(kNativeJob), ctx->fromInteger(id)};
        enqueue(ctx, ctx->newList(2, items));
    }
}

void MicrotaskQueue::noteRejection(proto::ProtoContext* ctx, const proto::ProtoObject* promise) {
    if (!ctx || !promise) return;
    ensureHolder(ctx);
    if (!holder_) return;
    const proto::ProtoString* k = keyRejections(ctx);
    const proto::ProtoList* list = holderList(ctx, k);
    if (!list) list = ctx->newList();
    setHolderList(ctx, k, list->appendLast(ctx, promise));
    pendingRejections_ = true;
    scheduleOnThisThread();
}

bool MicrotaskQueue::hasPendingJobs() const {
    return pendingJobs_ > 0 || inboxNonEmpty_.load(std::memory_order_acquire);
}

void MicrotaskQueue::runJob(proto::ProtoContext* ctx, const proto::ProtoObject* jobObj) {
    const proto::ProtoList* job = (jobObj && jobObj != PROTO_NONE) ? jobObj->asList(ctx) : nullptr;
    if (!job || job->getSize(ctx) < 2) return;
    const proto::ProtoObject* kindObj = job->getAt(ctx, 0);
    const long long kind = (kindObj && kindObj->isInteger(ctx)) ? kindObj->asLong(ctx) : 0;
    auto at = [&](int i) -> const proto::ProtoObject* {
        if (static_cast<proto::proto_ulong>(i) >= job->getSize(ctx)) return PROTO_NONE;
        const proto::ProtoObject* v = job->getAt(ctx, i);
        return v ? v : PROTO_NONE;
    };
    switch (kind) {
    case kReactionJob: {
        const proto::ProtoObject* st = at(3);
        promiseReactionJob(ctx, at(1), at(2),
                           (st && st->isInteger(ctx)) ? static_cast<int>(st->asLong(ctx)) : 1);
        break;
    }
    case kResolveThenableJob:
        promiseResolveThenableJob(ctx, at(1), at(2), at(3));
        break;
    case kCallbackJob:
        callJSFunction(ctx, at(1), getUndefinedSentinel(), ctx->newList());
        // An exception escaping a microtask is uncaught, as in Node.
        drainCallbackException(ctx, "microtask");
        break;
    case kNativeJob: {
        const proto::ProtoObject* idObj = at(1);
        const long long id = (idObj && idObj->isInteger(ctx)) ? idObj->asLong(ctx) : 0;
        auto it = nativeJobs_.find(id);
        if (it != nativeJobs_.end()) {
            NativeJob fn = std::move(it->second);
            nativeJobs_.erase(it);
            if (fn) fn(ctx);
            drainCallbackException(ctx, "microtask");
        }
        break;
    }
    default:
        break;
    }
}

void MicrotaskQueue::reportRejections(proto::ProtoContext* ctx) {
    pendingRejections_ = false;
    const proto::ProtoString* k = keyRejections(ctx);
    const proto::ProtoList* list = holderList(ctx, k);
    if (!list) return;
    // Keep the batch reachable while it is examined; then let it go.
    const proto::ProtoString* rk = keyReporting(ctx);
    setHolderList(ctx, rk, list);
    setHolderList(ctx, k, nullptr);
    const proto::proto_ulong n = list->getSize(ctx);
    for (proto::proto_ulong i = 0; i < n; ++i) {
        const proto::ProtoObject* p = list->getAt(ctx, static_cast<int>(i));
        if (!p || isPromiseHandled(ctx, p)) continue;
        // Reported once: mark it handled so a later checkpoint (the REPL keeps
        // running) does not report it again.
        markPromiseHandled(ctx, p);
        reportUnhandledRejection(ctx, getPromiseValuePublic(ctx, p),
                                 g_unhandledRejectionsAreFatal.load());
    }
    setHolderList(ctx, rk, nullptr);
}

void MicrotaskQueue::checkpoint(proto::ProtoContext* parent) {
    if (draining_ || !owner_) return;
    // A checkpoint runs only with an empty JavaScript stack (ECMA-262 §9.5:
    // jobs run when the execution context stack is empty).
    if (interpreterIsRunning()) return;
    if (!hasPendingJobs() && !pendingRejections_) return;

    draining_ = true;
    JSContextWrapper::CurrentScope current(owner_);
    proto::ProtoContext* root = owner_->getProtoContext();
    if (!parent) parent = root;
    InterpreterEntryScope entry(
        static_cast<const ProtoBytecodeModule*>(owner_->getRootModule()),
        owner_->getNativeGlobalRootPtr());

    for (;;) {
        {
            proto::ProtoContext batchCtx(parent->space, parent, nullptr, nullptr, nullptr, nullptr);
            takeInbox(&batchCtx);
            const proto::ProtoString* jk = keyJobs(&batchCtx);
            const proto::ProtoList* jobs = holderList(&batchCtx, jk);
            if (jobs && jobs->getSize(&batchCtx) > 0) {
                // The batch stays reachable through the holder while it runs;
                // jobs it queues go to a fresh list and form the next batch,
                // which preserves first-in, first-out order.
                const proto::ProtoString* dk = keyDraining(&batchCtx);
                setHolderList(&batchCtx, dk, jobs);
                setHolderList(&batchCtx, jk, nullptr);
                pendingJobs_ = 0;
                const proto::proto_ulong n = jobs->getSize(&batchCtx);
                for (proto::proto_ulong i = 0; i < n; ++i) {
                    proto::ProtoContext jobCtx(parent->space, &batchCtx,
                                               nullptr, nullptr, nullptr, nullptr);
                    runJob(&jobCtx, jobs->getAt(&jobCtx, static_cast<int>(i)));
                }
                setHolderList(&batchCtx, dk, nullptr);
                continue;
            }
            if (pendingRejections_) {
                reportRejections(&batchCtx);
                if (hasPendingJobs()) continue;
            }
        }
        break;
    }
    draining_ = false;
}

void MicrotaskQueue::checkpointThread() {
    while (!t_scheduled.empty()) {
        std::vector<MicrotaskQueue*> batch;
        batch.swap(t_scheduled);
        for (MicrotaskQueue* q : batch) {
            if (q && isLive(q)) q->checkpoint();
        }
    }
}

void MicrotaskQueue::setUnhandledRejectionsAreFatal(bool fatal) {
    g_unhandledRejectionsAreFatal.store(fatal);
}

}  // namespace protojs
