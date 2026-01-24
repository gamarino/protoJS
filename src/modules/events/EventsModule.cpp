#include "EventsModule.h"
#include "../../EventLoop.h"
#include <map>
#include <vector>
#include <string>

namespace protojs {

static JSClassID eventemitter_class_id;

struct EventEmitterData {
    std::map<std::string, std::vector<JSValue>> listeners;
    JSRuntime* rt;
    EventEmitterData(JSRuntime* r) : rt(r) {}
    ~EventEmitterData() {
        for (auto& [event, handlers] : listeners) {
            for (JSValue handler : handlers) {
                JS_FreeValueRT(rt, handler);
            }
        }
    }
};

void EventsModule::init(JSContext* ctx) {
    JS_NewClassID(&eventemitter_class_id);
    JSClassDef classDef = {"EventEmitter", EventEmitterFinalizer};
    JS_NewClass(JS_GetRuntime(ctx), eventemitter_class_id, &classDef);
    
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "on", JS_NewCFunction(ctx, on, "on", 2));
    JS_SetPropertyStr(ctx, proto, "once", JS_NewCFunction(ctx, once, "once", 2));
    JS_SetPropertyStr(ctx, proto, "emit", JS_NewCFunction(ctx, emit, "emit", 1));
    JS_SetPropertyStr(ctx, proto, "removeListener", JS_NewCFunction(ctx, removeListener, "removeListener", 2));
    JS_SetClassProto(ctx, eventemitter_class_id, proto);
    
    JSValue ctor = JS_NewCFunction2(ctx, EventEmitterConstructor, "EventEmitter", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    
    JSValue eventsModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, eventsModule, "EventEmitter", ctor);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "events", eventsModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue EventsModule::EventEmitterConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSValue obj = JS_NewObjectClass(ctx, eventemitter_class_id);
    if (JS_IsException(obj)) return obj;
    auto* data = new EventEmitterData(JS_GetRuntime(ctx));
    JS_SetOpaque(obj, data);
    return obj;
}

void EventsModule::EventEmitterFinalizer(JSRuntime* rt, JSValue val) {
    EventEmitterData* data = static_cast<EventEmitterData*>(JS_GetOpaque(val, eventemitter_class_id));
    if (data) delete data;
}

JSValue EventsModule::on(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "on expects event name and handler");
    const char* eventName = JS_ToCString(ctx, argv[0]);
    if (!eventName) return JS_EXCEPTION;
    if (!JS_IsFunction(ctx, argv[1])) {
        JS_FreeCString(ctx, eventName);
        return JS_ThrowTypeError(ctx, "on expects a function as handler");
    }
    EventEmitterData* data = static_cast<EventEmitterData*>(JS_GetOpaque(this_val, eventemitter_class_id));
    if (!data) {
        JS_FreeCString(ctx, eventName);
        return JS_ThrowTypeError(ctx, "Invalid EventEmitter");
    }
    data->listeners[eventName].push_back(JS_DupValue(ctx, argv[1]));
    JS_FreeCString(ctx, eventName);
    return JS_DupValue(ctx, this_val);
}

JSValue EventsModule::once(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return on(ctx, this_val, argc, argv);
}

JSValue EventsModule::emit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "emit expects event name");
    const char* eventName = JS_ToCString(ctx, argv[0]);
    if (!eventName) return JS_EXCEPTION;
    EventEmitterData* data = static_cast<EventEmitterData*>(JS_GetOpaque(this_val, eventemitter_class_id));
    if (!data) {
        JS_FreeCString(ctx, eventName);
        return JS_ThrowTypeError(ctx, "Invalid EventEmitter");
    }
    auto it = data->listeners.find(eventName);
    if (it != data->listeners.end()) {
        std::vector<JSValue> args;
        for (int i = 1; i < argc; i++) args.push_back(JS_DupValue(ctx, argv[i]));
        for (JSValue handler : it->second) {
            JSValue result = JS_Call(ctx, handler, this_val, args.size(), args.data());
            if (JS_IsException(result)) {}
            JS_FreeValue(ctx, result);
        }
        for (JSValue arg : args) JS_FreeValue(ctx, arg);
    }
    JS_FreeCString(ctx, eventName);
    return JS_NewBool(ctx, true);
}

JSValue EventsModule::removeListener(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "removeListener expects event name and handler");
    const char* eventName = JS_ToCString(ctx, argv[0]);
    if (!eventName) return JS_EXCEPTION;
    EventEmitterData* data = static_cast<EventEmitterData*>(JS_GetOpaque(this_val, eventemitter_class_id));
    if (!data) {
        JS_FreeCString(ctx, eventName);
        return JS_ThrowTypeError(ctx, "Invalid EventEmitter");
    }
    auto it = data->listeners.find(eventName);
    if (it != data->listeners.end() && !it->second.empty()) {
        JS_FreeValueRT(data->rt, it->second[0]);
        it->second.erase(it->second.begin());
    }
    JS_FreeCString(ctx, eventName);
    return JS_DupValue(ctx, this_val);
}

} // namespace protojs
