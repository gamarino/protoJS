#include "Timers.h"
#include "EventLoop.h"
#include "FunctionPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "ProtoDeferred.h"
#include "ProtoNativeModule.h"
#include "runtime/ProtoInterpreter.h"
#include "runtime/ProtoBytecodeModule.h"
#include "runtime/PinnedBuiltin.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace protojs {

namespace {

using Clock = std::chrono::steady_clock;

// The scheduling state lives in native memory: a min-heap of (due time,
// sequence) entries and the live timers by id.  A timer's entry is stale
// once the timer is cleared or rescheduled (its sequence no longer matches)
// and is dropped when it reaches the top.  Everything here is touched by the
// owner wrapper's thread only -- the JavaScript calls (refused elsewhere)
// and the drain loop in main.cpp -- so it needs no lock.
struct HeapEntry {
    Clock::time_point due;
    std::uint64_t seq;
    std::uint64_t id;
    bool operator>(const HeapEntry& o) const {
        return due != o.due ? due > o.due : seq > o.seq;
    }
};

struct TimerState {
    Clock::time_point due;
    std::uint64_t seq = 0;
    long long delayMs = 1;
    bool repeat = false;
    bool refed = true;
};

std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<HeapEntry>> g_heap;
std::unordered_map<std::uint64_t, TimerState> g_timers;
std::uint64_t g_nextId = 1;
std::uint64_t g_nextSeq = 1;
long long g_refedCount = 0;

// The Timeout objects of the live timers, by id, in a sparse list held by one
// mutable object pinned in the owner's root set (one pin for all timers): a
// timer's callback and arguments stay reachable until it is cleared or done.
const proto::ProtoObject* g_holder = nullptr;

const proto::ProtoString* key(proto::ProtoContext* ctx, const char* name) {
    return proto::ProtoString::createSymbol(ctx, name);
}
const proto::ProtoString* keyTable(proto::ProtoContext* ctx) { return key(ctx, "__timers__"); }
const proto::ProtoString* keyId(proto::ProtoContext* ctx) { return key(ctx, "__timer_id__"); }
const proto::ProtoString* keyCallback(proto::ProtoContext* ctx) { return key(ctx, "__timer_callback__"); }
const proto::ProtoString* keyArgs(proto::ProtoContext* ctx) { return key(ctx, "__timer_args__"); }
const proto::ProtoString* keyDelay(proto::ProtoContext* ctx) { return key(ctx, "__timer_delay__"); }
const proto::ProtoString* keyRepeat(proto::ProtoContext* ctx) { return key(ctx, "__timer_repeat__"); }
const proto::ProtoString* keyUnref(proto::ProtoContext* ctx) { return key(ctx, "__timer_unref__"); }

const proto::ProtoSparseList* table(proto::ProtoContext* ctx) {
    if (!g_holder) return nullptr;
    const proto::ProtoObject* t = g_holder->getAttribute(ctx, keyTable(ctx), false);
    return (t && t != PROTO_NONE) ? t->asSparseList(ctx) : nullptr;
}

void setTable(proto::ProtoContext* ctx, const proto::ProtoSparseList* t) {
    if (g_holder && t) g_holder->setAttribute(ctx, keyTable(ctx), t->asObject(ctx));
}

bool ensureHolder(proto::ProtoContext* ctx, JSContextWrapper* w) {
    if (g_holder) return true;
    const proto::ProtoObject* h = ctx->newObject(/*mutable=*/true);
    proto::ProtoRootSet* rs = w ? w->getRootSet() : nullptr;
    if (!h || !rs) return false;
    rs->add(h);   // for the wrapper's life, as the timers it holds
    g_holder = h;
    setTable(ctx, ctx->newSparseList());
    return true;
}

const proto::ProtoObject* timeoutObject(proto::ProtoContext* ctx, std::uint64_t id) {
    const proto::ProtoSparseList* t = table(ctx);
    if (!t || !t->has(ctx, id)) return nullptr;
    const proto::ProtoObject* o = t->getAt(ctx, id);
    return (o && o != PROTO_NONE) ? o : nullptr;
}

void schedule(std::uint64_t id, TimerState& st, Clock::time_point due) {
    st.due = due;
    st.seq = g_nextSeq++;
    g_heap.push(HeapEntry{due, st.seq, id});
}

// Forgets timer `id`: its state, and its Timeout in the table.
void forget(proto::ProtoContext* ctx, std::uint64_t id) {
    auto it = g_timers.find(id);
    if (it == g_timers.end()) return;
    if (it->second.refed) --g_refedCount;
    g_timers.erase(it);
    if (const proto::ProtoSparseList* t = table(ctx))
        if (t->has(ctx, id)) setTable(ctx, t->removeAt(ctx, id));
}

// Arms (or re-arms) the timer of Timeout `obj` with its stored delay.
void arm(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::uint64_t id) {
    const proto::ProtoObject* d = obj->getAttribute(ctx, keyDelay(ctx), false);
    const proto::ProtoObject* r = obj->getAttribute(ctx, keyRepeat(ctx), false);
    const proto::ProtoObject* u = obj->getAttribute(ctx, keyUnref(ctx), false);
    auto it = g_timers.find(id);
    if (it == g_timers.end()) {
        TimerState st;
        st.delayMs = (d && d->isInteger(ctx)) ? d->asLong(ctx) : 1;
        st.repeat = (r == PROTO_TRUE);
        st.refed = (u != PROTO_TRUE);
        if (st.refed) ++g_refedCount;
        it = g_timers.emplace(id, st).first;
        if (const proto::ProtoSparseList* t = table(ctx)) setTable(ctx, t->setAt(ctx, id, obj));
    }
    schedule(id, it->second, Clock::now() + std::chrono::milliseconds(it->second.delayMs));
}

// The id of a Timeout, or of a number or numeric string; 0 for anything else.
std::uint64_t timerIdOf(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel() || v == getNullSentinel()) return 0;
    long long id = 0;
    if (v->isInteger(ctx)) {
        id = v->asLong(ctx);
    } else if (v->isDouble(ctx)) {
        const double d = v->asDouble(ctx);
        if (d >= 1 && d == std::floor(d) && d < 9.0e15) id = static_cast<long long>(d);
    } else if (v->isString(ctx)) {
        std::string s;
        v->asString(ctx)->toUTF8String(ctx, s);
        if (s.empty() || s.size() > 15 || s.find_first_not_of("0123456789") != std::string::npos) return 0;
        id = std::stoll(s);
    } else {
        const proto::ProtoObject* idObj = v->getAttribute(ctx, keyId(ctx), false);
        if (idObj && idObj != PROTO_NONE && idObj->isInteger(ctx)) id = idObj->asLong(ctx);
    }
    return id > 0 ? static_cast<std::uint64_t>(id) : 0;
}

