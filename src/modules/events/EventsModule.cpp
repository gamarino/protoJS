#include "EventsModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../runtime/ProtoInterpreter.h"
#include <string>

namespace protojs {

namespace {

// Each EventEmitter instance carries `__listeners__`: a mutable
// ProtoObject whose attribute names are event names and whose values
// are Arrays of callbacks.  This is the natural protoCore-native
// equivalent of the QuickJS-side `std::map<string, vector<JSValue>>`
// the original used — and it keeps every callback reachable through
// the GC graph automatically (via the instance → __listeners__ →
// array → element chain), so we don't need a finalizer.
const proto::ProtoString* listenersKey(proto::ProtoContext* ctx) {
    // Re-intern per call rather than caching: when an EE instance is
    // accessed from a callback running in a wrapper that is not the
    // one that originally registered the listener (worker_threads
    // crosses main/worker wrappers in the same thread), a thread_local
    // cache would return the wrong SymbolTable's "__listeners__"
    // pointer.  createSymbol is a sharded hash lookup; cost is
    // negligible.
    return proto::ProtoString::createSymbol(ctx, "__listeners__");
}

// Resolve `this` to a writable handle on the EventEmitter instance.
// In protoCore, `this` for a method dispatched via prototype lookup
// is the receiver object.
const proto::ProtoObject* getThis(const proto::ProtoObject* self) {
    return (self && self != PROTO_NONE) ? self : nullptr;
}

const proto::ProtoObject* listenersFor(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* self,
                                        bool create) {
    const proto::ProtoString* k = listenersKey(ctx);
    if (!k) return nullptr;
    const proto::ProtoObject* attr = self->getAttribute(ctx, k, false);
    if (attr && attr != PROTO_NONE) return attr;
    if (!create) return nullptr;
    const proto::ProtoObject* fresh = ctx->newObject(/*mutable=*/true);
    self->setAttribute(ctx, k, fresh);
    return fresh;
}

const proto::ProtoString* eventName(proto::ProtoContext* ctx,
                                     const proto::ProtoList* args) {
    if (!ctx || !args || args->getSize(ctx) == 0) return nullptr;
    const proto::ProtoObject* a = args->getAt(ctx, 0);
    if (!a || !a->isString(ctx)) return nullptr;
    std::string s;
    a->asString(ctx)->toUTF8String(ctx, s);
    return proto::ProtoString::createSymbol(ctx, s.c_str());
}

// A listener added with once() is stored as a record holding the handler, so
// emit() can remove it before calling it and removeListener() can still find
// it by the handler the caller passed.
const proto::ProtoString* onceKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__ee_once_listener__");
}

// The handler a stored entry stands for: the record's handler for a once()
// entry, the entry itself otherwise.
const proto::ProtoObject* handlerOf(proto::ProtoContext* ctx, const proto::ProtoObject* entry) {
    if (!entry || entry == PROTO_NONE) return entry;
    const proto::ProtoString* k = onceKey(ctx);
    const proto::ProtoObject* inner = k ? entry->getOwnAttributeDirect(ctx, k) : nullptr;
    return (inner && inner != PROTO_NONE) ? inner : entry;
}

bool isOnceEntry(proto::ProtoContext* ctx, const proto::ProtoObject* entry) {
    return handlerOf(ctx, entry) != entry;
}

// The listener array for an event, or nullptr.
const proto::ProtoObject* listenerArray(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* self,
                                        const proto::ProtoString* nameK,
                                        bool create) {
    const proto::ProtoObject* listeners = listenersFor(ctx, self, create);
    if (!listeners) return nullptr;
    const proto::ProtoObject* arr = listeners->getAttribute(ctx, nameK, false);
    if (arr && arr != PROTO_NONE) return arr;
    if (!create) return nullptr;
    arr = createNewArray(ctx, nullptr);
    if (!arr) return nullptr;
    setArrayElements(ctx, arr, ctx->newList());
    listeners->setAttribute(ctx, nameK, arr);
    return arr;
}

// Replaces an event's listeners (setArrayElements only grows `length`).
void storeListeners(proto::ProtoContext* ctx, const proto::ProtoObject* arr,
                    const proto::ProtoList* els) {
    setArrayElements(ctx, arr, els);
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) arr->setAttribute(ctx, lk,
        ctx->fromInteger(static_cast<long long>(els->getSize(ctx))));
}

// Removes the first entry for `handler` (a plain entry or a once() record).
bool removeFirst(proto::ProtoContext* ctx, const proto::ProtoObject* arr,
                 const proto::ProtoObject* handler, bool matchEntryIdentity) {
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els) return false;
    const proto::ProtoList* out = ctx->newList();
    bool removed = false;
    const long long n = static_cast<long long>(els->getSize(ctx));
    for (long long i = 0; i < n; ++i) {
        const proto::ProtoObject* cb = els->getAt(ctx, static_cast<int>(i));
        const bool match = matchEntryIdentity ? (cb == handler)
                                              : (cb == handler || handlerOf(ctx, cb) == handler);
        if (!removed && match) { removed = true; continue; }
        out = out->appendLast(ctx, cb);
    }
    if (removed) storeListeners(ctx, arr, out);
    return removed;
}

