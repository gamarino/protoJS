#include "FSModule.h"
#include "../IOModule.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include "../../JSContext.h"
#include "../../IOThreadPool.h"
#include "../../EventLoop.h"
#include "../../Deferred.h"
#include "../../JSContext.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>

namespace protojs {
namespace fs = std::filesystem;

void FSModule::init(JSContext* ctx) {
    JSValue fsModule = JS_NewObject(ctx);
    JSValue promises = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, promises, "readFile", JS_NewCFunction(ctx, promisesReadFile, "readFile", 1));
    JS_SetPropertyStr(ctx, promises, "writeFile", JS_NewCFunction(ctx, promisesWriteFile, "writeFile", 2));
    JS_SetPropertyStr(ctx, promises, "readdir", JS_NewCFunction(ctx, promisesReaddir, "readdir", 1));
    JS_SetPropertyStr(ctx, promises, "mkdir", JS_NewCFunction(ctx, promisesMkdir, "mkdir", 1));
    JS_SetPropertyStr(ctx, promises, "stat", JS_NewCFunction(ctx, promisesStat, "stat", 1));
    
    JS_SetPropertyStr(ctx, fsModule, "promises", promises);
    
    // Sync API
    JS_SetPropertyStr(ctx, fsModule, "readFileSync", JS_NewCFunction(ctx, readFileSync, "readFileSync", 1));
    JS_SetPropertyStr(ctx, fsModule, "writeFileSync", JS_NewCFunction(ctx, writeFileSync, "writeFileSync", 2));
    JS_SetPropertyStr(ctx, fsModule, "readdirSync", JS_NewCFunction(ctx, readdirSync, "readdirSync", 1));
    JS_SetPropertyStr(ctx, fsModule, "mkdirSync", JS_NewCFunction(ctx, mkdirSync, "mkdirSync", 1));
    JS_SetPropertyStr(ctx, fsModule, "statSync", JS_NewCFunction(ctx, statSync, "statSync", 1));
    JS_SetPropertyStr(ctx, fsModule, "unlinkSync", JS_NewCFunction(ctx, unlinkSync, "unlinkSync", 1));
    JS_SetPropertyStr(ctx, fsModule, "rmdirSync", JS_NewCFunction(ctx, rmdirSync, "rmdirSync", 1));
    JS_SetPropertyStr(ctx, fsModule, "renameSync", JS_NewCFunction(ctx, renameSync, "renameSync", 2));
    JS_SetPropertyStr(ctx, fsModule, "copyFileSync", JS_NewCFunction(ctx, copyFileSync, "copyFileSync", 2));
    
