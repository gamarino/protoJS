#include "ErrorHandler.h"
#include "JSContext.h"
#include <cstring>
#include <cerrno>
#include <functional>

namespace protojs {

// Helper to convert ProtoString to C string for QuickJS (necessary conversion)
static const char* protoStringToCString(JSContext* ctx, const proto::ProtoString* str, proto::ProtoContext* pContext) {
    if (!str) return nullptr;
    
    // Convert ProtoString to UTF-8 C string
    // We need to iterate over characters and build UTF-8 string
    const proto::ProtoList* charList = str->asList(pContext);
    std::string result;
    result.reserve(str->getSize(pContext) * 4);
    
    unsigned long size = charList->getSize(pContext);
    for (unsigned long i = 0; i < size; i++) {
        const proto::ProtoObject* charObj = charList->getAt(pContext, i);
        unsigned int unicodeChar = charObj->asLong(pContext);
        
        // Convert Unicode to UTF-8
        if (unicodeChar < 0x80) {
            result += static_cast<char>(unicodeChar);
        } else if (unicodeChar < 0x800) {
            result += static_cast<char>(0xC0 | (unicodeChar >> 6));
            result += static_cast<char>(0x80 | (unicodeChar & 0x3F));
        } else if (unicodeChar < 0x10000) {
            result += static_cast<char>(0xE0 | (unicodeChar >> 12));
            result += static_cast<char>(0x80 | ((unicodeChar >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (unicodeChar & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (unicodeChar >> 18));
            result += static_cast<char>(0x80 | ((unicodeChar >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((unicodeChar >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (unicodeChar & 0x3F));
        }
    }
    
    // Allocate C string using QuickJS (will be freed by JS_FreeCString)
    JSValue jsStr = JS_NewString(ctx, result.c_str());
    if (JS_IsString(jsStr)) {
        const char* cstr = JS_ToCString(ctx, jsStr);
        JS_FreeValue(ctx, jsStr);
        return cstr;
    }
    JS_FreeValue(ctx, jsStr);
    return nullptr;
}

JSValue ErrorHandler::createError(JSContext* ctx, proto::ProtoContext* pContext, ErrorType type, const proto::ProtoString* message, const proto::ProtoObject* code) {
    JSValue error = JS_NewError(ctx);
    
    // Set error name
    const proto::ProtoString* typeName = getErrorTypeName(pContext, type);
    const char* typeNameCStr = protoStringToCString(ctx, typeName, pContext);
    if (typeNameCStr) {
        JS_SetPropertyStr(ctx, error, "name", JS_NewString(ctx, typeNameCStr));
        JS_FreeCString(ctx, typeNameCStr);
    }
    
    // Set error message
    const char* messageCStr = protoStringToCString(ctx, message, pContext);
    if (messageCStr) {
        JS_SetPropertyStr(ctx, error, "message", JS_NewString(ctx, messageCStr));
        JS_FreeCString(ctx, messageCStr);
    }
    
    // Set error code if provided
    if (code) {
        long long codeValue = code->asLong(pContext);
        if (codeValue != 0) {
            JS_SetPropertyStr(ctx, error, "code", JS_NewInt32(ctx, static_cast<int32_t>(codeValue)));
        }
    }
    
    // Set error type
    const char* typeNameCStr2 = protoStringToCString(ctx, typeName, pContext);
    if (typeNameCStr2) {
        JS_SetPropertyStr(ctx, error, "type", JS_NewString(ctx, typeNameCStr2));
        JS_FreeCString(ctx, typeNameCStr2);
    }
    
    // Generate and attach stack trace
    const proto::ProtoString* stack = getStackTrace(ctx, pContext);
    if (stack && stack->getSize(pContext) > 0) {
        attachStack(ctx, pContext, error, stack);
    }
    
    return error;
}

JSValue ErrorHandler::createErrorWithStack(JSContext* ctx, proto::ProtoContext* pContext, ErrorType type, const proto::ProtoString* message, const proto::ProtoString* stack) {
    JSValue error = createError(ctx, pContext, type, message);
    attachStack(ctx, pContext, error, stack);
    return error;
}

JSValue ErrorHandler::createSystemError(JSContext* ctx, proto::ProtoContext* pContext, const proto::ProtoObject* errnoValue, const proto::ProtoString* syscall, const proto::ProtoString* path) {
    // Build message using protoCore string operations
    const proto::ProtoString* message = syscall;
    if (path && path->getSize(pContext) > 0) {
        message = message->appendLast(pContext, pContext->fromUTF8String(" ")->asString(pContext));
        message = message->appendLast(pContext, path);
    }
    message = message->appendLast(pContext, pContext->fromUTF8String(": ")->asString(pContext));
    const proto::ProtoString* errMsg = getSystemErrorMessage(pContext, errnoValue);
    message = message->appendLast(pContext, errMsg);
    
    JSValue error = createError(ctx, pContext, ErrorType::SystemError, message, errnoValue);
    
    // Set syscall
    const char* syscallCStr = protoStringToCString(ctx, syscall, pContext);
    if (syscallCStr) {
        JS_SetPropertyStr(ctx, error, "syscall", JS_NewString(ctx, syscallCStr));
        JS_FreeCString(ctx, syscallCStr);
    }
    
    // Set errno
    long long errnoVal = errnoValue->asLong(pContext);
    JS_SetPropertyStr(ctx, error, "errno", JS_NewInt32(ctx, static_cast<int32_t>(errnoVal)));
    
    // Set path if provided
    if (path && path->getSize(pContext) > 0) {
        const char* pathCStr = protoStringToCString(ctx, path, pContext);
        if (pathCStr) {
            JS_SetPropertyStr(ctx, error, "path", JS_NewString(ctx, pathCStr));
            JS_FreeCString(ctx, pathCStr);
        }
    }
    
    return error;
}

void ErrorHandler::attachContext(JSContext* ctx, proto::ProtoContext* pContext, JSValue error, const proto::ProtoString* file, const proto::ProtoObject* line, const proto::ProtoString* function) {
    if (file && file->getSize(pContext) > 0) {
        const char* fileCStr = protoStringToCString(ctx, file, pContext);
        if (fileCStr) {
            JS_SetPropertyStr(ctx, error, "fileName", JS_NewString(ctx, fileCStr));
            JS_FreeCString(ctx, fileCStr);
        }
    }
    
    long long lineValue = line->asLong(pContext);
    JS_SetPropertyStr(ctx, error, "lineNumber", JS_NewInt32(ctx, static_cast<int32_t>(lineValue)));
    
    if (function && function->getSize(pContext) > 0) {
        const char* funcCStr = protoStringToCString(ctx, function, pContext);
        if (funcCStr) {
            JS_SetPropertyStr(ctx, error, "functionName", JS_NewString(ctx, funcCStr));
            JS_FreeCString(ctx, funcCStr);
        }
    }
}

void ErrorHandler::attachStack(JSContext* ctx, proto::ProtoContext* pContext, JSValue error, const proto::ProtoString* stack) {
    const char* stackCStr = protoStringToCString(ctx, stack, pContext);
    if (stackCStr) {
        JS_SetPropertyStr(ctx, error, "stack", JS_NewString(ctx, stackCStr));
        JS_FreeCString(ctx, stackCStr);
    }
}

const proto::ProtoString* ErrorHandler::getStackTrace(JSContext* ctx, proto::ProtoContext* pContext) {
    // QuickJS doesn't provide direct stack trace access
    // Return basic stack trace as ProtoString
    return pContext->fromUTF8String("    at (unknown location)\n")->asString(pContext);
}

const proto::ProtoString* ErrorHandler::formatStackFrame(proto::ProtoContext* pContext, const proto::ProtoString* file, const proto::ProtoObject* line, const proto::ProtoString* function) {
    // Build stack frame using protoCore string operations
    const proto::ProtoString* frame = pContext->fromUTF8String("    at ")->asString(pContext);
    
    if (function && function->getSize(pContext) > 0) {
        frame = frame->appendLast(pContext, function);
    } else {
        frame = frame->appendLast(pContext, pContext->fromUTF8String("(anonymous)")->asString(pContext));
    }
    
    frame = frame->appendLast(pContext, pContext->fromUTF8String(" (")->asString(pContext));
    frame = frame->appendLast(pContext, file);
    frame = frame->appendLast(pContext, pContext->fromUTF8String(":")->asString(pContext));
    
    // Convert line to string
    long long lineValue = line->asLong(pContext);
    char lineBuf[32];
    snprintf(lineBuf, sizeof(lineBuf), "%lld", lineValue);
    frame = frame->appendLast(pContext, pContext->fromUTF8String(lineBuf)->asString(pContext));
    frame = frame->appendLast(pContext, pContext->fromUTF8String(")")->asString(pContext));
    
    return frame;
}

ErrorType ErrorHandler::getErrorType(JSContext* ctx, proto::ProtoContext* pContext, JSValue error) {
    JSValue nameVal = JS_GetPropertyStr(ctx, error, "name");
    if (JS_IsString(nameVal)) {
        const char* nameStr = JS_ToCString(ctx, nameVal);
        // Convert to ProtoString for comparison
        const proto::ProtoString* name = pContext->fromUTF8String(nameStr)->asString(pContext);
        JS_FreeCString(ctx, nameStr);
        JS_FreeValue(ctx, nameVal);
        
        // Compare with error type names
        const proto::ProtoString* systemError = pContext->fromUTF8String("SystemError")->asString(pContext);
        const proto::ProtoString* typeError = pContext->fromUTF8String("TypeError")->asString(pContext);
        const proto::ProtoString* rangeError = pContext->fromUTF8String("RangeError")->asString(pContext);
        const proto::ProtoString* referenceError = pContext->fromUTF8String("ReferenceError")->asString(pContext);
        const proto::ProtoString* syntaxError = pContext->fromUTF8String("SyntaxError")->asString(pContext);
        const proto::ProtoString* moduleNotFoundError = pContext->fromUTF8String("ModuleNotFoundError")->asString(pContext);
        const proto::ProtoString* networkError = pContext->fromUTF8String("NetworkError")->asString(pContext);
        
        if (name->cmp_to_string(pContext, systemError) == 0) return ErrorType::SystemError;
        if (name->cmp_to_string(pContext, typeError) == 0) return ErrorType::TypeError;
        if (name->cmp_to_string(pContext, rangeError) == 0) return ErrorType::RangeError;
        if (name->cmp_to_string(pContext, referenceError) == 0) return ErrorType::ReferenceError;
        if (name->cmp_to_string(pContext, syntaxError) == 0) return ErrorType::SyntaxError;
        if (name->cmp_to_string(pContext, moduleNotFoundError) == 0) return ErrorType::ModuleNotFoundError;
        if (name->cmp_to_string(pContext, networkError) == 0) return ErrorType::NetworkError;
    } else {
        JS_FreeValue(ctx, nameVal);
    }
    
    return ErrorType::Error;
}

const proto::ProtoString* ErrorHandler::getErrorMessage(JSContext* ctx, proto::ProtoContext* pContext, JSValue error) {
    JSValue msgVal = JS_GetPropertyStr(ctx, error, "message");
    if (JS_IsString(msgVal)) {
        const char* msgStr = JS_ToCString(ctx, msgVal);
        const proto::ProtoString* msg = pContext->fromUTF8String(msgStr)->asString(pContext);
        JS_FreeCString(ctx, msgStr);
        JS_FreeValue(ctx, msgVal);
        return msg;
    }
    JS_FreeValue(ctx, msgVal);
    return pContext->fromUTF8String("")->asString(pContext);
}

const proto::ProtoObject* ErrorHandler::getErrorCode(JSContext* ctx, proto::ProtoContext* pContext, JSValue error) {
    JSValue codeVal = JS_GetPropertyStr(ctx, error, "code");
    if (JS_IsNumber(codeVal)) {
        int32_t code;
        JS_ToInt32(ctx, &code, codeVal);
        JS_FreeValue(ctx, codeVal);
        return pContext->fromInteger(code);
    }
    JS_FreeValue(ctx, codeVal);
    return pContext->fromInteger(0);
}

const proto::ProtoString* ErrorHandler::getErrorStack(JSContext* ctx, proto::ProtoContext* pContext, JSValue error) {
    JSValue stackVal = JS_GetPropertyStr(ctx, error, "stack");
    if (JS_IsString(stackVal)) {
        const char* stackStr = JS_ToCString(ctx, stackVal);
        const proto::ProtoString* stack = pContext->fromUTF8String(stackStr)->asString(pContext);
        JS_FreeCString(ctx, stackStr);
        JS_FreeValue(ctx, stackVal);
        return stack;
    }
    JS_FreeValue(ctx, stackVal);
    return pContext->fromUTF8String("")->asString(pContext);
}

bool ErrorHandler::canRecover(JSContext* ctx, proto::ProtoContext* pContext, JSValue error) {
    ErrorType type = getErrorType(ctx, pContext, error);
    
    if (type == ErrorType::SystemError) {
        const proto::ProtoObject* code = getErrorCode(ctx, pContext, error);
        long long codeValue = code->asLong(pContext);
        return (codeValue == EAGAIN || codeValue == EINTR);
    }
    
    return false;
}

JSValue ErrorHandler::recover(JSContext* ctx, proto::ProtoContext* pContext, JSValue error, const std::function<JSValue()>& recovery) {
    if (canRecover(ctx, pContext, error)) {
        try {
            return recovery();
        } catch (...) {
            return JS_Throw(ctx, error);
        }
    }
    return JS_Throw(ctx, error);
}

const proto::ProtoString* ErrorHandler::getErrorTypeName(proto::ProtoContext* pContext, ErrorType type) {
    switch (type) {
        case ErrorType::Error: return pContext->fromUTF8String("Error")->asString(pContext);
        case ErrorType::SystemError: return pContext->fromUTF8String("SystemError")->asString(pContext);
        case ErrorType::TypeError: return pContext->fromUTF8String("TypeError")->asString(pContext);
        case ErrorType::RangeError: return pContext->fromUTF8String("RangeError")->asString(pContext);
        case ErrorType::ReferenceError: return pContext->fromUTF8String("ReferenceError")->asString(pContext);
        case ErrorType::SyntaxError: return pContext->fromUTF8String("SyntaxError")->asString(pContext);
        case ErrorType::ModuleNotFoundError: return pContext->fromUTF8String("ModuleNotFoundError")->asString(pContext);
        case ErrorType::NetworkError: return pContext->fromUTF8String("NetworkError")->asString(pContext);
        default: return pContext->fromUTF8String("Error")->asString(pContext);
    }
}

const proto::ProtoString* ErrorHandler::getSystemErrorMessage(proto::ProtoContext* pContext, const proto::ProtoObject* errnoValue) {
    long long errnoVal = errnoValue->asLong(pContext);
    char buffer[256];
    const char* msg = strerror_r(static_cast<int>(errnoVal), buffer, sizeof(buffer));
    return pContext->fromUTF8String(msg ? msg : "Unknown error")->asString(pContext);
}

} // namespace protojs
