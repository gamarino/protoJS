#ifndef PROTOJS_COMMONJSLOADER_H
#define PROTOJS_COMMONJSLOADER_H

#include "quickjs.h"
#include <string>
#include <map>
#include <mutex>

namespace protojs {

class CommonJSLoader {
public:
    /**
     * @brief Initialize CommonJS loader, expose require() function globally
     */
    static void init(JSContext* ctx);

    /**
     * @brief Load and execute a CommonJS module
     * 
     * @param specifier Module identifier (relative or absolute path)
     * @param fromPath Current module path (for relative resolution)
     * @param ctx JavaScript context
     * @return JSValue The module's exports object
     */
    static JSValue require(
        const std::string& specifier,
        const std::string& fromPath,
        JSContext* ctx
    );

private:
    static std::map<std::string, JSValue> moduleCache;
    static std::mutex cacheMutex;

    // C callback for require()
    static JSValue requireImpl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // C callback for require.resolve()
    static JSValue requireResolveImpl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Helper methods
    static JSValue requireResolve(const std::string& specifier, const std::string& fromPath, JSContext* ctx);
    static JSValue createModuleObject(const std::string& filePath, JSContext* ctx);
    static JSValue wrapModule(const std::string& source, const std::string& filePath, JSContext* ctx);
};

} // namespace protojs

#endif // PROTOJS_COMMONJSLOADER_H
