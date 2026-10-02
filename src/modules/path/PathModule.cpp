/*
 * PathModule — Node's `path` module on the protoCore-native global.
 *
 * The algorithms are Node's own (PathAlgorithms.cpp, a port of Node.js
 * lib/path.js); this file binds them as ProtoMethods and validates the
 * arguments as Node does. The module has the shape Node gives it:
 *
 *   path.win32 and path.posix are the two implementations, each a complete
 *   module with sep, delimiter and every function, and each pointing at both
 *   (path.win32.posix === path.posix, path.posix.win32 === path.win32);
 *   `path` itself is path.win32 on Windows and path.posix everywhere else.
 *
 * Everything is lexical: resolve() only reads the current directory (and on
 * Windows the per-drive current directories in the "=C:" variables), never
 * the file system, and does not follow symbolic links -- as in Node.
 */
#include "PathModule.h"
#include "PathAlgorithms.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../JSSymbols.h"
#include "../../runtime/ProtoInterpreter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace protojs {

namespace {

#if defined(_WIN32)
constexpr bool kHostIsWindows = true;
#else
constexpr bool kHostIsWindows = false;
#endif

// --- Values ----------------------------------------------------------------

const proto::ProtoObject* argAt(proto::ProtoContext* ctx, const proto::ProtoList* args,
                                std::size_t idx) {
    if (!ctx || !args || idx >= static_cast<std::size_t>(args->getSize(ctx))) return PROTO_NONE;
    const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(idx));
    return a ? a : PROTO_NONE;
}

std::size_t argCount(proto::ProtoContext* ctx, const proto::ProtoList* args) {
    return (ctx && args) ? static_cast<std::size_t>(args->getSize(ctx)) : 0;
}

bool isUndefined(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return !v || v == PROTO_NONE || v->isNone(ctx) || v == getUndefinedSentinel();
}

bool isNull(const proto::ProtoObject* v) {
    return v && v == getNullSentinel();
}

// A primitive JavaScript string (not a String object).
bool isJSString(const proto::ProtoObject* v) {
    return v && v != PROTO_NONE && proto::ProtoObject::isStringTagFast(v);
}

std::string utf8Of(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    std::string out;
    const proto::ProtoString* s = v->asString(ctx);
    if (s) s->toUTF8String(ctx, out);
    return out;
}

bool isNumber(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx);
}

// Number::toString for the messages below: the shortest decimal that reads
// back as the same double.
std::string numberText(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (v->isInteger(ctx)) return std::to_string(static_cast<long long>(v->asLong(ctx)));
    const double d = v->asDouble(ctx);
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return "0";
    char buf[32];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof(buf), "%.*g", precision, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    return buf;
}

bool isFunction(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (v->isMethod(ctx)) return true;
    const proto::ProtoString* nf = JSSymbols::nativeFn(ctx);
    const proto::ProtoObject* target = nf ? v->getAttribute(ctx, nf, false) : nullptr;
    return target && target != PROTO_NONE && target->isMethod(ctx);
}

bool isArray(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return !isJSString(v) && getArrayElements(ctx, v) != nullptr;
}

// Node's invalidArgTypeHelper(input): the " Received ..." tail of an
// ERR_INVALID_ARG_TYPE message.
std::string received(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (isUndefined(ctx, v)) return " Received undefined";
    if (isNull(v)) return " Received null";
    std::string type;
    std::string inspected;
    if (isJSString(v)) {
        type = "string";
        inspected = "'" + utf8Of(ctx, v) + "'";
    } else if (v == PROTO_TRUE || v == PROTO_FALSE) {
        type = "boolean";
        inspected = v == PROTO_TRUE ? "true" : "false";
    } else if (isNumber(ctx, v)) {
        type = "number";
        inspected = numberText(ctx, v);
    } else if (isFunction(ctx, v)) {
        return " Received function";
    } else {
        return isArray(ctx, v) ? " Received an instance of Array"
                               : " Received an instance of Object";
    }
    if (inspected.size() > 28) inspected = inspected.substr(0, 25) + "...";
    return " Received type " + type + " (" + inspected + ")";
}

