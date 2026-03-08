#include "console.h"
#include "ProtoNativeModule.h"
#include <iostream>
#include <string>

namespace protojs {

namespace {

/** Convert a single ProtoObject to its string representation for printing. */
static void printProtoValue(proto::ProtoContext* ctx, const proto::ProtoObject* val,
                             std::ostream& out) {
    if (!ctx || !val || val == PROTO_NONE || val->isNone(ctx)) {
        out << "undefined";
        return;
    }
    if (val->isString(ctx)) {
        const proto::ProtoString* s = val->asString(ctx);
        if (s) {
            std::string tmp;
            s->toUTF8String(ctx, tmp);
            out << tmp;
        }
        return;
    }
    if (val->isBoolean(ctx)) {
        out << (val->asBoolean(ctx) ? "true" : "false");
        return;
    }
    if (val->isInteger(ctx)) {
        out << val->asLong(ctx);
        return;
    }
    if (val->isDouble(ctx)) {
        out << val->asDouble(ctx);
        return;
    }
    /* Objects, arrays, etc. */
    out << "[object Object]";
}

} // anonymous namespace

const proto::ProtoObject* Console::log(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* /*self*/,
                                        const proto::ParentLink* /*parentLink*/,
                                        const proto::ProtoList* args,
                                        const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cout << " ";
        printProtoValue(ctx, args->getAt(ctx, i), std::cout);
    }
    std::cout << "\n";
    std::cout.flush();
    return PROTO_NONE;
}

const proto::ProtoObject* Console::error(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* /*self*/,
                                          const proto::ParentLink* /*parentLink*/,
                                          const proto::ProtoList* args,
                                          const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cerr << " ";
        printProtoValue(ctx, args->getAt(ctx, i), std::cerr);
    }
    std::cerr << "\n";
    return PROTO_NONE;
}

const proto::ProtoObject* Console::warn(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* /*self*/,
                                         const proto::ParentLink* /*parentLink*/,
                                         const proto::ProtoList* args,
                                         const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    std::cerr << "Warning: ";
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cerr << " ";
        printProtoValue(ctx, args->getAt(ctx, i), std::cerr);
    }
    std::cerr << "\n";
    return PROTO_NONE;
}

void Console::init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj) {
    if (!ctx || !globalObj) return;
    static const NativeEntry entries[] = {
        {"log",   Console::log},
        {"error", Console::error},
        {"warn",  Console::warn},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* consoleObj =
        ProtoNativeModule::buildModule(ctx, entries, 3);
    if (!consoleObj) return;
    globalObj = ProtoNativeModule::registerOnGlobal(ctx, globalObj, "console", consoleObj);
}

} // namespace protojs
