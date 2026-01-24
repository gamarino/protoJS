#ifndef PROTOJS_EVENTSMODULE_H
#define PROTOJS_EVENTSMODULE_H

#include "quickjs.h"

namespace protojs {

class EventsModule {
public:
    static void init(JSContext* ctx);

private:
    // EventEmitter class methods
    static JSValue EventEmitterConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static JSValue on(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue once(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue emit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue removeListener(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void EventEmitterFinalizer(JSRuntime* rt, JSValue val);
};

} // namespace protojs

#endif // PROTOJS_EVENTSMODULE_H