    // Stream API
    JS_SetPropertyStr(ctx, fsModule, "createReadStream", JS_NewCFunction(ctx, createReadStream, "createReadStream", 1));
    JS_SetPropertyStr(ctx, fsModule, "createWriteStream", JS_NewCFunction(ctx, createWriteStream, "createWriteStream", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "fs", fsModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue FSModule::promisesReadFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readFile expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::string filePath(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath]() {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + filePath);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    auto futurePtr = std::make_shared<std::future<std::string>>(std::move(future));
    EventLoop::getInstance().enqueueCallback([futurePtr, resolve, ctx]() {
        try {
            std::string content = futurePtr->get();
            JSValue contentVal = JS_NewString(ctx, content.c_str());
            JSValue args[] = { contentVal };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, contentVal);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

JSValue FSModule::promisesWriteFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "writeFile expects a file path and content");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    const char* contentStr = JS_ToCString(ctx, argv[1]);
    if (!pathStr || !contentStr) {
        if (pathStr) JS_FreeCString(ctx, pathStr);
        if (contentStr) JS_FreeCString(ctx, contentStr);
        return JS_EXCEPTION;
    }
    
    std::string filePath(pathStr);
    std::string fileContent(contentStr);
    JS_FreeCString(ctx, pathStr);
    JS_FreeCString(ctx, contentStr);
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath, fileContent]() {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file for writing: " + filePath);
        }
        file << fileContent;
        file.close();
        return file.good();
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    auto futurePtr = std::make_shared<std::future<bool>>(std::move(future));
    EventLoop::getInstance().enqueueCallback([futurePtr, resolve, ctx]() {
        try {
            bool success = futurePtr->get();
            JSValue resultVal = JS_NewBool(ctx, success);
            JSValue args[] = { resultVal };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, resultVal);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

JSValue FSModule::promisesReaddir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readdir expects a directory path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::string dirPath(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    // Create Deferred for async operation
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([dirPath]() {
        std::vector<std::string> entries;
        try {
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                entries.push_back(entry.path().filename().string());
            }
        } catch (const std::exception& e) {
            throw;
        }
        return entries;
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    auto futurePtr = std::make_shared<std::future<std::vector<std::string>>>(std::move(future));
    EventLoop::getInstance().enqueueCallback([futurePtr, resolve, ctx]() {
        try {
            auto entries = futurePtr->get();
            JSValue arr = JS_NewArray(ctx);
            for (size_t i = 0; i < entries.size(); i++) {
                JS_SetPropertyUint32(ctx, arr, i, JS_NewString(ctx, entries[i].c_str()));
            }
            JSValue args[] = { arr };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, arr);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

JSValue FSModule::promisesMkdir(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "mkdir expects a directory path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::string dirPath(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([dirPath]() {
        try {
            fs::create_directories(dirPath);
            return true;
        } catch (...) {
            return false;
        }
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    auto futurePtr = std::make_shared<std::future<bool>>(std::move(future));
    EventLoop::getInstance().enqueueCallback([futurePtr, resolve, ctx]() {
        bool success = futurePtr->get();
        JSValue resultVal = JS_NewBool(ctx, success);
        JSValue args[] = { resultVal };
        JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, resultVal);
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

JSValue FSModule::promisesStat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "stat expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::string filePath(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    auto& ioPool = IOThreadPool::getInstance();
    auto future = ioPool.getExecutor().submit([filePath]() {
        struct stat st;
        if (stat(filePath.c_str(), &st) != 0) {
            throw std::runtime_error("Cannot stat file");
        }
        return st;
    });
    
    JSValue deferredObj = JS_NewObject(ctx);
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    
    auto futurePtr = std::make_shared<std::future<struct stat>>(std::move(future));
    EventLoop::getInstance().enqueueCallback([futurePtr, resolve, ctx]() {
        try {
            struct stat st = futurePtr->get();
            JSValue statsObj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, statsObj, "size", JS_NewInt64(ctx, st.st_size));
            JS_SetPropertyStr(ctx, statsObj, "isFile", JS_NewBool(ctx, S_ISREG(st.st_mode)));
            JS_SetPropertyStr(ctx, statsObj, "isDirectory", JS_NewBool(ctx, S_ISDIR(st.st_mode)));
            JS_SetPropertyStr(ctx, statsObj, "mtime", JS_NewInt64(ctx, st.st_mtime * 1000)); // Convert to milliseconds
            JSValue args[] = { statsObj };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, statsObj);
        } catch (const std::exception& e) {
            JSValue error = JS_NewString(ctx, e.what());
            JSValue args[] = { error };
            JSValue result = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, error);
        }
        JS_FreeValue(ctx, resolve);
    });
    
    return deferredObj;
}

// Sync API implementations
JSValue FSModule::readFileSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readFileSync expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    std::ifstream file(pathStr);
    if (!file.is_open()) {
        JS_FreeCString(ctx, pathStr);
        return JS_ThrowTypeError(ctx, "Cannot open file");
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    JS_FreeCString(ctx, pathStr);
    return JS_NewString(ctx, content.c_str());
}

JSValue FSModule::writeFileSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "writeFileSync expects file path and data");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    const char* dataStr = JS_ToCString(ctx, argv[1]);
    if (!dataStr) {
        JS_FreeCString(ctx, pathStr);
        return JS_EXCEPTION;
    }
    
    std::ofstream file(pathStr);
    if (!file.is_open()) {
        JS_FreeCString(ctx, pathStr);
        JS_FreeCString(ctx, dataStr);
        return JS_ThrowTypeError(ctx, "Cannot open file for writing");
    }
    
    file << dataStr;
    file.close();
    
    JS_FreeCString(ctx, pathStr);
    JS_FreeCString(ctx, dataStr);
    return JS_UNDEFINED;
}

JSValue FSModule::readdirSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "readdirSync expects a directory path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    JSValue arr = JS_NewArray(ctx);
    uint32_t index = 0;
    
    try {
        for (const auto& entry : fs::directory_iterator(pathStr)) {
            std::string name = entry.path().filename().string();
            JS_SetPropertyUint32(ctx, arr, index++, JS_NewString(ctx, name.c_str()));
        }
    } catch (const std::exception& e) {
        JS_FreeCString(ctx, pathStr);
        JS_FreeValue(ctx, arr);
        return JS_ThrowTypeError(ctx, ("Cannot read directory: " + std::string(e.what())).c_str());
    }
    
    JS_FreeCString(ctx, pathStr);
    return arr;
}

JSValue FSModule::mkdirSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "mkdirSync expects a directory path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    bool recursive = false;
    if (argc > 1 && JS_IsObject(ctx, argv[1])) {
        JSValue recursiveVal = JS_GetPropertyStr(ctx, argv[1], "recursive");
        if (!JS_IsUndefined(recursiveVal)) {
            recursive = JS_ToBool(ctx, recursiveVal);
        }
        JS_FreeValue(ctx, recursiveVal);
    }
    
    bool success = false;
    if (recursive) {
        success = fs::create_directories(pathStr);
    } else {
        success = fs::create_directory(pathStr);
    }
    
    JS_FreeCString(ctx, pathStr);
    
    if (!success) {
        return JS_ThrowTypeError(ctx, "Cannot create directory");
    }
    
    return JS_UNDEFINED;
}

JSValue FSModule::statSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "statSync expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    struct stat st;
    if (stat(pathStr, &st) != 0) {
        JS_FreeCString(ctx, pathStr);
        return JS_ThrowTypeError(ctx, "Cannot stat file");
    }
    
    JSValue statsObj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, statsObj, "size", JS_NewInt64(ctx, st.st_size));
    JS_SetPropertyStr(ctx, statsObj, "isFile", JS_NewBool(ctx, S_ISREG(st.st_mode)));
    JS_SetPropertyStr(ctx, statsObj, "isDirectory", JS_NewBool(ctx, S_ISDIR(st.st_mode)));
    JS_SetPropertyStr(ctx, statsObj, "mtime", JS_NewInt64(ctx, st.st_mtime * 1000));
    
    JS_FreeCString(ctx, pathStr);
    return statsObj;
}

JSValue FSModule::unlinkSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "unlinkSync expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    if (unlink(pathStr) != 0) {
        JS_FreeCString(ctx, pathStr);
        return JS_ThrowTypeError(ctx, "Cannot unlink file");
    }
    
    JS_FreeCString(ctx, pathStr);
    return JS_UNDEFINED;
}

