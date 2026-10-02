#include "ProtoDeferred.h"
#include "DeferredPool.h"
#include "runtime/ThreadIdentity.h"
#include "runtime/PinnedBuiltin.h"
#include "EventLoop.h"
#include "FunctionPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "PromisePrototype.h"
#include "MicrotaskQueue.h"
#include "platform/ProcessExit.h"
#include "runtime/ProtoInterpreter.h"
#include "runtime/ProtoBytecodeModule.h"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <string>

namespace protojs {

// A value is callable when it is a raw ProtoMethod, a wrapped native
// function, a bytecode closure or a bound function.  Same test the
// interpreter applies to Proxy traps (ProtoInterpreter.cpp).
bool isCallableValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!ctx || !v || v == PROTO_NONE) return false;
    if (v->isMethod(ctx)) return true;
    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
    if (bcK && v->hasAttribute(ctx, bcK) == PROTO_TRUE) return true;
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (nfK && v->hasAttribute(ctx, nfK) == PROTO_TRUE) return true;
    const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
    if (bfK && v->hasAttribute(ctx, bfK) == PROTO_TRUE) return true;
    return false;
}

namespace {

// "<name>: <message>" for an Error-like value; the value itself for a string or
// a number; "Error" when nothing describes it.
std::string describeThrownValue(proto::ProtoContext* ctx, const proto::ProtoObject* exc) {
    std::string errStr;
    if (!exc || exc == PROTO_NONE) return "undefined";
    if (ctx) {
        if (exc->isString(ctx)) {
            exc->asString(ctx)->toUTF8String(ctx, errStr);
            return errStr;
        }
        if (exc->isInteger(ctx)) return std::to_string(exc->asLong(ctx));
        if (exc->isDouble(ctx)) {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%.15g", exc->asDouble(ctx));
            return buf;
        }
        if (exc == getUndefinedSentinel()) return "undefined";
        if (exc == getNullSentinel()) return "null";
        if (exc->isBoolean(ctx)) return exc == PROTO_TRUE ? "true" : "false";
        const proto::ProtoString* nameKey = JSSymbols::name(ctx);
        if (nameKey) {
            const proto::ProtoObject* nv = exc->getAttribute(ctx, nameKey, true);
            if (nv && nv != PROTO_NONE && nv->isString(ctx))
                nv->asString(ctx)->toUTF8String(ctx, errStr);
        }
        const proto::ProtoString* msgKey = JSSymbols::message(ctx);
        if (msgKey) {
            const proto::ProtoObject* mv = exc->getAttribute(ctx, msgKey, true);
            if (mv && mv != PROTO_NONE && mv->isString(ctx)) {
                std::string tmp;
                mv->asString(ctx)->toUTF8String(ctx, tmp);
                if (!tmp.empty()) {
                    if (!errStr.empty()) errStr += ": ";
                    errStr += tmp;
                }
            }
        }
    }
    if (errStr.empty()) errStr = "Error";
    return errStr;
}

}  // namespace

// Consume and report an exception left behind by a user callback.
//
// callJSFunction reports a throw by setting a thread-local flag and returning
// PROTO_NONE.  Nothing in the event loop consumes that flag, so leaving it set
// made the next native call on this thread believe that IT had thrown.  Every
// site that invokes a user callback must drain it.  Declared in
// ProtoDeferred.h for the other modules that invoke callbacks (fs).
//
// The exception reached the top of a callback, so nothing will catch it: as in
// Node, the process ends with status 1.  Pre-fix the message was printed and
// the program carried on to exit with status 0.
void drainCallbackException(proto::ProtoContext* ctx, const char* where) {
    if (!hasCallException()) return;
    const proto::ProtoObject* exc = consumeCallException();
    std::cerr << "Uncaught exception in " << where << ": "
              << describeThrownValue(ctx, exc) << std::endl;
    platform::exitNow(1);
}

void endOfTurnChecks(proto::ProtoContext* ctx) {
    drainCallbackException(ctx, "event-loop callback");
    // Unhandled rejections are reported by the microtask checkpoint that ends
    // every turn (MicrotaskQueue.h); run it here too for a turn that queued
    // jobs without passing through the event loop.
    MicrotaskQueue::checkpointThread();
}

void reportUnhandledRejection(proto::ProtoContext* ctx, const proto::ProtoObject* reason,
                              bool fatal) {
    std::cerr << "Uncaught (in promise) " << describeThrownValue(ctx, reason) << std::endl;
    if (fatal) platform::exitNow(1);
}