const proto::ProtoObject* addListener(proto::ProtoContext* ctx,
                                      const proto::ProtoObject* self,
                                      const proto::ProtoList* args,
                                      bool once) {
    self = getThis(self);
    if (!self || !ctx || !args || args->getSize(ctx) < 2) return self ? self : PROTO_NONE;
    const proto::ProtoString* nameK = eventName(ctx, args);
    if (!nameK) return self;
    const proto::ProtoObject* handler = args->getAt(ctx, 1);
    if (!handler || handler == PROTO_NONE) return self;
    const proto::ProtoObject* entry = handler;
    if (once) {
        const proto::ProtoString* k = onceKey(ctx);
        const proto::ProtoObject* rec = ctx->newObject(/*mutable=*/false);
        if (!k || !rec) return self;
        entry = rec->setAttribute(ctx, k, handler);
    }
    const proto::ProtoObject* arr = listenerArray(ctx, self, nameK, /*create=*/true);
    if (!arr) return self;
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els) els = ctx->newList();
    storeListeners(ctx, arr, els->appendLast(ctx, entry));
    return self;
}

// ---- ProtoMethods ---------------------------------------------------

const proto::ProtoObject* eeOn(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return addListener(ctx, self, args, /*once=*/false);
}

// once(name, fn): fn runs for the next emit of `name` only, and is removed
// before it runs (as in Node, so an emit from inside fn does not call it again).
const proto::ProtoObject* eeOnce(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return addListener(ctx, self, args, /*once=*/true);
}

const proto::ProtoObject* eeEmit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    self = getThis(self);
    if (!self || !ctx || !args || args->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoString* nameK = eventName(ctx, args);
    if (!nameK) return PROTO_FALSE;

    const proto::ProtoObject* arr = listenerArray(ctx, self, nameK, /*create=*/false);
    const proto::ProtoList* els = arr ? getArrayElements(ctx, arr) : nullptr;
    const long long n = els ? static_cast<long long>(els->getSize(ctx)) : 0;
    if (n == 0) {
        // Node: an 'error' event nobody listens to is thrown.
        std::string name;
        nameK->toUTF8String(ctx, name);
        if (name == "error") {
            const proto::ProtoObject* err = args->getSize(ctx) > 1 ? args->getAt(ctx, 1) : nullptr;
            signalNativeException((err && err != PROTO_NONE)
                ? err
                : makeNativeError(ctx, "Error", "Unhandled error."));
            return PROTO_NONE;
        }
        return PROTO_FALSE;
    }

    const proto::ProtoList* cbArgs = ctx->newList();
    const long long argc = static_cast<long long>(args->getSize(ctx));
    for (long long i = 1; i < argc; ++i)
        cbArgs = cbArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
    // `els` is the list as it was when emit began: listeners added or removed
    // by a listener take effect from the next emit, as in Node.
    for (long long i = 0; i < n; ++i) {
        const proto::ProtoObject* entry = els->getAt(ctx, static_cast<int>(i));
        if (!entry || entry == PROTO_NONE) continue;
        const proto::ProtoObject* cb = handlerOf(ctx, entry);
        if (cb != entry) removeFirst(ctx, arr, entry, /*matchEntryIdentity=*/true);
        callJSFunction(ctx, cb, self, cbArgs);
        if (hasCallException()) return PROTO_NONE;  // a listener threw: propagate
    }
    return PROTO_TRUE;
}

const proto::ProtoObject* eeRemoveListener(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    self = getThis(self);
    if (!self || !ctx || !args || args->getSize(ctx) < 2) return self ? self : PROTO_NONE;
    const proto::ProtoString* nameK = eventName(ctx, args);
    if (!nameK) return self;
    const proto::ProtoObject* handler = args->getAt(ctx, 1);
    if (!handler || handler == PROTO_NONE) return self;
    // Node removes the most recently added matching listener; protoJS removes
    // the first, which is the same whenever a handler is added once.
    if (const proto::ProtoObject* arr = listenerArray(ctx, self, nameK, /*create=*/false))
        removeFirst(ctx, arr, handler, /*matchEntryIdentity=*/false);
    return self;
}

const proto::ProtoObject* eeRemoveAllListeners(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    self = getThis(self);
    if (!self || !ctx) return self ? self : PROTO_NONE;
    const proto::ProtoString* k = listenersKey(ctx);
    if (!args || args->getSize(ctx) == 0 || args->getAt(ctx, 0) == getUndefinedSentinel()) {
        if (k && listenersFor(ctx, self, false)) self->setAttribute(ctx, k, ctx->newObject(true));
        return self;
    }
    const proto::ProtoString* nameK = eventName(ctx, args);
    if (nameK) {
        if (const proto::ProtoObject* arr = listenerArray(ctx, self, nameK, false))
            storeListeners(ctx, arr, ctx->newList());
    }
    return self;
}