JSValue FSModule::rmdirSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "rmdirSync expects a directory path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    if (rmdir(pathStr) != 0) {
        JS_FreeCString(ctx, pathStr);
        return JS_ThrowTypeError(ctx, "Cannot remove directory");
    }
    
    JS_FreeCString(ctx, pathStr);
    return JS_UNDEFINED;
}

JSValue FSModule::renameSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "renameSync expects old and new paths");
    }
    
    const char* oldPath = JS_ToCString(ctx, argv[0]);
    const char* newPath = JS_ToCString(ctx, argv[1]);
    
    if (!oldPath || !newPath) {
        if (oldPath) JS_FreeCString(ctx, oldPath);
        if (newPath) JS_FreeCString(ctx, newPath);
        return JS_EXCEPTION;
    }
    
    if (rename(oldPath, newPath) != 0) {
        JS_FreeCString(ctx, oldPath);
        JS_FreeCString(ctx, newPath);
        return JS_ThrowTypeError(ctx, "Cannot rename file");
    }
    
    JS_FreeCString(ctx, oldPath);
    JS_FreeCString(ctx, newPath);
    return JS_UNDEFINED;
}

JSValue FSModule::copyFileSync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "copyFileSync expects source and destination paths");
    }
    
    const char* srcPath = JS_ToCString(ctx, argv[0]);
    const char* destPath = JS_ToCString(ctx, argv[1]);
    
    if (!srcPath || !destPath) {
        if (srcPath) JS_FreeCString(ctx, srcPath);
        if (destPath) JS_FreeCString(ctx, destPath);
        return JS_EXCEPTION;
    }
    
    try {
        fs::copy_file(srcPath, destPath, fs::copy_options::overwrite_existing);
    } catch (const std::exception& e) {
        JS_FreeCString(ctx, srcPath);
        JS_FreeCString(ctx, destPath);
        return JS_ThrowTypeError(ctx, ("Cannot copy file: " + std::string(e.what())).c_str());
    }
    
    JS_FreeCString(ctx, srcPath);
    JS_FreeCString(ctx, destPath);
    return JS_UNDEFINED;
}

JSValue FSModule::createReadStream(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "createReadStream expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    // Create a ReadableStream for file reading
    // For Phase 2, return a basic readable stream
    JSValue readableCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "ReadableStream");
    if (JS_IsUndefined(readableCtor)) {
        // Fallback: get from stream module
        JSValue streamModule = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "stream");
        if (!JS_IsUndefined(streamModule)) {
            readableCtor = JS_GetPropertyStr(ctx, streamModule, "Readable");
        }
        JS_FreeValue(ctx, streamModule);
    }
    
    JSValue stream = JS_UNDEFINED;
    if (!JS_IsUndefined(readableCtor) && JS_IsFunction(ctx, readableCtor)) {
        stream = JS_CallConstructor(ctx, readableCtor, 0, nullptr);
        JS_SetPropertyStr(ctx, stream, "_path", JS_NewString(ctx, pathStr));
    } else {
        stream = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, stream, "_path", JS_NewString(ctx, pathStr));
    }
    
    JS_FreeCString(ctx, pathStr);
    JS_FreeValue(ctx, readableCtor);
    return stream;
}

JSValue FSModule::createWriteStream(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "createWriteStream expects a file path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    // Create a WritableStream for file writing
    JSValue writableCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "WritableStream");
    if (JS_IsUndefined(writableCtor)) {
        JSValue streamModule = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "stream");
        if (!JS_IsUndefined(streamModule)) {
            writableCtor = JS_GetPropertyStr(ctx, streamModule, "Writable");
        }
        JS_FreeValue(ctx, streamModule);
    }
    
    JSValue stream = JS_UNDEFINED;
    if (!JS_IsUndefined(writableCtor) && JS_IsFunction(ctx, writableCtor)) {
        stream = JS_CallConstructor(ctx, writableCtor, 0, nullptr);
        JS_SetPropertyStr(ctx, stream, "_path", JS_NewString(ctx, pathStr));
    } else {
        stream = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, stream, "_path", JS_NewString(ctx, pathStr));
    }
    
    JS_FreeCString(ctx, pathStr);
    JS_FreeValue(ctx, writableCtor);
    return stream;
}

} // namespace protojs
