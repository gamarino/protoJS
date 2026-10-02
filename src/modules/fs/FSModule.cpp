#include "FSModule.h"
#include "../IOModule.h"
#include "../../ProtoNativeModule.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include "../../JSContext.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../ProtoDeferred.h"
#include "../../runtime/ProtoInterpreter.h"
#include <atomic>
#include <cerrno>
#include <functional>
#include <memory>
#include <system_error>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../platform/Posix.h"
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
                                            const platform::StatBuf& st) {
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
        platform::StatBuf st;
        if (platform::statPath(path.c_str(), &st) != 0) {
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
    platform::StatBuf st;
    if (platform::statPath(path.c_str(), &st) != 0) return PROTO_NONE;
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
    if (platform::unlinkPath(path.c_str()) != 0) return PROTO_FALSE;
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
    if (platform::rmdirPath(path.c_str()) != 0) return PROTO_FALSE;
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

// ---- Callback API ------------------------------------------------------
//
// fs.readFile(path[, options], callback) and its siblings, as in Node: the
// operation runs on the IOThreadPool and the callback is invoked on the event
// loop -- never before the call returns -- with (err) on failure and
// (null, result) on success. err is an Error carrying Node's code, errno,
// syscall and path, with Node's message ("ENOENT: no such file or directory,
// open '/x'"). Invalid arguments throw synchronously, as in Node.

// Operations whose callback has not run yet; main.cpp's drain loop waits for
// them, so a pending callback keeps the process alive as in Node.
std::atomic<int> g_activeCallbacks{0};

// What an operation produced on the I/O thread: plain C++ values only, since
// no ProtoObject may be created off the JavaScript thread.
struct FsResult {
    int err = 0;                 // errno; 0 on success
    std::string syscall;
    std::string path;
    enum class Kind { None, Text, Names, Stat, OptionalText } kind = Kind::None;
    std::string text;
    bool hasText = false;        // Kind::OptionalText: undefined when false
    std::vector<std::string> names;
    platform::StatBuf st{};
};

FsResult fsFailure(int err, const char* syscall, const std::string& path) {
    FsResult r;
    r.err = err ? err : EIO;
    r.syscall = syscall;
    r.path = path;
    return r;
}

// errno of a std::filesystem error, in the generic (POSIX) category on every
// platform: on Windows the error_code carries a Win32 code, which
// default_error_condition maps to its errno equivalent.
int errnoOf(const std::error_code& ec) {
    const std::error_condition cond = ec.default_error_condition();
    if (cond.category() == std::generic_category()) return cond.value();
    return EIO;
}

// libuv's names and descriptions, which Node puts in err.code and err.message.
struct ErrnoName { int err; const char* code; const char* description; };
const ErrnoName kErrnoNames[] = {
    {ENOENT,       "ENOENT",       "no such file or directory"},
    {EACCES,       "EACCES",       "permission denied"},
    {EEXIST,       "EEXIST",       "file already exists"},
    {ENOTDIR,      "ENOTDIR",      "not a directory"},
    {EISDIR,       "EISDIR",       "illegal operation on a directory"},
    {ENOTEMPTY,    "ENOTEMPTY",    "directory not empty"},
    {EPERM,        "EPERM",        "operation not permitted"},
    {EBADF,        "EBADF",        "bad file descriptor"},
    {EMFILE,       "EMFILE",       "too many open files"},
    {EINVAL,       "EINVAL",       "invalid argument"},
    {EIO,          "EIO",          "i/o error"},
    {ENOSPC,       "ENOSPC",       "no space left on device"},
    {EROFS,        "EROFS",        "read-only file system"},
    {ELOOP,        "ELOOP",        "too many symbolic links encountered"},
    {ENAMETOOLONG, "ENAMETOOLONG", "name too long"},
    {EBUSY,        "EBUSY",        "resource busy or locked"},
};

void setProp(proto::ProtoContext* ctx, const proto::ProtoObject*& obj,
             const char* name, const proto::ProtoObject* value) {
    const proto::ProtoObject* n = ctx->fromUTF8String(name);
    const proto::ProtoString* k = n ? n->asString(ctx) : nullptr;
    if (k && obj) obj = obj->setAttribute(ctx, k, value);
}

// The Error Node passes to the callback.  errno is the negated errno, which is
// what Node reports on Linux and macOS; Windows' libuv numbers differ.  It is
// built on the event loop, outside any runBytecode frame, so the global that
// supplies Error.prototype is passed explicitly.
const proto::ProtoObject* makeFsError(proto::ProtoContext* ctx, const FsResult& r,
                                      const proto::ProtoObject* const* globalRoot) {
    const char* code = "UNKNOWN";
    const char* description = "unknown error";
    for (const ErrnoName& e : kErrnoNames) {
        if (e.err == r.err) { code = e.code; description = e.description; break; }
    }
    // Node names the path in the message and in err.path when the operation
    // has one (not for EISDIR from read, which works on a descriptor).
    std::string message = std::string(code) + ": " + description + ", " + r.syscall;
    if (!r.path.empty()) message += " '" + r.path + "'";
    const proto::ProtoObject* err =
        makeNativeError(ctx, "Error", message.c_str(), globalRoot);
    if (!err) return PROTO_NONE;
    setProp(ctx, err, "errno", ctx->fromInteger(-static_cast<long long>(r.err)));
    setProp(ctx, err, "code", ctx->fromUTF8String(code));
    setProp(ctx, err, "syscall", ctx->fromUTF8String(r.syscall.c_str()));
    if (!r.path.empty())
        setProp(ctx, err, "path", ctx->fromUTF8String(r.path.c_str()));
    return err;
}

// A TypeError with Node's code for a bad argument, thrown synchronously.
const proto::ProtoObject* throwInvalidArg(proto::ProtoContext* ctx,
                                          const std::string& message) {
    const proto::ProtoObject* err = makeNativeError(ctx, "TypeError", message.c_str());
    if (err) setProp(ctx, err, "code", ctx->fromUTF8String("ERR_INVALID_ARG_TYPE"));
    signalNativeException(err);
    return PROTO_NONE;
}

// Node's "Received ..." suffix, for the values a caller is likely to pass.
std::string describeReceived(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) return "Received undefined";
    if (v == getNullSentinel()) return "Received null";
    if (v->isBoolean(ctx)) return std::string("Received type boolean (") +
                                  (v->asBoolean(ctx) ? "true" : "false") + ")";
    if (v->isInteger(ctx)) return "Received type number (" +
                                  std::to_string(v->asLong(ctx)) + ")";
    if (v->isDouble(ctx)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", v->asDouble(ctx));
        return std::string("Received type number (") + buf + ")";
    }
    if (isCallableValue(ctx, v)) return "Received function";
    return "Received an instance of Object";
}

// UTF-8 strings (protoJS's) to and from std::filesystem paths, which on
// Windows are UTF-16.
fs::path pathFromUtf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}
std::string utf8FromPath(const fs::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

// The trailing callback of an fs call, or a thrown ERR_INVALID_ARG_TYPE.
const proto::ProtoObject* callbackArg(proto::ProtoContext* ctx,
                                      const proto::ProtoList* args) {
    const int n = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* cb = n > 0 ? args->getAt(ctx, n - 1) : nullptr;
    if (!isCallableValue(ctx, cb)) {
        throwInvalidArg(ctx, "The \"cb\" argument must be of type function. " +
                             describeReceived(ctx, cb));
        return nullptr;
    }
    return cb;
}

// The path argument (a string), or a thrown ERR_INVALID_ARG_TYPE.  Buffer and
// URL paths are not supported.
bool pathArg(proto::ProtoContext* ctx, const proto::ProtoList* args,
             const char* name, int idx, std::string& out) {
    if (argString(ctx, args, idx, out)) return true;
    throwInvalidArg(ctx, std::string("The \"") + name +
                         "\" argument must be of type string or an instance of "
                         "Buffer or URL. " +
                         describeReceived(ctx, argAt(ctx, args, idx)));
    return false;
}

// options.<name> when the options argument (args[idx], not the callback) is
// an object; nullptr otherwise.
const proto::ProtoObject* optionAt(proto::ProtoContext* ctx,
                                   const proto::ProtoList* args, int idx,
                                   const char* name) {
    const int n = args ? static_cast<int>(args->getSize(ctx)) : 0;
    if (idx >= n - 1) return nullptr;  // the last argument is the callback
    const proto::ProtoObject* opts = args->getAt(ctx, idx);
    if (!opts || opts == PROTO_NONE || opts->isString(ctx) || opts->isInteger(ctx) ||
        opts == getNullSentinel() || opts == getUndefinedSentinel())
        return nullptr;
    const proto::ProtoObject* k = ctx->fromUTF8String(name);
    const proto::ProtoString* key = k ? k->asString(ctx) : nullptr;
    const proto::ProtoObject* v = key ? opts->getAttribute(ctx, key, true) : nullptr;
    return (v && v != PROTO_NONE) ? v : nullptr;
}

// Run `work` on the I/O pool and call `cb` with its outcome on the event loop.
// The callback is pinned in the wrapper's root set until it has run.
template <class Work>
const proto::ProtoObject* runWithCallback(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* cb,
                                          Work work) {
    JSContextWrapper* wrapper = JSContextWrapper::current();
    proto::ProtoRootSet* rs = wrapper ? wrapper->getRootSet() : nullptr;
    if (!ctx || !wrapper || !rs) return PROTO_NONE;
    const proto::ProtoRootSet::Handle pin = rs->add(cb);
    g_activeCallbacks.fetch_add(1);
    IOThreadPool::getInstance().getExecutor().submit(
        [wrapper, pin, work = std::move(work)]() mutable {
        auto result = std::make_shared<FsResult>();
        try {
            *result = work();
        } catch (const std::exception&) {
            *result = fsFailure(EIO, "unknown", "");
        }
        EventLoop::getInstance().enqueueCallback([wrapper, pin, result]() {
            JSContextWrapper::CurrentScope ws(wrapper);
            proto::ProtoContext* c = wrapper->getProtoContext();
            proto::ProtoRootSet* rs = wrapper->getRootSet();
            const proto::ProtoObject* cb = (c && rs) ? rs->resolve(pin) : nullptr;
            if (cb && cb != PROTO_NONE) {
                const proto::ProtoList* cbArgs = c->newList();
                if (result->err != 0) {
                    cbArgs = cbArgs->appendLast(c, makeFsError(c, *result,
                        wrapper->getNativeGlobalRootPtr()));
                } else {
                    cbArgs = cbArgs->appendLast(c, getNullSentinel());
                    switch (result->kind) {
                    case FsResult::Kind::Text:
                        cbArgs = cbArgs->appendLast(c,
                            c->fromUTF8String(result->text.c_str()));
                        break;
                    case FsResult::Kind::OptionalText:
                        cbArgs = cbArgs->appendLast(c, result->hasText
                            ? c->fromUTF8String(result->text.c_str())
                            : getUndefinedSentinel());
                        break;
                    case FsResult::Kind::Names: {
                        const proto::ProtoObject* arr = createNewArray(c, nullptr);
                        const proto::ProtoList* els = c->newList();
                        for (const std::string& name : result->names)
                            els = els->appendLast(c, c->fromUTF8String(name.c_str()));
                        setArrayElements(c, arr, els);
                        cbArgs = cbArgs->appendLast(c, arr);
                        break;
                    }
                    case FsResult::Kind::Stat:
                        cbArgs = cbArgs->appendLast(c, buildStatsObject(c, result->st));
                        break;
                    case FsResult::Kind::None:
                        break;
                    }
                }
                const ProtoBytecodeModule* mod =
                    static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
                callJSFunctionFromAsync(c, cb, PROTO_NONE, cbArgs, mod,
                                        wrapper->getNativeGlobalRootPtr());
                drainCallbackException(c, "fs callback");
            }
            if (rs) rs->remove(pin);
            g_activeCallbacks.fetch_sub(1);
        });
    });
    return PROTO_NONE;
}

FsResult readTextFile(const std::string& path) {
    // A directory is EISDIR on every platform, as Node reports it (fopen
    // succeeds on a directory on Linux and fails with EACCES on Windows).
    platform::StatBuf st;
    if (platform::statPath(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
        return fsFailure(EISDIR, "read", "");
    std::FILE* f = platform::fopenPath(path.c_str(), "rb");
    if (!f) return fsFailure(errno, "open", path);
    FsResult r;
    r.kind = FsResult::Kind::Text;
    char buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) r.text.append(buf, got);
    const bool failed = std::ferror(f) != 0;
    const int err = errno;
    std::fclose(f);
    if (failed) return fsFailure(err, "read", "");
    return r;
}

FsResult writeTextFile(const std::string& path, const std::string& data, bool append) {
    std::FILE* f = platform::fopenPath(path.c_str(), append ? "ab" : "wb");
    if (!f) return fsFailure(errno, "open", path);
    const size_t put = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), f);
    int err = put == data.size() ? 0 : errno;
    if (std::fclose(f) != 0 && err == 0) err = errno;
    if (err != 0) return fsFailure(err, "write", path);
    return FsResult{};
}