// The id of `self` when it is a Timeout, else 0.
std::uint64_t selfId(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE) return 0;
    const proto::ProtoObject* idObj = self->getAttribute(ctx, keyId(ctx), false);
    return (idObj && idObj != PROTO_NONE && idObj->isInteger(ctx))
        ? static_cast<std::uint64_t>(idObj->asLong(ctx)) : 0;
}

// The delay argument as Node takes it: ToNumber, then 1 unless the value is
// in 1 .. 2^31 - 1, truncated to whole milliseconds.
long long coerceDelay(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    constexpr double kTimeoutMax = 2147483647.0;
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) return 1;
    double d = 0;
    if (v->isInteger(ctx)) {
        d = static_cast<double>(v->asLong(ctx));
    } else if (v->isDouble(ctx)) {
        d = v->asDouble(ctx);
    } else {
        const proto::ProtoObject* n = jsToNumber(ctx, v);
        if (!n || n == PROTO_NONE) return 1;
        d = n->isInteger(ctx) ? static_cast<double>(n->asLong(ctx))
          : n->isDouble(ctx) ? n->asDouble(ctx) : 0;
    }
    if (!(d >= 1 && d <= kTimeoutMax)) return 1;
    return static_cast<long long>(d);
}

// A short description of a value for ERR_INVALID_ARG_TYPE.
std::string describeReceived(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) return "undefined";
    if (v == getNullSentinel()) return "null";
    if (v == PROTO_TRUE || v == PROTO_FALSE) return "type boolean";
    if (v->isInteger(ctx) || v->isDouble(ctx)) return "type number";
    if (v->isString(ctx)) return "type string";
    return "an instance of Object";
}

