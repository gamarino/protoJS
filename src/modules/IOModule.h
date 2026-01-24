#ifndef PROTOJS_IOMODULE_H
#define PROTOJS_IOMODULE_H

#include "quickjs.h"
#include "../IOThreadPool.h"
#include "../EventLoop.h"

namespace protojs {

/**
 * @brief I/O module providing explicit I/O operations that use the I/O thread pool.
 * 
 * All I/O operations are executed in the I/O thread pool to avoid blocking
 * the CPU pool or main thread.
 */
class IOModule {
public:
    /**
     * @brief Initialize the I/O module and register it in the global scope.
     * @param ctx QuickJS context
     */
    static void init(JSContext* ctx);
    
    // Async I/O operations (return Promises) - public for use by fs module
    static JSValue readFileAsync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue writeFileAsync(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

private:
    // I/O operations (synchronous - for backward compatibility)
    static JSValue readFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue writeFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Helper to read file in I/O thread
    static std::string readFileSync(const std::string& path);
    
    // Helper to write file in I/O thread
    static bool writeFileSync(const std::string& path, const std::string& content);
};

} // namespace protojs

#endif // PROTOJS_IOMODULE_H
