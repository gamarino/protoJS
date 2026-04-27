#ifndef PROTOJS_COMMONJSLOADER_H
#define PROTOJS_COMMONJSLOADER_H

#include "quickjs.h"
#include <string>
#include <map>
#include <mutex>
#include "headers/protoCore.h"

namespace protojs {

class CommonJSLoader {
public:
    /**
     * @brief Initialize CommonJS loader, expose require() function on
     *        the protoCore-native global.  Returns the updated global.
     *        Migrated; previous JS_SetPropertyStr-on-QuickJS-global
     *        install has been removed (it was unreachable from user
     *        code under the protoCore eval path).
     */
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

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

    static JSValue requireResolve(
        const std::string& specifier,
        const std::string& fromPath,
        JSContext* ctx
    );

private:
    // C callback for require()
    static JSValue requireImpl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

    // C callback for require.resolve()
    static JSValue requireResolveImpl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Helper methods
    static JSValue createModuleObject(const std::string& filePath, JSContext* ctx);
    static JSValue executeModule(
        const std::string& source,
        const std::string& filePath,
        JSValue moduleObj,
        JSValue exportsObj,
        JSValue requireFunc,
        JSValue filenameVal,
        JSValue dirnameVal,
        JSContext* ctx
    );

    // ProtoMethod wrapper for interpreter path
    static const proto::ProtoObject* requireProtoMethod(
        proto::ProtoContext* pCtx,
        const proto::ProtoObject* self,
        const proto::ParentLink* parent,
        const proto::ProtoList* args,
        const proto::ProtoSparseList* locals
    );
};

} // namespace protojs

#endif // PROTOJS_COMMONJSLOADER_H