// Throw Node's ERR_INVALID_ARG_TYPE TypeError. The caller returns PROTO_NONE.
const proto::ProtoObject* throwInvalidArgType(proto::ProtoContext* ctx, const std::string& name,
                                              const char* expected,
                                              const proto::ProtoObject* value) {
    const std::string message = "The \"" + name + "\" argument must be of type " + expected + "." +
                                received(ctx, value);
    const proto::ProtoObject* err = makeNativeError(ctx, "TypeError", message.c_str());
    if (err) {
        const proto::ProtoObject* codeName = ctx->fromUTF8String("code");
        const proto::ProtoString* codeKey = codeName ? codeName->asString(ctx) : nullptr;
        if (codeKey) err = err->setAttribute(ctx, codeKey, ctx->fromUTF8String("ERR_INVALID_ARG_TYPE"));
    }
    signalNativeException(err);
    return PROTO_NONE;
}

// validateString(value, name): true with the UTF-8 value, or false after
// throwing.
bool requireString(proto::ProtoContext* ctx, const proto::ProtoObject* v, const std::string& name,
                   std::string& out) {
    if (!isJSString(v)) {
        throwInvalidArgType(ctx, name, "string", v);
        return false;
    }
    out = utf8Of(ctx, v);
    return true;
}

const proto::ProtoObject* str(proto::ProtoContext* ctx, const std::string& s) {
    return ctx->fromUTF8String(s.c_str());
}

const proto::ProtoString* key(proto::ProtoContext* ctx, const char* name) {
    const proto::ProtoObject* o = ctx->fromUTF8String(name);
    return o ? o->asString(ctx) : nullptr;
}

pathalg::ResolveEnv resolveEnv() {
    pathalg::ResolveEnv env;
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
#if defined(_WIN32)
        // The native form, with backslashes, as process.cwd() returns it.
        const std::u8string u8 = cwd.u8string();
        env.cwd.assign(u8.begin(), u8.end());
#else
        env.cwd = cwd.generic_string();
#endif
    }
    env.hostIsWindows = kHostIsWindows;
#if defined(_WIN32)
    // process.env[`=${device}`]: Windows keeps each drive's current directory
    // in a hidden variable named "=C:".
    env.driveCwd = [](const std::string& device) {
        const char* v = std::getenv(("=" + device).c_str());
        return v ? std::string(v) : std::string();
    };
#endif
    return env;
}

// --- Bindings, one per function and flavour -----------------------------------

enum class Flavor { Win32, Posix };

template <Flavor F>
const proto::ProtoObject* pathResolve(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                      const proto::ParentLink*, const proto::ProtoList* args,
                                      const proto::ProtoSparseList*) {
    std::vector<std::string> parts;
    const std::size_t n = argCount(ctx, args);
    for (std::size_t i = 0; i < n; ++i) {
        std::string s;
        if (!requireString(ctx, argAt(ctx, args, i), "paths[" + std::to_string(i) + "]", s))
            return PROTO_NONE;
        parts.push_back(s);
    }
    const pathalg::ResolveEnv env = resolveEnv();
    return str(ctx, F == Flavor::Win32 ? pathalg::win32::resolve(parts, env)
                                       : pathalg::posix::resolve(parts, env));
}

template <Flavor F>
const proto::ProtoObject* pathJoin(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                   const proto::ParentLink*, const proto::ProtoList* args,
                                   const proto::ProtoSparseList*) {
    std::vector<std::string> parts;
    const std::size_t n = argCount(ctx, args);
    for (std::size_t i = 0; i < n; ++i) {
        std::string s;
        if (!requireString(ctx, argAt(ctx, args, i), "path", s)) return PROTO_NONE;
        parts.push_back(s);
    }
    return str(ctx, F == Flavor::Win32 ? pathalg::win32::join(parts) : pathalg::posix::join(parts));
}

template <Flavor F>
const proto::ProtoObject* pathNormalize(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                        const proto::ParentLink*, const proto::ProtoList* args,
                                        const proto::ProtoSparseList*) {
    std::string p;
    if (!requireString(ctx, argAt(ctx, args, 0), "path", p)) return PROTO_NONE;
    return str(ctx, F == Flavor::Win32 ? pathalg::win32::normalize(p) : pathalg::posix::normalize(p));
}

