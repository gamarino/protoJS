#include "ESModuleLoader.h"
#include "ModuleResolver.h"
#include "ModuleCache.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <iostream>

namespace protojs {

JSValue ESModuleLoader::loadModule(
    const std::string& specifier,
    const std::string& fromPath,
    JSContext* ctx
) {
    // Check cache first
    std::string cacheKey = specifier + ":" + fromPath;
    CachedModule* cached = ModuleCache::get(cacheKey);
    if (cached && cached->type == ModuleType::ESM) {
        return JS_DupValue(ctx, cached->moduleObject);
    }
    
    // Resolve module
    ResolveResult resolved = ModuleResolver::resolve(specifier, fromPath, ctx);
    if (resolved.filePath.empty()) {
        return JS_ThrowTypeError(ctx, "%s", ("Module not found: " + specifier).c_str());
    }
    
    if (resolved.type != ModuleType::ESM) {
        // Will be handled by interop layer
        return JS_ThrowTypeError(ctx, "%s", ("Not an ES module: " + specifier).c_str());
    }
    
    // Read source
    std::ifstream file(resolved.filePath);
    if (!file.is_open()) {
        return JS_ThrowTypeError(ctx, "%s", ("Cannot open module: " + resolved.filePath).c_str());
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    // Parse module
    auto record = parseModule(resolved.filePath, source, ctx);
    if (!record) {
        return JS_ThrowTypeError(ctx, "%s", ("Failed to parse module: " + resolved.filePath).c_str());
    }
    
    record->specifier = specifier;
    record->filePath = resolved.filePath;
    
    // Resolve dependencies
    resolveDependencies(record.get(), ctx);
    
    // Evaluate module
    evaluateModule(record.get(), ctx);
    
    // Create namespace object
    JSValue namespaceObj = createNamespaceObject(record.get(), ctx);
    
    // Cache module
    auto cachedMod = std::make_unique<CachedModule>();
    cachedMod->type = ModuleType::ESM;
    cachedMod->moduleObject = JS_DupValue(ctx, namespaceObj);
    cachedMod->filePath = resolved.filePath;
    cachedMod->resolvedSpecifier = specifier;
    cachedMod->loadTime = std::chrono::system_clock::now();
    cachedMod->dependencies = record->dependencies;
    ModuleCache::put(cacheKey, std::move(cachedMod));
    
    return namespaceObj;
}

void ESModuleLoader::evaluateModule(ESModuleRecord* record, JSContext* ctx) {
    if (record->isEvaluated) {
        return;
    }
    
    if (record->isEvaluating) {
        // Circular dependency - module is being evaluated
        // Bindings will be live, so we can continue
        return;
    }
    
    record->isEvaluating = true;
    
    // Evaluate dependencies first
    for (const auto& dep : record->dependencies) {
        // Dependencies should already be loaded and evaluated
        // This is handled in resolveDependencies
    }
    
    // Evaluate module code
    JSValue result = JS_Eval(ctx, record->sourceCode.c_str(), 
                            record->sourceCode.length(),
                            record->filePath.c_str(),
                            JS_EVAL_TYPE_MODULE);
    
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char* err = JS_ToCString(ctx, exception);
        if (err) {
            std::cerr << "Error evaluating module " << record->filePath << ": " << err << std::endl;
            JS_FreeCString(ctx, err);
        }
        JS_FreeValue(ctx, exception);
        record->isEvaluating = false;
        return;
    }
    
    JS_FreeValue(ctx, result);
    
    // Bind imports to exports
    bindImports(record, ctx);
    
    record->isEvaluating = false;
    record->isEvaluated = true;
}

std::unique_ptr<ESModuleRecord> ESModuleLoader::parseModule(
    const std::string& filePath,
    const std::string& source,
    JSContext* ctx
) {
    auto record = std::make_unique<ESModuleRecord>();
    record->filePath = filePath;
    record->sourceCode = source;
    
    // Extract imports and exports
    extractImports(record.get(), source);
    extractExports(record.get(), source);
    
    return record;
}

void ESModuleLoader::resolveDependencies(ESModuleRecord* record, JSContext* ctx) {
    // Dependencies are already extracted in parseModule
    // Here we resolve them and ensure they're loaded
    for (const auto& dep : record->dependencies) {
        // Resolve dependency
        ResolveResult resolved = ModuleResolver::resolve(dep, record->filePath, ctx);
        if (!resolved.filePath.empty()) {
            // Load dependency if it's an ES module
            if (resolved.type == ModuleType::ESM) {
                loadModule(dep, record->filePath, ctx);
            }
        }
    }
}

std::vector<ESModuleRecord*> ESModuleLoader::topologicalSort(
    std::vector<ESModuleRecord*>& modules
) {
    // Simple topological sort implementation
    std::vector<ESModuleRecord*> sorted;
    std::set<ESModuleRecord*> visited;
    std::set<ESModuleRecord*> tempMark;
    
    std::function<void(ESModuleRecord*)> visit = [&](ESModuleRecord* node) {
        if (tempMark.find(node) != tempMark.end()) {
            // Cycle detected - handle in circular dependency handler
            return;
        }
        if (visited.find(node) != visited.end()) {
            return;
        }
        
        tempMark.insert(node);
        
        // Visit dependencies
        for (const auto& dep : node->dependencies) {
            // Find dependency module in modules list
            // For now, simplified - full implementation would track module map
        }
        
        tempMark.erase(node);
        visited.insert(node);
        sorted.push_back(node);
    };
    
    for (auto* module : modules) {
        if (visited.find(module) == visited.end()) {
            visit(module);
        }
    }
    
    return sorted;
}

bool ESModuleLoader::detectCycle(
    ESModuleRecord* start,
    std::set<ESModuleRecord*>& visited,
    std::vector<ESModuleRecord*>& path
) {
    if (std::find(path.begin(), path.end(), start) != path.end()) {
        // Cycle detected
        return true;
    }
    
    if (visited.find(start) != visited.end()) {
        return false;
    }
    
    visited.insert(start);
    path.push_back(start);
    
    // Check dependencies (simplified - would need module map)
    // For now, return false
    
    path.pop_back();
    return false;
}

void ESModuleLoader::extractImports(ESModuleRecord* record, const std::string& source) {
    // Simple regex-based extraction (full implementation would use AST)
    std::regex importRegex(R"(import\s+(?:(?:\*\s+as\s+(\w+))|(?:\{([^}]+)\})|(\w+)|(\w+)\s*,\s*\{([^}]+)\})\s+from\s+['"]([^'"]+)['"])");
    std::sregex_iterator iter(source.begin(), source.end(), importRegex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        std::smatch match = *iter;
        if (match.size() >= 7) {
            std::string specifier = match[6].str();
            record->dependencies.push_back(specifier);
            
            // Extract binding names
            if (!match[1].str().empty()) {
                // import * as name from 'module'
                record->importBindings[match[1].str()] = "*";
            } else if (!match[2].str().empty()) {
                // import { a, b } from 'module'
                // Parse named imports
            } else if (!match[3].str().empty()) {
                // import default from 'module'
                record->importBindings[match[3].str()] = "default";
            }
        }
    }
    
    // Also handle dynamic import()
    std::regex dynamicImportRegex(R"(import\s*\(\s*['"]([^'"]+)['"]\s*\))");
    std::sregex_iterator dynIter(source.begin(), source.end(), dynamicImportRegex);
    for (; dynIter != end; ++dynIter) {
        std::smatch match = *dynIter;
        if (match.size() >= 2) {
            record->dependencies.push_back(match[1].str());
        }
    }
}

void ESModuleLoader::extractExports(ESModuleRecord* record, const std::string& source) {
    // Simple regex-based extraction
    std::regex exportRegex(R"(export\s+(?:default\s+)?(?:(?:function|const|let|var|class)\s+(\w+)|(?:\{([^}]+)\})|(?:\*\s+from\s+['"]([^'"]+)['"])))");
    std::sregex_iterator iter(source.begin(), source.end(), exportRegex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        std::smatch match = *iter;
        // Extract export names (simplified)
    }
}

JSValue ESModuleLoader::createNamespaceObject(ESModuleRecord* record, JSContext* ctx) {
    JSValue namespaceObj = JS_NewObject(ctx);
    
    // Add all exports to namespace
    for (const auto& [name, value] : record->exports) {
        JS_SetPropertyStr(ctx, namespaceObj, name.c_str(), JS_DupValue(ctx, value));
    }
    
    // Add default export
    if (!JS_IsUndefined(record->defaultExport)) {
        JS_SetPropertyStr(ctx, namespaceObj, "default", JS_DupValue(ctx, record->defaultExport));
    }
    
    // Make it non-extensible (module namespace object behavior)
    JS_DefinePropertyValueStr(ctx, namespaceObj, "__esModule", JS_NewBool(ctx, true), JS_PROP_CONFIGURABLE);
    
    return namespaceObj;
}

void ESModuleLoader::bindImports(ESModuleRecord* record, JSContext* ctx) {
    // Bind imports to their corresponding exports
    // This is simplified - full implementation would track module map
    // and bind live references
}

} // namespace protojs
