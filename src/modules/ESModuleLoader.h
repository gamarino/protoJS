#ifndef PROTOJS_ESMODULELOADER_H
#define PROTOJS_ESMODULELOADER_H

#include "ModuleResolver.h"
#include "ModuleCache.h"
#include "quickjs.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>

namespace protojs {

struct ESModuleRecord {
    std::string specifier;
    std::string filePath;
    JSValue moduleNamespace;
    std::map<std::string, JSValue> exports;
    JSValue defaultExport;
    std::vector<std::string> dependencies;
    std::map<std::string, std::string> importBindings;
    bool isEvaluated;
    bool isEvaluating;
    std::string sourceCode;
    
    ESModuleRecord() : moduleNamespace(JS_UNDEFINED), defaultExport(JS_UNDEFINED),
                       isEvaluated(false), isEvaluating(false) {}
};

class ESModuleLoader {
public:
    static JSValue loadModule(
        const std::string& specifier,
        const std::string& fromPath,
        JSContext* ctx
    );
    
    static void evaluateModule(ESModuleRecord* record, JSContext* ctx);
    static std::unique_ptr<ESModuleRecord> parseModule(
        const std::string& filePath,
        const std::string& source,
        JSContext* ctx
    );
    static void resolveDependencies(ESModuleRecord* record, JSContext* ctx);
    static std::vector<ESModuleRecord*> topologicalSort(
        std::vector<ESModuleRecord*>& modules
    );
    static bool detectCycle(
        ESModuleRecord* start,
        std::set<ESModuleRecord*>& visited,
        std::vector<ESModuleRecord*>& path
    );

private:
    static void extractImports(ESModuleRecord* record, const std::string& source);
    static void extractExports(ESModuleRecord* record, const std::string& source);
    static JSValue createNamespaceObject(ESModuleRecord* record, JSContext* ctx);
    static void bindImports(ESModuleRecord* record, JSContext* ctx);
};

} // namespace protojs

#endif // PROTOJS_ESMODULELOADER_H
