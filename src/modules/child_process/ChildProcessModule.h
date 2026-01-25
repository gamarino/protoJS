#ifndef PROTOJS_CHILDPROCESSMODULE_H
#define PROTOJS_CHILDPROCESSMODULE_H

#include "quickjs.h"
#include <string>
#include <vector>
#include <sys/types.h>

namespace protojs {

class ChildProcessModule {
public:
    static void init(JSContext* ctx);

private:
    // Process creation methods
    static JSValue spawn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue execFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue fork(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Child process methods
    static JSValue childKill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue childSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static void ChildProcessFinalizer(JSRuntime* rt, JSValue val);
    
    // Helper functions
    static void spawnProcess(JSContext* ctx, const std::string& command, const std::vector<std::string>& args, 
                             JSValue options, JSValue childObj);
};

} // namespace protojs

#endif // PROTOJS_CHILDPROCESSMODULE_H