const proto::ProtoObject* timeoutPrototype(proto::ProtoContext* ctx);

// The common body of setTimeout and setInterval.
const proto::ProtoObject* createTimer(proto::ProtoContext* ctx,
                                      const proto::ProtoArguments* args,
                                      bool repeat, const char* what) {
    if (refuseOnDeferredThread(ctx, what)) return PROTO_NONE;
    JSContextWrapper* w = JSContextWrapper::current();
    if (!w || w != EventLoop::getInstance().owner()) {
        const std::string msg = std::string(what) + " is not available in a worker thread";
        signalNativeException(makeNativeError(ctx, "Error", msg.c_str()));
    }
    const proto::ProtoObject* callback = args ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!isCallableValue(ctx, callback)) {
        const std::string msg = "The \"callback\" argument must be of type function. Received "
            + describeReceived(ctx, callback);
        const proto::ProtoObject* err = makeNativeError(ctx, "TypeError", msg.c_str());
        if (err && err != PROTO_NONE)
            err->setAttribute(ctx, ctx->fromUTF8String("code")->asString(ctx),
                              ctx->fromUTF8String("ERR_INVALID_ARG_TYPE"));
        signalNativeException(err);
    }
    const long long delay = coerceDelay(ctx, args ? args->getAt(ctx, 1) : PROTO_NONE);
    if (!ensureHolder(ctx, w)) return PROTO_NONE;

    const proto::ProtoList* extra = ctx->newList();
    const int n = args ? static_cast<int>(args->getSize(ctx)) : 0;
    for (int i = 2; i < n; ++i) extra = extra->appendLast(ctx, args->getAt(ctx, i));

    const proto::ProtoObject* proto = timeoutPrototype(ctx);
    const proto::ProtoObject* t = proto ? proto->newChild(ctx, /*mutable=*/true)
                                        : ctx->newObject(/*mutable=*/true);
    const std::uint64_t id = g_nextId++;
    t->setAttribute(ctx, keyId(ctx), ctx->fromInteger(static_cast<long long>(id)));
    t->setAttribute(ctx, keyCallback(ctx), callback);
    t->setAttribute(ctx, keyArgs(ctx), extra->asObject(ctx));
    t->setAttribute(ctx, keyDelay(ctx), ctx->fromInteger(delay));
    t->setAttribute(ctx, keyRepeat(ctx), repeat ? PROTO_TRUE : PROTO_FALSE);
    arm(ctx, t, id);
    return t;
}

const proto::ProtoObject* setTimeoutNative(proto::ProtoContext* ctx, const proto::ProtoObject*,
        const proto::ParentLink*, const proto::ProtoArguments* args,
        const proto::ProtoKeywordArguments*) {
    return createTimer(ctx, args, /*repeat=*/false, "setTimeout()");
}

const proto::ProtoObject* setIntervalNative(proto::ProtoContext* ctx, const proto::ProtoObject*,
        const proto::ParentLink*, const proto::ProtoArguments* args,
        const proto::ProtoKeywordArguments*) {
    return createTimer(ctx, args, /*repeat=*/true, "setInterval()");
}

// clearTimeout and clearInterval (the same function, as in Node).
const proto::ProtoObject* clearTimerNative(proto::ProtoContext* ctx, const proto::ProtoObject*,
        const proto::ParentLink*, const proto::ProtoArguments* args,
        const proto::ProtoKeywordArguments*) {
    if (JSContextWrapper::current() != EventLoop::getInstance().owner() || JSContextWrapper::onPoolThread())
        return getUndefinedSentinel();
    const std::uint64_t id = timerIdOf(ctx, args ? args->getAt(ctx, 0) : PROTO_NONE);
    if (id) forget(ctx, id);
    return getUndefinedSentinel();
}

// ---- Timeout.prototype ------------------------------------------------------

bool onOwner() {
    return JSContextWrapper::current() == EventLoop::getInstance().owner() && !JSContextWrapper::onPoolThread();
}

