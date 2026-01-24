#include "IOModule.h"
#include "../Deferred.h"
#include "../JSContext.h"
#include <fstream>
#include <sstream>
#include <future>

namespace protojs {

void IOModule::init(JSContext* ctx) {
    JSValue ioModule = JS_NewObject(ctx);
    
    // Register I/O functions
    JS_SetPropertyStr(ctx, ioModule, "readFile", 
                     JS_NewCFunction(ctx, readFile, "readFile", 1));
    JS_SetPropertyStr(ctx, ioModule, "writeFile", 
                     JS_NewCFunction(ctx, writeFile, "writeFile", 2));
    JS_SetPropertyStr(ctx, ioModule, "readFileAsync", 
                     JS_NewCFunction(ctx, readFileAsync, "readFileAsync", 1));
    JS_SetPropertyStr(ctx, ioModule, "writeFileAsync", 
                     JS_NewCFunction(ctx, writeFileAsync, "writeFileAsync", 2));
    
    // Add to global scope
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "io", ioModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue IOModule::readFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readFile expects a file path");
    }
    
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    
    std::string filePath(path);
    JS_FreeCString(ctx, path);
    
    // Submit to I/O thread pool
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath]() {
        return readFileSync(filePath);
    });
    
    // Wait for result (in a full implementation, this would be async with Promise)
    // For now, we'll block - in Phase 2 this should return a Promise
    try {
        std::string content = future.get();
        return JS_NewString(ctx, content.c_str());
    } catch (const std::exception& e) {
        std::string errorMsg = "readFile error: " + std::string(e.what());
        return JS_ThrowTypeError(ctx, errorMsg.c_str());
    }
}

JSValue IOModule::writeFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "writeFile expects a file path and content");
    }
    
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    
    const char* content = JS_ToCString(ctx, argv[1]);
    if (!content) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }
    
    std::string filePath(path);
    std::string fileContent(content);
    
    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, content);
    
    // Submit to I/O thread pool
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath, fileContent]() {
        return writeFileSync(filePath, fileContent);
    });
    
    // Wait for result
    try {
        bool success = future.get();
        return JS_NewBool(ctx, success);
    } catch (const std::exception& e) {
        std::string errorMsg = "writeFile error: " + std::string(e.what());
        return JS_ThrowTypeError(ctx, errorMsg.c_str());
    }
}

std::string IOModule::readFileSync(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool IOModule::writeFileSync(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file for writing: " + path);
    }
    
    file << content;
    file.close();
    return file.good();
}

JSValue IOModule::readFileAsync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readFileAsync expects a file path");
    }
    
    const char* path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    
    std::string filePath(path);
    JS_FreeCString(ctx, path);
    
    // Get JSContextWrapper for Deferred
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    // Create Deferred that executes in IOThreadPool
    JSValue deferredObj = JS_NewObject(ctx);
    
    // Create resolve/reject functions
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    JSValue reject = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "reject", 1);
    
    // Submit to IO thread pool
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath]() {
        try {
            return readFileSync(filePath);
        } catch (const std::exception& e) {
            throw;
        }
    });
    
    // Schedule callback when future completes
    EventLoop::getInstance().enqueueCallback([future, resolve, reject, ctx, filePath]() {
        try {
            std::string content = future.get();
            JSValue contentVal = JS_NewString(ctx, content.c_str());
            JSValue args[] = { contentVal };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, contentVal);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, reject, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
        JS_FreeValue(ctx, reject);
    });
    
    // Return a Promise-like object (simplified - would use Deferred)
    // For now, return the deferred object
    return deferredObj;
}

JSValue IOModule::writeFileAsync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "writeFileAsync expects a file path and content");
    }
    
    const char* path = JS_ToCString(ctx, argv[0]);
    const char* content = JS_ToCString(ctx, argv[1]);
    if (!path || !content) {
        if (path) JS_FreeCString(ctx, path);
        if (content) JS_FreeCString(ctx, content);
        return JS_EXCEPTION;
    }
    
    std::string filePath(path);
    std::string fileContent(content);
    
    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, content);
    
    // Submit to IO thread pool
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath, fileContent]() {
        return writeFileSync(filePath, fileContent);
    });
    
    // Create Promise-like object
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    JSValue reject = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "reject", 1);
    
    // Schedule callback
    EventLoop::getInstance().enqueueCallback([future, resolve, reject, ctx]() {
        try {
            bool success = future.get();
            JSValue resultVal = JS_NewBool(ctx, success);
            JSValue args[] = { resultVal };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, resultVal);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, reject, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
        JS_FreeValue(ctx, reject);
    });
    
    return deferredObj;
}

} // namespace protojs