// The data argument of writeFile / appendFile: a string.  Buffers and typed
// arrays are not supported yet.
bool dataArg(proto::ProtoContext* ctx, const proto::ProtoList* args,
             std::string& out) {
    if (argString(ctx, args, 1, out)) return true;
    throwInvalidArg(ctx, "The \"data\" argument must be of type string "
                         "(protoJS supports string data only). " +
                         describeReceived(ctx, argAt(ctx, args, 1)));
    return false;
}

bool appendFlag(proto::ProtoContext* ctx, const proto::ProtoList* args) {
    const proto::ProtoObject* flag = optionAt(ctx, args, 2, "flag");
    if (!flag || !flag->isString(ctx)) return false;
    std::string f;
    flag->asString(ctx)->toUTF8String(ctx, f);
    return !f.empty() && f[0] == 'a';
}

const proto::ProtoObject* readFileCallback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string path;
    if (!pathArg(ctx, args, "path", 0, path)) return PROTO_NONE;
    const proto::ProtoObject* cb = callbackArg(ctx, args);
    if (!cb) return PROTO_NONE;
    return runWithCallback(ctx, cb, [path]() { return readTextFile(path); });
}

const proto::ProtoObject* writeFileCallback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string path, data;
    if (!pathArg(ctx, args, "file", 0, path) || !dataArg(ctx, args, data))
        return PROTO_NONE;
    const proto::ProtoObject* cb = callbackArg(ctx, args);
    if (!cb) return PROTO_NONE;
    const bool append = appendFlag(ctx, args);
    return runWithCallback(ctx, cb, [path, data, append]() {
        return writeTextFile(path, data, append);
    });
}