const proto::ProtoObject* timeoutRef(proto::ProtoContext* ctx, const proto::ProtoObject* self,
        const proto::ParentLink*, const proto::ProtoArguments*,
        const proto::ProtoKeywordArguments*) {
    const std::uint64_t id = selfId(ctx, self);
    if (!id || !onOwner()) return self;
    self->setAttribute(ctx, keyUnref(ctx), PROTO_FALSE);
    auto it = g_timers.find(id);
    if (it != g_timers.end() && !it->second.refed) { it->second.refed = true; ++g_refedCount; }
    return self;
}

const proto::ProtoObject* timeoutUnref(proto::ProtoContext* ctx, const proto::ProtoObject* self,
        const proto::ParentLink*, const proto::ProtoArguments*,
        const proto::ProtoKeywordArguments*) {
    const std::uint64_t id = selfId(ctx, self);
    if (!id || !onOwner()) return self;
    self->setAttribute(ctx, keyUnref(ctx), PROTO_TRUE);
    auto it = g_timers.find(id);
    if (it != g_timers.end() && it->second.refed) { it->second.refed = false; --g_refedCount; }
    return self;
}

const proto::ProtoObject* timeoutHasRef(proto::ProtoContext* ctx, const proto::ProtoObject* self,
        const proto::ParentLink*, const proto::ProtoArguments*,
        const proto::ProtoKeywordArguments*) {
    if (!selfId(ctx, self)) return PROTO_FALSE;
    return self->getAttribute(ctx, keyUnref(ctx), false) == PROTO_TRUE ? PROTO_FALSE : PROTO_TRUE;
}

// Restarts the timer's countdown from now; re-arms a timer that has already
// run or been cleared (Node).
const proto::ProtoObject* timeoutRefresh(proto::ProtoContext* ctx, const proto::ProtoObject* self,
        const proto::ParentLink*, const proto::ProtoArguments*,
        const proto::ProtoKeywordArguments*) {
    const std::uint64_t id = selfId(ctx, self);
    if (!id || !onOwner() || !g_holder) return self;
    arm(ctx, self, id);
    return self;
}

const proto::ProtoObject* timeoutClose(proto::ProtoContext* ctx, const proto::ProtoObject* self,
        const proto::ParentLink*, const proto::ProtoArguments*,
        const proto::ProtoKeywordArguments*) {
    const std::uint64_t id = selfId(ctx, self);
    if (id && onOwner()) forget(ctx, id);
    return self;
}

const proto::ProtoObject* timeoutToPrimitive(proto::ProtoContext* ctx, const proto::ProtoObject* self,
        const proto::ParentLink*, const proto::ProtoArguments*,
        const proto::ProtoKeywordArguments*) {
    const std::uint64_t id = selfId(ctx, self);
    return ctx->fromInteger(static_cast<long long>(id));
}

const proto::ProtoObject* timeoutConstructorCall(proto::ProtoContext* ctx, const proto::ProtoObject*,
        const proto::ParentLink*, const proto::ProtoArguments*,
        const proto::ProtoKeywordArguments*) {
    signalNativeException(makeNativeError(ctx, "TypeError",
        "Timeout objects are made by setTimeout and setInterval"));
}

// Timeout.prototype: a child of the realm's Object.prototype, with a
// `constructor` named Timeout, as in Node.
const proto::ProtoObject* timeoutPrototype(proto::ProtoContext* ctx) {
    static PinnedBuiltin protoCache;
    if (const proto::ProtoObject* cached = protoCache.get(ctx)) return cached;
    static const NativeEntry entries[] = {
        {"ref",     timeoutRef},
        {"unref",   timeoutUnref},
        {"hasRef",  timeoutHasRef},
        {"refresh", timeoutRefresh},
        {"close",   timeoutClose},
        {"Symbol.toPrimitive", timeoutToPrimitive},
        NATIVE_MODULE_END
    };
    JSContextWrapper* w = JSContextWrapper::current();
    const proto::ProtoObject* objectProto = w ? w->getJSObjectPrototype() : nullptr;
    const proto::ProtoObject* p = objectProto ? objectProto->newChild(ctx, /*mutable=*/true)
                                              : ctx->newObject(/*mutable=*/true);
    for (const NativeEntry* e = entries; p && e->name; ++e)
        p = ProtoNativeModule::addMethod(ctx, p, e->name, e->fn);
    if (const proto::ProtoObject* ctor =
            wrapNativeFunction(ctx, timeoutConstructorCall, "Timeout", 5, nullptr)) {
        const proto::ProtoObject* c = ctor->setAttribute(ctx, JSSymbols::prototype(ctx), p);
        p->setAttribute(ctx, JSSymbols::constructor(ctx), c);
    }
    return protoCache.keep(ctx, p);
}