template <Flavor F>
const proto::ProtoObject* pathIsAbsolute(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                         const proto::ParentLink*, const proto::ProtoList* args,
                                         const proto::ProtoSparseList*) {
    std::string p;
    if (!requireString(ctx, argAt(ctx, args, 0), "path", p)) return PROTO_NONE;
    const bool r = F == Flavor::Win32 ? pathalg::win32::isAbsolute(p) : pathalg::posix::isAbsolute(p);
    return r ? PROTO_TRUE : PROTO_FALSE;
}

template <Flavor F>
const proto::ProtoObject* pathRelative(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                       const proto::ParentLink*, const proto::ProtoList* args,
                                       const proto::ProtoSparseList*) {
    std::string from, to;
    if (!requireString(ctx, argAt(ctx, args, 0), "from", from)) return PROTO_NONE;
    if (!requireString(ctx, argAt(ctx, args, 1), "to", to)) return PROTO_NONE;
    const pathalg::ResolveEnv env = resolveEnv();
    return str(ctx, F == Flavor::Win32 ? pathalg::win32::relative(from, to, env)
                                       : pathalg::posix::relative(from, to, env));
}

template <Flavor F>
const proto::ProtoObject* pathToNamespacedPath(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                               const proto::ParentLink*, const proto::ProtoList* args,
                                               const proto::ProtoSparseList*) {
    const proto::ProtoObject* a = argAt(ctx, args, 0);
    // Anything but a non-empty string is returned as it is, as in Node.
    if (F == Flavor::Posix || !isJSString(a)) return a;
    const std::string p = utf8Of(ctx, a);
    if (p.empty()) return a;
    return str(ctx, pathalg::win32::toNamespacedPath(p, resolveEnv()));
}

template <Flavor F>
const proto::ProtoObject* pathDirname(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                      const proto::ParentLink*, const proto::ProtoList* args,
                                      const proto::ProtoSparseList*) {
    std::string p;
    if (!requireString(ctx, argAt(ctx, args, 0), "path", p)) return PROTO_NONE;
    return str(ctx, F == Flavor::Win32 ? pathalg::win32::dirname(p) : pathalg::posix::dirname(p));
}

template <Flavor F>
const proto::ProtoObject* pathBasename(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                       const proto::ParentLink*, const proto::ProtoList* args,
                                       const proto::ProtoSparseList*) {
    const proto::ProtoObject* suffixArg = argAt(ctx, args, 1);
    const bool hasSuffix = !isUndefined(ctx, suffixArg);
    std::string suffix;
    if (hasSuffix && !requireString(ctx, suffixArg, "suffix", suffix)) return PROTO_NONE;
    std::string p;
    if (!requireString(ctx, argAt(ctx, args, 0), "path", p)) return PROTO_NONE;
    return str(ctx, F == Flavor::Win32 ? pathalg::win32::basename(p, hasSuffix, suffix)
                                       : pathalg::posix::basename(p, hasSuffix, suffix));
}

template <Flavor F>
const proto::ProtoObject* pathExtname(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                      const proto::ParentLink*, const proto::ProtoList* args,
                                      const proto::ProtoSparseList*) {
    std::string p;
    if (!requireString(ctx, argAt(ctx, args, 0), "path", p)) return PROTO_NONE;
    return str(ctx, F == Flavor::Win32 ? pathalg::win32::extname(p) : pathalg::posix::extname(p));
}

template <Flavor F>
const proto::ProtoObject* pathParse(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                    const proto::ParentLink*, const proto::ProtoList* args,
                                    const proto::ProtoSparseList*) {
    std::string p;
    if (!requireString(ctx, argAt(ctx, args, 0), "path", p)) return PROTO_NONE;
    const pathalg::ParsedPath r = F == Flavor::Win32 ? pathalg::win32::parse(p) : pathalg::posix::parse(p);
    const proto::ProtoObject* obj = ctx->newObject(true);
    // Node's insertion order: root, dir, base, ext, name.
    obj = obj->setAttribute(ctx, key(ctx, "root"), str(ctx, r.root));
    obj = obj->setAttribute(ctx, key(ctx, "dir"), str(ctx, r.dir));
    obj = obj->setAttribute(ctx, key(ctx, "base"), str(ctx, r.base));
    obj = obj->setAttribute(ctx, key(ctx, "ext"), str(ctx, r.ext));
    obj = obj->setAttribute(ctx, key(ctx, "name"), str(ctx, r.name));
    return obj;
}