bool refuseOnDeferredThread(proto::ProtoContext* ctx, const char* what) {
    if (!JSContextWrapper::onPoolThread()) return false;
    std::string msg = std::string(what) + " is not available inside a Deferred function";
    signalNativeException(makeNativeError(ctx, "Error", msg.c_str()));
    return true;
}

namespace {

// Pending Deferreds, for the event-loop drain in main.cpp: the process stays
// alive while a Deferred's function runs on the pool or a native producer
// (runInThread, io.*Async) still owes a settlement. Atomic: incremented on
// the owner thread, decremented by the settling job on the owner thread, but
// read by the drain loop between turns.
std::atomic<int> g_activeCount{0};

// %Deferred.prototype%: a child of %Promise.prototype%, one per space.
PinnedBuiltin s_deferredPrototype;

const proto::ProtoObject* deferredPrototypeObject(proto::ProtoContext* ctx) {
    if (const proto::ProtoObject* cached = s_deferredPrototype.get(ctx)) return cached;
    const proto::ProtoObject* promiseProto = intrinsicPromisePrototype(ctx);
    const proto::ProtoObject* p = promiseProto ? promiseProto->newChild(ctx, true)
                                               : ctx->newObject(true);
    if (!p) return nullptr;
    // No own `constructor`: Deferred.prototype inherits Promise.prototype's,
    // so `d.constructor === Promise`. That is deliberate. then/catch/finally
    // derive their result through SpeciesConstructor(d, %Promise%), and a
    // Deferred constructor there would be handed the executor of a
    // NewPromiseCapability and run it on a pool thread. With %Promise% they
    // return ordinary promises, and `await d` takes the native-promise path.
    // Object.prototype.toString reports "[object Deferred]".
    if (const proto::ProtoString* tag = JSSymbols::symbolToStringTag(ctx)) {
        p->setAttribute(ctx, tag, ctx->fromUTF8String("Deferred"));
        p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__pd_Symbol.toStringTag__"),
                        ctx->fromInteger(0x2LL));
    }
    if (const proto::ProtoString* tag = JSSymbols::toStringTag(ctx))
        p->setAttribute(ctx, tag, ctx->fromUTF8String("Deferred"));
    return s_deferredPrototype.keep(ctx, p);
}

const proto::ProtoObject* newDeferredObject(proto::ProtoContext* ctx) {
    const proto::ProtoObject* proto = deferredPrototypeObject(ctx);
    return proto ? newPromiseWithPrototype(ctx, proto) : nullptr;
}

// Run `fn` here and now, on the calling thread, and settle `d` with the
// outcome. Used where the pool cannot take the task: inside a Deferred's own
// function (a pool thread), and when no pool thread could be started.
void runInline(proto::ProtoContext* ctx, const proto::ProtoObject* fn,
               const proto::ProtoObject* d) {
    const proto::ProtoObject* u = getUndefinedSentinel();
    const proto::ProtoObject* result = callJSFunction(ctx, fn, u ? u : PROTO_NONE, ctx->newList());
    if (hasCallException()) {
        const proto::ProtoObject* reason = consumeCallException();
        rejectPromise(ctx, d, reason ? reason : PROTO_NONE);
    } else {
        resolvePromise(ctx, d, result ? result : PROTO_NONE);
    }
}

// new Deferred(fn) / Deferred(fn): run fn on the Deferred pool (DeferredPool.h)
// and return a promise of this thread that settles with its outcome.
const proto::ProtoObject* deferredConstruct(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* fn =
        (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;

    // Reject a missing or non-callable function synchronously, rather than
    // create a Deferred that nothing can ever settle.
    if (!isCallableValue(ctx, fn)) {
        signalNativeException(makeNativeError(
            ctx, "TypeError", "Deferred requires a function argument"));
        return PROTO_NONE;
    }

    const proto::ProtoObject* d = newDeferredObject(ctx);
    if (!d) return PROTO_NONE;

    JSContextWrapper* wrapper = JSContextWrapper::current();
    // Inside a Deferred's function (a pool thread) the Deferred runs inline:
    // a pool thread does not wait for other pool threads, so nested Deferreds
    // can never deadlock the pool, and library code that uses Deferred
    // internally still works when it is itself called from a Deferred.
    if (!wrapper || JSContextWrapper::onPoolThread()) {
        runInline(ctx, fn, d);
        return d;
    }

    proto::ProtoRootSet* rs = wrapper->getRootSet();
    if (!rs) {
        runInline(ctx, fn, d);
        return d;
    }
    DeferredPool::Task task;
    task.fn = rs->add(fn);
    task.deferred = rs->add(d);
    const proto::ProtoObject** gr = getCurrentGlobalRoot();
    task.global = (gr && *gr) ? *gr : *wrapper->getNativeGlobalRootPtr();
    task.module = currentInterpreterModule();
    if (!task.module) task.module = static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
    task.identity = captureThreadIdentity(ctx);

    g_activeCount.fetch_add(1, std::memory_order_acq_rel);
    if (!wrapper->deferredPool().submit(ctx, task)) {
        g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
        rs->remove(task.fn);
        rs->remove(task.deferred);
        runInline(ctx, fn, d);
    }
    return d;
}

}  // namespace

