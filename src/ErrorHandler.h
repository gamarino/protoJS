#ifndef PROTOJS_ERRORHANDLER_H
#define PROTOJS_ERRORHANDLER_H

#include "quickjs.h"
#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Error types enum
 */
enum class ErrorType {
    Error,              // Base error
    SystemError,        // System errors (errno-based)
    TypeError,          // Type errors
    RangeError,         // Range errors
    ReferenceError,     // Reference errors
    SyntaxError,        // Syntax errors
    ModuleNotFoundError,// Module not found
    NetworkError        // Network errors
};

/**
 * @brief ErrorHandler provides standardized error handling across protoJS using protoCore objects
 */
class ErrorHandler {
public:
    /**
     * @brief Create a standard error
     * @param pContext ProtoContext for creating protoCore objects
     */
    static JSValue createError(JSContext* ctx, proto::ProtoContext* pContext, ErrorType type, const proto::ProtoString* message, const proto::ProtoObject* code = nullptr);

    /**
     * @brief Create an error with a pre-generated stack trace
     */
    static JSValue createErrorWithStack(JSContext* ctx, proto::ProtoContext* pContext, ErrorType type, const proto::ProtoString* message, const proto::ProtoString* stack);

    /**
     * @brief Create a system error from errno
     */
    static JSValue createSystemError(JSContext* ctx, proto::ProtoContext* pContext, const proto::ProtoObject* errnoValue, const proto::ProtoString* syscall, const proto::ProtoString* path = nullptr);

    /**
     * @brief Attach context information to an error
     */
    static void attachContext(JSContext* ctx, proto::ProtoContext* pContext, JSValue error, const proto::ProtoString* file, const proto::ProtoObject* line, const proto::ProtoString* function);

    /**
     * @brief Attach a stack trace to an error
     */
    static void attachStack(JSContext* ctx, proto::ProtoContext* pContext, JSValue error, const proto::ProtoString* stack);

    /**
     * @brief Get stack trace from current execution context (returns ProtoString)
     */
    static const proto::ProtoString* getStackTrace(JSContext* ctx, proto::ProtoContext* pContext);

    /**
     * @brief Format a stack frame (returns ProtoString)
     */
    static const proto::ProtoString* formatStackFrame(proto::ProtoContext* pContext, const proto::ProtoString* file, const proto::ProtoObject* line, const proto::ProtoString* function);

    /**
     * @brief Get error type from error object
     */
    static ErrorType getErrorType(JSContext* ctx, proto::ProtoContext* pContext, JSValue error);

    /**
     * @brief Get error message from error object (returns ProtoString)
     */
    static const proto::ProtoString* getErrorMessage(JSContext* ctx, proto::ProtoContext* pContext, JSValue error);

    /**
     * @brief Get error code from error object
     */
    static const proto::ProtoObject* getErrorCode(JSContext* ctx, proto::ProtoContext* pContext, JSValue error);

    /**
     * @brief Get error stack trace from error object (returns ProtoString)
     */
    static const proto::ProtoString* getErrorStack(JSContext* ctx, proto::ProtoContext* pContext, JSValue error);

    /**
     * @brief Check if an error can be recovered
     */
    static bool canRecover(JSContext* ctx, proto::ProtoContext* pContext, JSValue error);

    /**
     * @brief Attempt to recover from an error
     */
    static JSValue recover(JSContext* ctx, proto::ProtoContext* pContext, JSValue error, const std::function<JSValue()>& recovery);

private:
    /**
     * @brief Get error type name as ProtoString
     */
    static const proto::ProtoString* getErrorTypeName(proto::ProtoContext* pContext, ErrorType type);

    /**
     * @brief Get system error message from errno (returns ProtoString)
     */
    static const proto::ProtoString* getSystemErrorMessage(proto::ProtoContext* pContext, const proto::ProtoObject* errnoValue);
    
    /**
     * @brief Convert ProtoString to C string for QuickJS (necessary conversion)
     */
    static const char* toStringCString(JSContext* ctx, const proto::ProtoString* str, proto::ProtoContext* pContext);
    
    /**
     * @brief Free C string after use
     */
    static void freeCString(JSContext* ctx, const char* str);
};

} // namespace protojs

#endif // PROTOJS_ERRORHANDLER_H
