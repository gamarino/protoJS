#ifndef PROTOJS_ASYNCMODULELOADER_H
#define PROTOJS_ASYNCMODULELOADER_H
#include "ESModuleLoader.h"
#include "quickjs.h"
#include <string>
namespace protojs {
class AsyncModuleLoader {
public:
    static JSValue dynamicImport(const std::string& specifier, JSContext* ctx);
    static JSValue evaluateAsyncModule(ESModuleRecord* record, JSContext* ctx);
};
} // namespace protojs
#endif
