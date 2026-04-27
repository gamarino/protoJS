#include "EventLoopBindings.h"
#include "EventLoop.h"
#include "JSContext.h"
#include "runtime/ProtoInterpreter.h"
#include "runtime/ProtoBytecodeModule.h"

namespace protojs {

namespace {

// Internal sentinel attribute on the protoCore-native global that holds
// the queue of pending setImmediate callbacks.  Storing the queue as an
// attribute on the global gives it GC reachability for free — the global
// is already a GC root via the wrapper's nativeGlobalRoot_ pointer.
//
// The queue is a ProtoList (persistent immutable; appendLast / removeFirst
// produce new versions).  Mutation is single-threaded: setImmediate runs
// on the JS thread, and the event loop also drains on the JS thread, so
// no locking is required.
constexpr const char* kQueueAttr = "__protojs_immediate_queue__";

const proto::ProtoString* queueKey(proto::ProtoContext* ctx) {
    static thread_local const proto::ProtoString* key = nullptr;
    if (!key) {
        key = proto::ProtoString::createSymbol(ctx, kQueueAttr);
    }
    return key;
}

const proto::ProtoList* readQueue(proto::ProtoContext* ctx,
                                   const proto::ProtoObject* globalObj) {
    if (!globalObj) return nullptr;
    const proto::ProtoString* k = queueKey(ctx);
    if (!k) return nullptr;
    const proto::ProtoObject* attr = globalObj->getAttribute(ctx, k, false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    return attr->asList(ctx);
}

void writeQueue(proto::ProtoContext* ctx,
                 const proto::ProtoObject* globalObj,
                 const proto::ProtoList* list) {
    if (!globalObj || !list) return;
    const proto::ProtoString* k = queueKey(ctx);
    if (!k) return;
    globalObj->setAttribute(ctx, k, list->asObject(ctx));
}

// One event-loop callback drains exactly one immediate.  We schedule one
// per setImmediate call rather than draining all in a single callback —
// matches Node's semantics where I/O callbacks can interleave with
// setImmediate callbacks across event-loop turns.
//
// `wrapper` is captured at enqueue time because EventLoop callbacks fire
// AFTER the eval() that scheduled them has returned, by which point
// JSContextWrapper::current() has reverted to nullptr (eval()'s
// WrapperScope is gone).  We re-publish the wrapper as the thread's
// current via CurrentScope so callJSFunction and any nested code can
// find it again.
void runOneImmediate(JSContextWrapper* wrapper) {
    if (!wrapper) return;
    JSContextWrapper::CurrentScope wscope(wrapper);
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    if (!ctx) return;
    const proto::ProtoObject* global = wrapper->getNativeGlobal();
    if (!global) return;

    const proto::ProtoList* queue = readQueue(ctx, global);
    if (!queue || queue->getSize(ctx) == 0) return;

    // Pin the callback in the wrapper's protoCore root set across
    // the unavoidable allocations between removeFirst and dispatch
    // (newList, child ProtoContext, callback's own bytecode entry).
    // Any of those can trigger GC, and at that point `callback` is
    // only on the C++ stack — without an explicit root the GC would
    // reclaim it.
    const proto::ProtoObject* callback = queue->getFirst(ctx);
    const proto::ProtoList* rest = queue->removeFirst(ctx);
    if (rest) writeQueue(ctx, global, rest);
    if (!callback || callback == PROTO_NONE) return;

    proto::ProtoRootSet* rs = wrapper->getRootSet();
    proto::ProtoRootSet::Handle pin =
        rs ? rs->add(callback) : proto::ProtoRootSet::kNullHandle;

    const ProtoBytecodeModule* mod =
        static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
    callJSFunctionFromAsync(ctx, callback, PROTO_NONE, ctx->newList(),
                            mod, wrapper->getNativeGlobalRootPtr());

    if (rs) rs->remove(pin);
}

}  // namespace

const proto::ProtoObject* EventLoopBindings::setImmediateNative(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* callback = args->getAt(ctx, 0);
    if (!callback || callback == PROTO_NONE) return PROTO_NONE;

    // Append to the per-global pending queue (GC-reachable through global).
    JSContextWrapper* w = JSContextWrapper::current();
    if (!w) return PROTO_NONE;
    const proto::ProtoObject* global = w->getNativeGlobal();
    if (!global) return PROTO_NONE;

    const proto::ProtoList* queue = readQueue(ctx, global);
    if (!queue) queue = ctx->newList();
    queue = queue->appendLast(ctx, callback);
    writeQueue(ctx, global, queue);

    EventLoop::getInstance().enqueueCallback([w]() { runOneImmediate(w); });
    return PROTO_NONE;
}

const proto::ProtoObject* EventLoopBindings::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    const proto::ProtoObject* fn = ctx->fromMethod(nullptr, setImmediateNative);
    if (!fn) return globalObj;
    const proto::ProtoString* name = ctx->fromUTF8String("setImmediate")
        ? ctx->fromUTF8String("setImmediate")->asString(ctx)
        : nullptr;
    if (!name) return globalObj;
    return globalObj->setAttribute(ctx, name, fn);
}

}  // namespace protojs