// Runs the callback of timer `id`, due at `seq`, as one macrotask.
void fire(std::uint64_t id, std::uint64_t seq) {
    JSContextWrapper* w = EventLoop::getInstance().owner();
    if (!w) return;
    JSContextWrapper::CurrentScope scope(w);
    proto::ProtoContext* ctx = w->getProtoContext();
    if (!ctx) return;
    const proto::ProtoObject* t = timeoutObject(ctx, id);
    if (!t) { g_timers.erase(id); return; }
    const proto::ProtoObject* callback = t->getAttribute(ctx, keyCallback(ctx), false);
    const proto::ProtoObject* argsObj = t->getAttribute(ctx, keyArgs(ctx), false);
    const proto::ProtoList* args = (argsObj && argsObj != PROTO_NONE) ? argsObj->asList(ctx) : nullptr;

    // The Timeout stays pinned while its callback runs, even if the callback
    // clears it (which drops it from the table).
    proto::ProtoRootSet* rs = w->getRootSet();
    proto::ProtoRootSet::Handle pin = rs ? rs->add(t) : proto::ProtoRootSet::kNullHandle;
    EventLoop::getInstance().runMacrotask([&]() {
        const ProtoBytecodeModule* mod =
            static_cast<const ProtoBytecodeModule*>(w->getRootModule());
        callJSFunctionFromAsync(ctx, callback, t, args ? args : ctx->newList(),
                                mod, w->getNativeGlobalRootPtr());
    });
    if (rs) rs->remove(pin);

    // Unless the callback cleared or refreshed it, an interval goes again
    // `delay` ms from now and a one-shot timer is done.
    auto it = g_timers.find(id);
    if (it == g_timers.end() || it->second.seq != seq) return;
    if (it->second.repeat)
        schedule(id, it->second, Clock::now() + std::chrono::milliseconds(it->second.delayMs));
    else
        forget(ctx, id);
}

}  // namespace

bool Timers::hasRefedTimers() { return g_refedCount > 0; }

long long Timers::msUntilNext() {
    while (!g_heap.empty()) {
        const HeapEntry& top = g_heap.top();
        auto it = g_timers.find(top.id);
        if (it != g_timers.end() && it->second.seq == top.seq) break;
        g_heap.pop();   // stale: cleared or rescheduled
    }
    if (g_heap.empty()) return -1;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        g_heap.top().due - Clock::now()).count();
    return left > 0 ? left : 0;
}

void Timers::runDue() {
    if (!EventLoop::getInstance().owner() || g_heap.empty()) return;
    const Clock::time_point now = Clock::now();
    // A timer scheduled while this pass runs (a new timer, an interval's next
    // run, a refresh) waits for the next pass, as in Node.
    const std::uint64_t passLimit = g_nextSeq;
    while (!g_heap.empty()) {
        const HeapEntry top = g_heap.top();
        if (top.due > now || top.seq >= passLimit) break;
        g_heap.pop();
        auto it = g_timers.find(top.id);
        if (it == g_timers.end() || it->second.seq != top.seq) continue;   // stale
        fire(top.id, top.seq);
    }
}

const proto::ProtoObject* Timers::init(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    struct Fn { const char* name; proto::ProtoMethod fn; long long length; };
    static const Fn fns[] = {
        {"setTimeout",    setTimeoutNative,  2},
        {"setInterval",   setIntervalNative, 2},
        {"clearTimeout",  clearTimerNative,  1},
        {"clearInterval", clearTimerNative,  1},
    };
    for (const Fn& f : fns) {
        const proto::ProtoObject* fn = wrapNativeFunction(ctx, f.fn, f.name, f.length, nullptr);
        const proto::ProtoObject* k = ctx->fromUTF8String(f.name);
        const proto::ProtoString* ks = k ? k->asString(ctx) : nullptr;
        if (fn && ks) globalObj = globalObj->setAttribute(ctx, ks, fn);
    }
    return globalObj;
}

}  // namespace protojs