void ProtoDeferred::taskSettled() {
    g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
}

const proto::ProtoObject* ProtoDeferred::createPending(proto::ProtoContext* ctx) {
    if (!ctx) return nullptr;
    const proto::ProtoObject* d = newDeferredObject(ctx);
    if (!d) return nullptr;
    // Keeps the process alive until resolveFromAsync / rejectFromAsync.
    g_activeCount.fetch_add(1, std::memory_order_acq_rel);
    return d;
}

void ProtoDeferred::resolveFromAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* deferred,
    const proto::ProtoObject* value,
    JSContextWrapper* /*wrapper*/) {
    if (!ctx || !deferred) return;
    if (getPromiseStatePublic(ctx, deferred) != 0) return;  // already settled
    resolvePromise(ctx, deferred, value ? value : PROTO_NONE);
    g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
}

void ProtoDeferred::rejectFromAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* deferred,
    const proto::ProtoObject* reason,
    JSContextWrapper* /*wrapper*/) {
    if (!ctx || !deferred) return;
    if (getPromiseStatePublic(ctx, deferred) != 0) return;
    rejectPromise(ctx, deferred, reason ? reason : PROTO_NONE);
    g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
}

int ProtoDeferred::getActiveCount() {
    return g_activeCount.load(std::memory_order_acquire);
}

const proto::ProtoObject* ProtoDeferred::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    // `new Deferred(fn)` needs a constructor object, not a bare ProtoMethod:
    // L_OP_call_constructor rejects a raw method with "function is not a
    // constructor". A wrapNativeFunction wrapper carrying `prototype` and
    // `__construct__`; plain `Deferred(fn)` works through `__native_fn__`.
    const proto::ProtoObject* ctor =
        wrapNativeFunction(ctx, deferredConstruct, "Deferred",
                            /*length=*/1, /*globalRoot=*/nullptr);
    if (!ctor) return globalObj;

    // The prototype is made on first use: %Promise.prototype% does not exist
    // yet when the runtime globals are installed (the interpreter creates it
    // on the first run). `prototype` is published lazily too, through
    // ensureDeferredPrototype below.
    const proto::ProtoString* constructKey = JSSymbols::construct(ctx);
    if (constructKey) {
        const proto::ProtoObject* cm = ctx->fromMethod(nullptr, deferredConstruct);
        if (cm) ctor = ctor->setAttribute(ctx, constructKey, cm);
    }

    const proto::ProtoString* name = proto::ProtoString::createSymbol(ctx, "Deferred");
    if (!name) return globalObj;
    return globalObj->setAttribute(ctx, name, ctor);
}

void ProtoDeferred::ensurePrototype(proto::ProtoContext* ctx,
                                    const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoString* name = proto::ProtoString::createSymbol(ctx, "Deferred");
    const proto::ProtoObject* ctor = (*globalRoot)->getAttribute(ctx, name, true);
    if (!ctor || ctor == PROTO_NONE) return;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (!protoKey || ctor->getOwnAttributeDirect(ctx, protoKey)) return;
    const proto::ProtoObject* dproto = deferredPrototypeObject(ctx);
    if (!dproto) return;
    // `instanceof Deferred` holds for every Deferred: they are all children of
    // this one object. When the constructor is immutable the write yields a
    // new constructor object, which replaces the old one on the global.
    const proto::ProtoObject* updated = ctor->setAttribute(ctx, protoKey, dproto);
    if (updated && updated != ctor)
        *globalRoot = (*globalRoot)->setAttribute(ctx, name, updated);
}

}  // namespace protojs
