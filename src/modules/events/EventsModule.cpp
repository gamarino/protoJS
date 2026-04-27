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
    static thread_local const proto::ProtoString* k = nullptr;
    if (!k) k = proto::ProtoString::createSymbol(ctx, "__listeners__");
    return k;
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

// ---- ProtoMethods ---------------------------------------------------

const proto::ProtoObject* eeOn(
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

    const proto::ProtoObject* listeners = listenersFor(ctx, self, /*create=*/true);
    if (!listeners) return self;

    // Get-or-create the array for this event.
    const proto::ProtoObject* arr = listeners->getAttribute(ctx, nameK, false);
    if (!arr || arr == PROTO_NONE) {
        arr = createNewArray(ctx, nullptr);
        if (!arr) return self;
        setArrayElements(ctx, arr, ctx->newList());
        listeners->setAttribute(ctx, nameK, arr);
    }
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els) els = ctx->newList();
    setArrayElements(ctx, arr, els->appendLast(ctx, handler));
    return self;
}

const proto::ProtoObject* eeOnce(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    // The original QuickJS-side `once` was just an alias for `on` —
    // there was no actual one-shot semantics either.  Preserve that
    // behaviour.  A real once() that auto-removes the listener after
    // the first emit is tracked separately.
    return eeOn(ctx, self, pl, args, kw);
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

    const proto::ProtoObject* listeners = listenersFor(ctx, self, /*create=*/false);
    if (!listeners) return PROTO_FALSE;
    const proto::ProtoObject* arr = listeners->getAttribute(ctx, nameK, false);
    if (!arr || arr == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els) return PROTO_FALSE;

    // Build the callback args list (everything past index 0).
    const proto::ProtoList* cbArgs = ctx->newList();
    long long argc = static_cast<long long>(args->getSize(ctx));
    for (long long i = 1; i < argc; ++i) {
        cbArgs = cbArgs->appendLast(ctx, args->getAt(ctx, static_cast<int>(i)));
    }
    long long n = static_cast<long long>(els->getSize(ctx));
    for (long long i = 0; i < n; ++i) {
        const proto::ProtoObject* cb = els->getAt(ctx, static_cast<int>(i));
        if (cb && cb != PROTO_NONE) {
            callJSFunction(ctx, cb, self, cbArgs);
        }
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

    const proto::ProtoObject* listeners = listenersFor(ctx, self, /*create=*/false);
    if (!listeners) return self;
    const proto::ProtoObject* arr = listeners->getAttribute(ctx, nameK, false);
    if (!arr || arr == PROTO_NONE) return self;
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els) return self;

    // Filter out the FIRST handler that matches by pointer identity
    // (matches the original QuickJS-side semantics, which removed only
    // the first occurrence too).
    const proto::ProtoList* out = ctx->newList();
    bool removed = false;
    long long n = static_cast<long long>(els->getSize(ctx));
    for (long long i = 0; i < n; ++i) {
        const proto::ProtoObject* cb = els->getAt(ctx, static_cast<int>(i));
        if (!removed && cb == handler) {
            removed = true;
            continue;
        }
        out = out->appendLast(ctx, cb);
    }
    if (removed) setArrayElements(ctx, arr, out);
    return self;
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
        {"on",             eeOn},
        {"once",           eeOnce},
        {"emit",           eeEmit},
        {"removeListener", eeRemoveListener},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* eeProto =
        ProtoNativeModule::buildModule(ctx, protoEntries, 4);
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

    // 4. Build the `events` module object exposing the constructor.
    const proto::ProtoObject* mod = ctx->newObject(/*mutable=*/true);
    if (!mod) return globalObj;
    const proto::ProtoString* eeKey =
        ctx->fromUTF8String("EventEmitter")->asString(ctx);
    if (eeKey) mod->setAttribute(ctx, eeKey, eeCtor);

    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "events", mod);
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