const proto::ProtoObject* appendFileCallback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string path, data;
    if (!pathArg(ctx, args, "path", 0, path) || !dataArg(ctx, args, data))
        return PROTO_NONE;
    const proto::ProtoObject* cb = callbackArg(ctx, args);
    if (!cb) return PROTO_NONE;
    return runWithCallback(ctx, cb, [path, data]() {
        return writeTextFile(path, data, /*append=*/true);
    });
}

const proto::ProtoObject* statCallback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string path;
    if (!pathArg(ctx, args, "path", 0, path)) return PROTO_NONE;
    const proto::ProtoObject* cb = callbackArg(ctx, args);
    if (!cb) return PROTO_NONE;
    return runWithCallback(ctx, cb, [path]() {
        FsResult r;
        if (platform::statPath(path.c_str(), &r.st) != 0)
            return fsFailure(errno, "stat", path);
        r.kind = FsResult::Kind::Stat;
        return r;
    });
}

const proto::ProtoObject* readdirCallback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string path;
    if (!pathArg(ctx, args, "path", 0, path)) return PROTO_NONE;
    const proto::ProtoObject* cb = callbackArg(ctx, args);
    if (!cb) return PROTO_NONE;
    return runWithCallback(ctx, cb, [path]() {
        FsResult r;
        r.kind = FsResult::Kind::Names;
        std::error_code ec;
        fs::directory_iterator it(pathFromUtf8(path), ec);
        for (; !ec && it != fs::directory_iterator(); it.increment(ec))
            r.names.push_back(utf8FromPath(it->path().filename()));
        if (ec) return fsFailure(errnoOf(ec), "scandir", path);
        return r;
    });
}

