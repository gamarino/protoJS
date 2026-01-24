#include "ProcessModule.h"
#include <cstdlib>
#include <unistd.h>
#include <sys/utsname.h>
#include <limits.h>
#include <string>

namespace protojs {

void ProcessModule::init(JSContext* ctx, int argc, char** argv) {
    JSValue processObj = JS_NewObject(ctx);
    
    // argv - command line arguments
    JSValue argvArray = JS_NewArray(ctx);
    for (int i = 0; i < argc; i++) {
        JS_SetPropertyUint32(ctx, argvArray, i, JS_NewString(ctx, argv[i]));
    }
    JS_DefinePropertyValueStr(ctx, processObj, "argv", argvArray, JS_PROP_CONFIGURABLE);
    
    // env - environment variables
    // Note: environ is not always available, so we'll build env object lazily
    JSValue envObj = JS_NewObject(ctx);
    // For Fase 1, we'll populate common env vars
    // In full implementation, would iterate over all env vars
    const char* path = std::getenv("PATH");
    if (path) JS_SetPropertyStr(ctx, envObj, "PATH", JS_NewString(ctx, path));
    
    const char* home = std::getenv("HOME");
    if (home) JS_SetPropertyStr(ctx, envObj, "HOME", JS_NewString(ctx, home));
    
    const char* user = std::getenv("USER");
    if (user) JS_SetPropertyStr(ctx, envObj, "USER", JS_NewString(ctx, user));
    
    JS_DefinePropertyValueStr(ctx, processObj, "env", envObj, JS_PROP_CONFIGURABLE);
    
    // cwd - current working directory
    JS_SetPropertyStr(ctx, processObj, "cwd", JS_NewCFunction(ctx, GetCwd, "cwd", 0));
    
    // platform
    JS_SetPropertyStr(ctx, processObj, "platform", JS_NewCFunction(ctx, GetPlatform, "platform", 0));
    
    // arch
    JS_SetPropertyStr(ctx, processObj, "arch", JS_NewCFunction(ctx, GetArch, "arch", 0));
    
    // exit
    JS_SetPropertyStr(ctx, processObj, "exit", JS_NewCFunction(ctx, Exit, "exit", 1));
    
    // Add to global scope
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "process", processObj);
    JS_FreeValue(ctx, global_obj);
}

JSValue ProcessModule::GetArgv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // This would need access to argc/argv stored in context
    // For now, return undefined
    return JS_UNDEFINED;
}

JSValue ProcessModule::GetEnv(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Return the env object from process
    JSValue processObj = JS_GetGlobalObject(ctx);
    JSValue envObj = JS_GetPropertyStr(ctx, processObj, "env");
    JS_FreeValue(ctx, processObj);
    return envObj;
}

JSValue ProcessModule::GetCwd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        return JS_NewString(ctx, cwd);
    }
    return JS_NewString(ctx, "");
}

JSValue ProcessModule::GetPlatform(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    struct utsname uts;
    if (uname(&uts) == 0) {
        std::string sysname(uts.sysname);
        // Normalize to Node.js platform names
        if (sysname == "Linux") {
            return JS_NewString(ctx, "linux");
        } else if (sysname == "Darwin") {
            return JS_NewString(ctx, "darwin");
        } else if (sysname == "Windows" || sysname.find("WIN") != std::string::npos) {
            return JS_NewString(ctx, "win32");
        }
        return JS_NewString(ctx, sysname.c_str());
    }
    return JS_NewString(ctx, "unknown");
}

JSValue ProcessModule::GetArch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    struct utsname uts;
    if (uname(&uts) == 0) {
        std::string machine(uts.machine);
        // Normalize to Node.js arch names
        if (machine == "x86_64" || machine == "amd64") {
            return JS_NewString(ctx, "x64");
        } else if (machine == "i386" || machine == "i686") {
            return JS_NewString(ctx, "ia32");
        } else if (machine.find("arm") != std::string::npos) {
            return JS_NewString(ctx, "arm");
        }
        return JS_NewString(ctx, machine.c_str());
    }
    return JS_NewString(ctx, "unknown");
}

JSValue ProcessModule::Exit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    int exitCode = 0;
    if (argc > 0 && JS_IsNumber(argv[0])) {
        JS_ToInt32(ctx, &exitCode, argv[0]);
    }
    std::exit(exitCode);
    return JS_UNDEFINED; // Never reached
}

} // namespace protojs