const proto::ProtoObject* eeListenerCount(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    self = getThis(self);
    if (!self || !ctx) return ctx ? ctx->fromInteger(0) : PROTO_NONE;
    const proto::ProtoString* nameK = eventName(ctx, args);
    const proto::ProtoObject* arr = nameK ? listenerArray(ctx, self, nameK, false) : nullptr;
    const proto::ProtoList* els = arr ? getArrayElements(ctx, arr) : nullptr;
    return ctx->fromInteger(els ? static_cast<long long>(els->getSize(ctx)) : 0);
}

const proto::ProtoObject* eeListeners(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    if (!result) return PROTO_NONE;
    const proto::ProtoList* out = ctx->newList();
    self = getThis(self);
    const proto::ProtoString* nameK = (self && ctx) ? eventName(ctx, args) : nullptr;
    const proto::ProtoObject* arr = nameK ? listenerArray(ctx, self, nameK, false) : nullptr;
    const proto::ProtoList* els = arr ? getArrayElements(ctx, arr) : nullptr;
    const long long n = els ? static_cast<long long>(els->getSize(ctx)) : 0;
    for (long long i = 0; i < n; ++i)
        out = out->appendLast(ctx, handlerOf(ctx, els->getAt(ctx, static_cast<int>(i))));
    storeListeners(ctx, result, out);
    return result;
}

const proto::ProtoObject* eeSetMaxListeners(
    proto::ProtoContext*,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    // No leak warning is implemented, so the limit has nothing to govern.
    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* eeGetMaxListeners(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return ctx ? ctx->fromInteger(10) : PROTO_NONE;  // Node's default
}

const proto::ProtoObject* eeConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*);

}  // namespace

const proto::ProtoObject* EventsModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    // 1. Build the prototype object holding the instance methods.
    static const NativeEntry protoEntries[] = {
        {"on",                 eeOn},
        {"addListener",        eeOn},
        {"once",               eeOnce},
        {"emit",               eeEmit},
        {"removeListener",     eeRemoveListener},
        {"off",                eeRemoveListener},
        {"removeAllListeners", eeRemoveAllListeners},
        {"listenerCount",      eeListenerCount},
        {"listeners",          eeListeners},
        {"setMaxListeners",    eeSetMaxListeners},
        {"getMaxListeners",    eeGetMaxListeners},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* eeProto =
        ProtoNativeModule::buildModule(ctx, protoEntries, 11);
    if (!eeProto) return globalObj;

    // 2. Build the constructor as a wrapNativeFunction wrapper —
    //    OP_call_constructor's default-dispatch branch looks for
    //    `__native_fn__` on the func and invokes that with the
    //    pre-created `newObj` (whose parent is already
    //    func.prototype) as `self`.  See ProtoInterpreter.cpp's
    //    OP_call_constructor implementation.
    const proto::ProtoObject* eeCtor =
        wrapNativeFunction(ctx, eeConstructor, "EventEmitter",
                            /*length=*/0, /*globalRoot=*/nullptr);
    if (!eeCtor) return globalObj;

    // 3. Set the `prototype` attribute so OP_call_constructor's
    //    `new` machinery uses our methods for the new instance.
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) eeCtor = eeCtor->setAttribute(ctx, protoKey, eeProto);
    // OP_call_constructor's default-dispatch branch invokes any
    // `__construct__` method on the constructor.  EventEmitter's
    // body is a no-op (state is built lazily by .on()), but
    // installing the hook makes the dispatch consistent across
    // ProtoCore-native modules.
    {
        const proto::ProtoString* ck =
            ctx->fromUTF8String("__construct__")->asString(ctx);
        if (ck) eeCtor = eeCtor->setAttribute(ctx, ck,
            ctx->fromMethod(nullptr, eeConstructor));
    }

    // 4. The module IS the constructor, as in Node: require('events') returns
    //    EventEmitter, and EventEmitter.EventEmitter is EventEmitter, so both
    //    `const EventEmitter = require('events')` and
    //    `const { EventEmitter } = require('events')` work. The wrapper is
    //    mutable, so the attribute is written in place; the self-reference is
    //    a cycle the collector keeps, which costs nothing for an object that
    //    lives as long as the global anyway.
    const proto::ProtoString* eeKey =
        ctx->fromUTF8String("EventEmitter")->asString(ctx);
    if (eeKey) eeCtor = eeCtor->setAttribute(ctx, eeKey, eeCtor);
    const proto::ProtoString* dmlKey =
        ctx->fromUTF8String("defaultMaxListeners")->asString(ctx);
    if (dmlKey) eeCtor = eeCtor->setAttribute(ctx, dmlKey, ctx->fromInteger(10));

    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "events", eeCtor);
}

namespace {

// Constructor: receives the pre-built `newObj` (already parented on
// EventEmitter.prototype by OP_call_constructor) as `self`.  Returns
// PROTO_NONE so the runtime keeps `newObj` as the constructed value;
// listeners are added lazily on the first `.on()` call.
const proto::ProtoObject* eeConstructor(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

}  // namespace

} // namespace protojs
