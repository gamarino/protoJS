#ifndef PROTOJS_MODULERESOLVER_H
#define PROTOJS_MODULERESOLVER_H

#include "quickjs.h"
#include <string>
#include <map>
#include <vector>

namespace protojs {

enum class ModuleType {
    ESM,        // ES Module (.mjs or type: "module" in package.json)
    CommonJS,   // CommonJS (.js or type: "commonjs")
    Native      // Native module (.protojs)
};

struct ResolveResult {
    std::string filePath;
    ModuleType type;
    std::string packageName;
    std::string packageVersion;
    bool isPackage; // true if resolved from node_modules
    
    ResolveResult() : type(ModuleType::CommonJS), isPackage(false) {}
};

class ModuleResolver {
public:
    static ResolveResult resolve(
        const std::string& specifier,
        const std::string& fromPath,
        JSContext* ctx
    );
    
    static bool isFile(const std::string& path);
    static bool isDirectory(const std::string& path);
    static std::string getDirectory(const std::string& filePath);
    static std::string normalizePath(const std::string& path);

private:
    static std::string findPackageJson(const std::string& dir);
    static ResolveResult resolveNodeModules(
        const std::string& specifier,
        const std::string& startDir
    );
    static ResolveResult resolveExports(
        const std::string& packageJsonPath,
        const std::string& specifier,
        const std::string& packageDir
    );
    static bool parsePackageJson(
        const std::string& packageJsonPath,
        std::string& main,
        std::string& module,
        std::string& type,
        std::map<std::string, std::string>& exports
    );
    static std::string tryExtensions(
        const std::string& basePath,
        const std::vector<std::string>& extensions = {".js", ".mjs", ".protojs"}
    );
    static std::string tryDirectory(const std::string& dirPath);
};

} // namespace protojs

#endif // PROTOJS_MODULERESOLVER_H
