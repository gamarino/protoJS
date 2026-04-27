#include "FSModule.h"
#include "../IOModule.h"
#include "../../ProtoNativeModule.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include "../../JSContext.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../ProtoDeferred.h"
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace protojs {

namespace fs = std::filesystem;

namespace {

// ---- Argument helpers --------------------------------------------------

bool argString(proto::ProtoContext* ctx, const proto::ProtoList* args,
                int idx, std::string& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isString(ctx)) return false;
    a->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

const proto::ProtoObject* argAt(proto::ProtoContext* ctx,
                                 const proto::ProtoList* args, int idx) {
    if (!ctx || !args) return nullptr;
    if (idx >= static_cast<int>(args->getSize(ctx))) return nullptr;
    return args->getAt(ctx, idx);
}

// Build a `Stats`-like object matching Node's minimal surface.
const proto::ProtoObject* buildStatsObject(proto::ProtoContext* ctx,
                                            const struct stat& st) {
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    auto setI = [&](const char* k, long long v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk, ctx->fromInteger(v));
    };
    auto setB = [&](const char* k, bool v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk, v ? PROTO_TRUE : PROTO_FALSE);
    };
    setI("size", static_cast<long long>(st.st_size));
    setB("isFile", S_ISREG(st.st_mode));
    setB("isDirectory", S_ISDIR(st.st_mode));
    setI("mtime", static_cast<long long>(st.st_mtime) * 1000);
    return obj;
}

// Generic async wrapper: run `work` on the IOThreadPool, then resolve /
// reject the returned ProtoDeferred from the EventLoop.  Both deferred
// and any captured ProtoObject values pass through the wrapper's
// protoCore root set across the thread-pool / event-loop hop.
template <class F>
const proto::ProtoObject* runAsync(proto::ProtoContext* ctx, F&& work) {
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;
    const proto::ProtoObject* deferred = ProtoDeferred::createPending(ctx);
    if (!deferred) return PROTO_NONE;
    proto::ProtoRootSet* rs = wrapper->getRootSet();
    proto::ProtoRootSet::Handle pin = rs ? rs->add(deferred)
                                          : proto::ProtoRootSet::kNullHandle;
    IOThreadPool::getInstance().getExecutor().submit(
        [wrapper, pin, work = std::forward<F>(work)]() mutable {
        std::string err;
        std::function<const proto::ProtoObject*(proto::ProtoContext*)> resolveFn;
        try {
            resolveFn = work();
        } catch (const std::exception& e) {
            err = e.what();
        }
        EventLoop::getInstance().enqueueCallback(
            [wrapper, pin, err = std::move(err),
             resolveFn = std::move(resolveFn)]() {
            if (!wrapper) return;
            JSContextWrapper::CurrentScope ws(wrapper);
            proto::ProtoContext* c = wrapper->getProtoContext();
            if (!c) return;
            proto::ProtoRootSet* rs = wrapper->getRootSet();
            const proto::ProtoObject* d = rs ? rs->resolve(pin) : nullptr;
            if (rs) rs->remove(pin);
            if (!d) return;
            if (!err.empty()) {
                ProtoDeferred::rejectFromAsync(c, d,
                    c->fromUTF8String(err.c_str()), wrapper);
            } else {
                const proto::ProtoObject* v = resolveFn ? resolveFn(c) : PROTO_NONE;
                ProtoDeferred::resolveFromAsync(c, d, v, wrapper);
            }
        });
    });
    return deferred;
}

// ---- fs.promises.* -----------------------------------------------------

const proto::ProtoObject* promisesReadFile(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    return runAsync(ctx, [path]() {
        std::string content = IOModule::readFileSync(path);
        return [content = std::move(content)](proto::ProtoContext* c)
            -> const proto::ProtoObject* {
            return c->fromUTF8String(content.c_str());
        };
    });
}

const proto::ProtoObject* promisesWriteFile(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path, data;
    if (!argString(ctx, args, 0, path) ||
        !argString(ctx, args, 1, data)) return PROTO_NONE;
    return runAsync(ctx, [path, data]() {
        IOModule::writeFileSync(path, data);
        return [](proto::ProtoContext*) -> const proto::ProtoObject* {
            return PROTO_NONE;  // resolves as undefined per Node
        };
    });
}

const proto::ProtoObject* promisesReaddir(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    return runAsync(ctx, [path]() {
        std::vector<std::string> entries;
        for (const auto& e : fs::directory_iterator(path)) {
            entries.push_back(e.path().filename().string());
        }
        return [entries = std::move(entries)](proto::ProtoContext* c)
            -> const proto::ProtoObject* {
            const proto::ProtoObject* arr = createNewArray(c, nullptr);
            const proto::ProtoList* els = c->newList();
            for (const auto& s : entries) {
                els = els->appendLast(c, c->fromUTF8String(s.c_str()));
            }
            setArrayElements(c, arr, els);
            return arr;
        };
    });
}

const proto::ProtoObject* promisesMkdir(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    bool recursive = false;
    const proto::ProtoObject* opts = argAt(ctx, args, 1);
    if (opts && !opts->isNone(ctx)) {
        const proto::ProtoString* rk = ctx->fromUTF8String("recursive")->asString(ctx);
        if (rk) {
            const proto::ProtoObject* rv = opts->getAttribute(ctx, rk, false);
            if (rv == PROTO_TRUE) recursive = true;
        }
    }
    return runAsync(ctx, [path, recursive]() {
        if (recursive) fs::create_directories(path);
        else           fs::create_directory(path);
        return [](proto::ProtoContext*) -> const proto::ProtoObject* {
            return PROTO_NONE;
        };
    });
}