const proto::ProtoObject* unlinkCallback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string path;
    if (!pathArg(ctx, args, "path", 0, path)) return PROTO_NONE;
    const proto::ProtoObject* cb = callbackArg(ctx, args);
    if (!cb) return PROTO_NONE;
    return runWithCallback(ctx, cb, [path]() {
        if (platform::unlinkPath(path.c_str()) != 0)
            return fsFailure(errno, "unlink", path);
        return FsResult{};
    });
}

const proto::ProtoObject* rmdirCallback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string path;
    if (!pathArg(ctx, args, "path", 0, path)) return PROTO_NONE;
    const proto::ProtoObject* cb = callbackArg(ctx, args);
    if (!cb) return PROTO_NONE;
    return runWithCallback(ctx, cb, [path]() {
        if (platform::rmdirPath(path.c_str()) != 0)
            return fsFailure(errno, "rmdir", path);
        return FsResult{};
    });
}

// mkdir(path[, options], callback).  With { recursive: true } the callback
// receives the first directory created, or undefined when none was, as in Node.
const proto::ProtoObject* mkdirCallback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string path;
    if (!pathArg(ctx, args, "path", 0, path)) return PROTO_NONE;
    const proto::ProtoObject* cb = callbackArg(ctx, args);
    if (!cb) return PROTO_NONE;
    const bool recursive = optionAt(ctx, args, 1, "recursive") == PROTO_TRUE;
    return runWithCallback(ctx, cb, [path, recursive]() {
        if (!recursive) {
            if (platform::mkdirPath(path.c_str()) != 0)
                return fsFailure(errno, "mkdir", path);
            return FsResult{};
        }
        FsResult r;
        r.kind = FsResult::Kind::OptionalText;
        // The first directory created is the outermost one that is missing.
        const fs::path target = fs::absolute(pathFromUtf8(path));
        std::error_code ec;
        fs::path first;
        for (fs::path p = target; !p.empty(); p = p.parent_path()) {
            if (fs::exists(p, ec)) break;
            first = p;
            if (p == p.parent_path()) break;
        }
        fs::create_directories(target, ec);
        if (ec) return fsFailure(errnoOf(ec), "mkdir", path);
        if (!fs::is_directory(target, ec)) return fsFailure(EEXIST, "mkdir", path);
        if (!first.empty()) {
            r.hasText = true;
            r.text = utf8FromPath(first);
        }
        return r;
    });
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

int FSModule::getActiveCount() { return g_activeCallbacks.load(); }

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
        {"readFile",         readFileCallback},
        {"writeFile",        writeFileCallback},
        {"appendFile",       appendFileCallback},
        {"stat",             statCallback},
        {"readdir",          readdirCallback},
        {"unlink",           unlinkCallback},
        {"rmdir",            rmdirCallback},
        {"mkdir",            mkdirCallback},
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
        ProtoNativeModule::buildModule(ctx, fsEntries, 19);
    if (!mod) return globalObj;
    const proto::ProtoString* pk =
        ctx->fromUTF8String("promises")->asString(ctx);
    if (pk) mod = mod->setAttribute(ctx, pk, promisesObj);

    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "fs", mod);
}

} // namespace protojs
