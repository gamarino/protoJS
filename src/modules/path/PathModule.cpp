#include "PathModule.h"
#include "../../ProtoNativeModule.h"
#include <filesystem>
#include <string>
#include <vector>

namespace protojs {
namespace fs = std::filesystem;

namespace {

// Helpers ----------------------------------------------------------------

// Read argument N as a UTF-8 std::string, or return an empty optional
// when the slot is missing or non-string.  Throwing TypeError for
// missing args matches the QuickJS-side behaviour we replaced.
struct ArgString {
    bool ok;
    std::string value;
};

ArgString arg(proto::ProtoContext* ctx, const proto::ProtoList* args,
              int idx) {
    if (!ctx || !args) return {false, ""};
    if (idx >= static_cast<int>(args->getSize(ctx))) return {false, ""};
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || a == PROTO_NONE) return {false, ""};
    if (!a->isString(ctx)) return {false, ""};
    std::string out;
    a->asString(ctx)->toUTF8String(ctx, out);
    return {true, out};
}

const proto::ProtoObject* str(proto::ProtoContext* ctx,
                               const std::string& s) {
    return ctx->fromUTF8String(s.c_str());
}

// ProtoMethods ------------------------------------------------------------

const proto::ProtoObject* pathJoin(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args) return ctx ? ctx->fromUTF8String(".") : PROTO_NONE;
    fs::path result;
    bool any = false;
    long long n = static_cast<long long>(args->getSize(ctx));
    for (long long i = 0; i < n; ++i) {
        ArgString a = arg(ctx, args, static_cast<int>(i));
        if (!a.ok || a.value.empty()) continue;
        result /= a.value;
        any = true;
    }
    if (!any) return ctx->fromUTF8String(".");
    return str(ctx, result.string());
}

const proto::ProtoObject* pathResolve(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    fs::path result = fs::current_path();
    if (args) {
        long long n = static_cast<long long>(args->getSize(ctx));
        for (long long i = 0; i < n; ++i) {
            ArgString a = arg(ctx, args, static_cast<int>(i));
            if (!a.ok) continue;
            result /= a.value;
        }
    }
    try {
        result = fs::canonical(result);
    } catch (...) {
        result = fs::absolute(result);
    }
    return str(ctx, result.string());
}

const proto::ProtoObject* pathNormalize(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ArgString a = arg(ctx, args, 0);
    if (!a.ok) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    return str(ctx, fs::path(a.value).lexically_normal().string());
}

const proto::ProtoObject* pathDirname(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ArgString a = arg(ctx, args, 0);
    if (!a.ok) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    return str(ctx, fs::path(a.value).parent_path().string());
}

const proto::ProtoObject* pathBasename(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ArgString a = arg(ctx, args, 0);
    if (!a.ok) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    return str(ctx, fs::path(a.value).filename().string());
}

const proto::ProtoObject* pathExtname(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ArgString a = arg(ctx, args, 0);
    if (!a.ok) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    return str(ctx, fs::path(a.value).extension().string());
}

const proto::ProtoObject* pathIsAbsolute(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ArgString a = arg(ctx, args, 0);
    if (!a.ok) return PROTO_FALSE;
    return fs::path(a.value).is_absolute() ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* pathRelative(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    ArgString from = arg(ctx, args, 0);
    ArgString to   = arg(ctx, args, 1);
    if (!from.ok || !to.ok) return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    try {
        return str(ctx, fs::relative(fs::path(to.value), fs::path(from.value)).string());
    } catch (...) {
        return ctx ? ctx->fromUTF8String("") : PROTO_NONE;
    }
}

}  // namespace

const proto::ProtoObject* PathModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"join",       pathJoin},
        {"resolve",    pathResolve},
        {"normalize",  pathNormalize},
        {"dirname",    pathDirname},
        {"basename",   pathBasename},
        {"extname",    pathExtname},
        {"isAbsolute", pathIsAbsolute},
        {"relative",   pathRelative},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 8);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "path", mod);
}

} // namespace protojs