const proto::ProtoObject* promisesStat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    return runAsync(ctx, [path]() {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            throw std::runtime_error("Cannot stat file: " + path);
        }
        return [st](proto::ProtoContext* c) -> const proto::ProtoObject* {
            return buildStatsObject(c, st);
        };
    });
}

// ---- Sync API ----------------------------------------------------------

const proto::ProtoObject* readFileSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    try {
        return ctx->fromUTF8String(IOModule::readFileSync(path).c_str());
    } catch (...) {
        return PROTO_NONE;
    }
}

const proto::ProtoObject* writeFileSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path, data;
    if (!argString(ctx, args, 0, path) ||
        !argString(ctx, args, 1, data)) return PROTO_FALSE;
    try {
        IOModule::writeFileSync(path, data);
    } catch (...) {
        return PROTO_FALSE;
    }
    return PROTO_NONE;
}

const proto::ProtoObject* readdirSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    const proto::ProtoList* els = ctx->newList();
    try {
        for (const auto& e : fs::directory_iterator(path)) {
            els = els->appendLast(ctx,
                ctx->fromUTF8String(e.path().filename().string().c_str()));
        }
    } catch (...) {
        return PROTO_NONE;
    }
    setArrayElements(ctx, arr, els);
    return arr;
}

const proto::ProtoObject* mkdirSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_FALSE;
    bool recursive = false;
    const proto::ProtoObject* opts = argAt(ctx, args, 1);
    if (opts && !opts->isNone(ctx)) {
        const proto::ProtoString* rk = ctx->fromUTF8String("recursive")->asString(ctx);
        if (rk) {
            const proto::ProtoObject* rv = opts->getAttribute(ctx, rk, false);
            if (rv == PROTO_TRUE) recursive = true;
        }
    }
    try {
        if (recursive) fs::create_directories(path);
        else           fs::create_directory(path);
    } catch (...) {
        return PROTO_FALSE;
    }
    return PROTO_NONE;
}

const proto::ProtoObject* statSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return PROTO_NONE;
    return buildStatsObject(ctx, st);
}

const proto::ProtoObject* unlinkSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_FALSE;
    if (unlink(path.c_str()) != 0) return PROTO_FALSE;
    return PROTO_NONE;
}

const proto::ProtoObject* rmdirSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_FALSE;
    if (rmdir(path.c_str()) != 0) return PROTO_FALSE;
    return PROTO_NONE;
}

const proto::ProtoObject* renameSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string oldPath, newPath;
    if (!argString(ctx, args, 0, oldPath) ||
        !argString(ctx, args, 1, newPath)) return PROTO_FALSE;
    if (std::rename(oldPath.c_str(), newPath.c_str()) != 0) return PROTO_FALSE;
    return PROTO_NONE;
}

const proto::ProtoObject* copyFileSyncImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string src, dst;
    if (!argString(ctx, args, 0, src) ||
        !argString(ctx, args, 1, dst)) return PROTO_FALSE;
    try {
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    } catch (...) {
        return PROTO_FALSE;
    }
    return PROTO_NONE;
}

// ---- Stream stubs ------------------------------------------------------
//
// fs.createReadStream / createWriteStream return objects with a `_path`
// attribute.  The full Stream contract is owned by `stream` (still
// QuickJS-side); these stubs preserve the same API surface and let
// the migration of `stream` come independently.

const proto::ProtoObject* createReadStreamImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* k = ctx->fromUTF8String("_path")->asString(ctx);
    if (k) obj->setAttribute(ctx, k, ctx->fromUTF8String(path.c_str()));
    return obj;
}

const proto::ProtoObject* createWriteStreamImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return createReadStreamImpl(ctx, self, pl, args, kw);  // same shape
}

}  // namespace

const proto::ProtoObject* FSModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    static const NativeEntry promisesEntries[] = {
        {"readFile",  promisesReadFile},
        {"writeFile", promisesWriteFile},
        {"readdir",   promisesReaddir},
        {"mkdir",     promisesMkdir},
        {"stat",      promisesStat},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* promisesObj =
        ProtoNativeModule::buildModule(ctx, promisesEntries, 5);
    if (!promisesObj) return globalObj;

    static const NativeEntry fsEntries[] = {
        {"readFileSync",     readFileSyncImpl},
        {"writeFileSync",    writeFileSyncImpl},
        {"readdirSync",      readdirSyncImpl},
        {"mkdirSync",        mkdirSyncImpl},
        {"statSync",         statSyncImpl},
        {"unlinkSync",       unlinkSyncImpl},
        {"rmdirSync",        rmdirSyncImpl},
        {"renameSync",       renameSyncImpl},
        {"copyFileSync",     copyFileSyncImpl},
        {"createReadStream", createReadStreamImpl},
        {"createWriteStream",createWriteStreamImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, fsEntries, 11);
    if (!mod) return globalObj;
    const proto::ProtoString* pk =
        ctx->fromUTF8String("promises")->asString(ctx);
    if (pk) mod = mod->setAttribute(ctx, pk, promisesObj);

    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "fs", mod);
}

} // namespace protojs