// A field of format()'s argument as the template literal in Node's _format
// would print it, or "" for a falsy value.
std::string formatField(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const char* name) {
    const proto::ProtoString* k = key(ctx, name);
    const proto::ProtoObject* v = k ? obj->getAttribute(ctx, k, true) : nullptr;
    if (isUndefined(ctx, v) || isNull(v) || v == PROTO_FALSE) return std::string();
    if (isJSString(v)) return utf8Of(ctx, v);
    if (v == PROTO_TRUE) return "true";
    if (isNumber(ctx, v)) {
        if (v->isInteger(ctx) && v->asLong(ctx) == 0) return std::string();
        if (!v->isInteger(ctx)) {
            const double d = v->asDouble(ctx);
            if (d == 0 || std::isnan(d)) return std::string();
        }
        return numberText(ctx, v);
    }
    return "[object Object]";
}

template <Flavor F>
const proto::ProtoObject* pathFormat(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                     const proto::ParentLink*, const proto::ProtoList* args,
                                     const proto::ProtoSparseList*) {
    const proto::ProtoObject* o = argAt(ctx, args, 0);
    // validateObject(pathObject, 'pathObject'): an object that is neither
    // null, an array nor a function.
    if (isUndefined(ctx, o) || isNull(o) || isJSString(o) || o == PROTO_TRUE || o == PROTO_FALSE ||
        isNumber(ctx, o) || isArray(ctx, o) || isFunction(ctx, o)) {
        return throwInvalidArgType(ctx, "pathObject", "object", o);
    }
    pathalg::FormatInput in;
    in.dir = formatField(ctx, o, "dir");
    in.root = formatField(ctx, o, "root");
    in.base = formatField(ctx, o, "base");
    in.name = formatField(ctx, o, "name");
    in.ext = formatField(ctx, o, "ext");
    return str(ctx, F == Flavor::Win32 ? pathalg::win32::format(in) : pathalg::posix::format(in));
}

template <Flavor F>
const proto::ProtoObject* buildFlavor(proto::ProtoContext* ctx) {
    static const NativeEntry entries[] = {
        {"resolve",          pathResolve<F>},
        {"normalize",        pathNormalize<F>},
        {"isAbsolute",       pathIsAbsolute<F>},
        {"join",             pathJoin<F>},
        {"relative",         pathRelative<F>},
        {"toNamespacedPath", pathToNamespacedPath<F>},
        {"dirname",          pathDirname<F>},
        {"basename",         pathBasename<F>},
        {"extname",          pathExtname<F>},
        {"format",           pathFormat<F>},
        {"parse",            pathParse<F>},
        {"_makeLong",        pathToNamespacedPath<F>},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, sizeof(entries) / sizeof(entries[0]) - 1);
    if (!mod) return nullptr;
    mod = mod->setAttribute(ctx, key(ctx, "sep"), str(ctx, F == Flavor::Win32 ? "\\" : "/"));
    mod = mod->setAttribute(ctx, key(ctx, "delimiter"), str(ctx, F == Flavor::Win32 ? ";" : ":"));
    return mod;
}

}  // namespace

const proto::ProtoObject* PathModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    const proto::ProtoObject* win32 = buildFlavor<Flavor::Win32>(ctx);
    const proto::ProtoObject* posix = buildFlavor<Flavor::Posix>(ctx);
    if (!win32 || !posix) return globalObj;
    // Both objects are mutable, so each update is in place and the
    // cross-references below see the final objects.
    win32 = win32->setAttribute(ctx, key(ctx, "win32"), win32);
    win32 = win32->setAttribute(ctx, key(ctx, "posix"), posix);
    posix = posix->setAttribute(ctx, key(ctx, "win32"), win32);
    posix = posix->setAttribute(ctx, key(ctx, "posix"), posix);
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "path",
                                               kHostIsWindows ? win32 : posix);
}

} // namespace protojs
