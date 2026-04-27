#pragma once

// Native protoCore-side bindings for the event-loop user surface.
// Currently exposes:
//   - setImmediate(callback) — enqueues `callback` to run on the next
//     event-loop turn.  callback may be any protoCore callable
//     (bytecode function or ProtoMethod); invocation goes through
//     callJSFunction inside the event-loop callback.
//
// This file replaces the QuickJS-side js_setImmediate that lived in
// main.cpp.  The QuickJS version installed via JS_SetPropertyStr on
// the QuickJS global, which never propagated to the protoCore-native
// global — so user code running through the protoCore interpreter
// observed `typeof setImmediate === 'undefined'`.  Now setImmediate
// is a real ProtoMethod on the protoCore global, callable directly.

#include <protoCore.h>

namespace protojs {

class EventLoopBindings {
public:
    // Install setImmediate (and any future event-loop globals) onto the
    // protoCore-native global.  Returns the (potentially new) global root
    // pointer — the caller must capture it and call
    // wrapper.updateNativeGlobal() so subsequent eval sees the change.
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    // The actual ProtoMethod that backs setImmediate.  Exposed here only
    // because tests may want to call it directly without going through
    // the global lookup.  Not part of the public API.
    static const proto::ProtoObject* setImmediateNative(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* self,
        const proto::ParentLink* parentLink,
        const proto::ProtoList* args,
        const proto::ProtoSparseList* kwargs);
};

}  // namespace protojs
