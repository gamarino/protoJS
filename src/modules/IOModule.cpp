#include "IOModule.h"
#include "../ProtoNativeModule.h"
#include "../JSContext.h"
#include "../ProtoDeferred.h"
#include <fstream>
#include <sstream>
#include <future>
#include <stdexcept>

namespace protojs {

namespace {

bool argString(proto::ProtoContext* ctx, const proto::ProtoList* args,
                int idx, std::string& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isString(ctx)) return false;
    a->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

const proto::ProtoObject* ioReadFile(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    auto& pool = IOThreadPool::getInstance();
    auto future = pool.getExecutor().submit(
        [path]() { return IOModule::readFileSync(path); });
    try {
        std::string content = future.get();
        return ctx->fromUTF8String(content.c_str());
    } catch (...) {
        return PROTO_NONE;
    }
}

const proto::ProtoObject* ioWriteFile(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path, content;
    if (!argString(ctx, args, 0, path) ||
        !argString(ctx, args, 1, content)) return PROTO_FALSE;
    auto& pool = IOThreadPool::getInstance();
    auto future = pool.getExecutor().submit(
        [path, content]() { return IOModule::writeFileSync(path, content); });
    try {
        return future.get() ? PROTO_TRUE : PROTO_FALSE;
    } catch (...) {
        return PROTO_FALSE;
    }
}

// Async paths now produce a real ProtoDeferred — `io.readFileAsync(p)`
// returns an object supporting `.then(cb)` / `.catch(cb)`.  This
// replaces the old QuickJS-side stubs whose resolve/reject were
// no-op JS_NewCFunctions.
const proto::ProtoObject* ioReadFileAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path;
    if (!argString(ctx, args, 0, path)) return PROTO_NONE;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;

    const proto::ProtoObject* deferred = ProtoDeferred::createPending(ctx);
    if (!deferred) return PROTO_NONE;
    proto::ProtoRootSet* rs = wrapper->getRootSet();
    proto::ProtoRootSet::Handle pin = rs ? rs->add(deferred)
                                          : proto::ProtoRootSet::kNullHandle;

    IOThreadPool::getInstance().getExecutor().submit([path, wrapper, pin]() {
        std::string err;
        std::string content;
        try { content = IOModule::readFileSync(path); }
        catch (const std::exception& e) { err = e.what(); }
        EventLoop::getInstance().enqueueCallback([wrapper, pin, content, err]() {
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
                ProtoDeferred::resolveFromAsync(c, d,
                    c->fromUTF8String(content.c_str()), wrapper);
            }
        });
    });
    return deferred;
}

const proto::ProtoObject* ioWriteFileAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string path, content;
    if (!argString(ctx, args, 0, path) ||
        !argString(ctx, args, 1, content)) return PROTO_NONE;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;

    const proto::ProtoObject* deferred = ProtoDeferred::createPending(ctx);
    if (!deferred) return PROTO_NONE;
    proto::ProtoRootSet* rs = wrapper->getRootSet();
    proto::ProtoRootSet::Handle pin = rs ? rs->add(deferred)
                                          : proto::ProtoRootSet::kNullHandle;

    IOThreadPool::getInstance().getExecutor().submit(
        [path, content, wrapper, pin]() {
        std::string err;
        bool ok = false;
        try { ok = IOModule::writeFileSync(path, content); }
        catch (const std::exception& e) { err = e.what(); }
        EventLoop::getInstance().enqueueCallback([wrapper, pin, ok, err]() {
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
                ProtoDeferred::resolveFromAsync(c, d,
                    ok ? PROTO_TRUE : PROTO_FALSE, wrapper);
            }
        });
    });
    return deferred;
}

}  // namespace

std::string IOModule::readFileSync(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool IOModule::writeFileSync(const std::string& path,
                              const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for writing: " + path);
    }
    file << content;
    file.close();
    return file.good();
}

const proto::ProtoObject* IOModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"readFile",       ioReadFile},
        {"writeFile",      ioWriteFile},
        {"readFileAsync",  ioReadFileAsync},
        {"writeFileAsync", ioWriteFileAsync},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 4);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "io", mod);
}

} // namespace protojs
