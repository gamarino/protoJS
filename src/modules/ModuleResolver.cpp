#include "ModuleResolver.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace protojs {

namespace fs = std::filesystem;

ResolveResult ModuleResolver::resolve(
    const std::string& specifier,
    const std::string& fromPath,
    JSContext* ctx
) {
    ResolveResult result;
    
    // Normalize fromPath to directory
    std::string fromDir = isFile(fromPath) ? getDirectory(fromPath) : fromPath;
    if (fromDir.empty()) {
        fromDir = ".";
    }
    
    // Check if specifier is absolute path
    if (specifier[0] == '/') {
        result.filePath = normalizePath(specifier);
        if (isFile(result.filePath)) {
            // Determine type from extension
            if (result.filePath.size() >= 4 && result.filePath.substr(result.filePath.size() - 4) == ".mjs") {
                result.type = ModuleType::ESM;
            } else if (result.filePath.size() >= 8 && result.filePath.substr(result.filePath.size() - 8) == ".protojs") {
                result.type = ModuleType::Native;
            } else {
                result.type = ModuleType::CommonJS;
            }
            return result;
        }
        // Try with extensions
        std::string withExt = tryExtensions(result.filePath);
        if (!withExt.empty()) {
            result.filePath = withExt;
            if (result.filePath.size() >= 4 && result.filePath.substr(result.filePath.size() - 4) == ".mjs") {
                result.type = ModuleType::ESM;
            } else if (result.filePath.size() >= 8 && result.filePath.substr(result.filePath.size() - 8) == ".protojs") {
                result.type = ModuleType::Native;
            }
            return result;
        }
        // Try as directory
        if (isDirectory(result.filePath)) {
            std::string indexFile = tryDirectory(result.filePath);
            if (!indexFile.empty()) {
                result.filePath = indexFile;
                if (result.filePath.size() >= 4 && result.filePath.substr(result.filePath.size() - 4) == ".mjs") {
                    result.type = ModuleType::ESM;
                }
                return result;
            }
        }
        // Not found
        return result;
    }
    
    // Check if specifier is relative path (starts with ./ or ../)
    if ((specifier.size() >= 2 && specifier.substr(0, 2) == "./") || 
        (specifier.size() >= 3 && specifier.substr(0, 3) == "../")) {
        std::string resolved = normalizePath(fromDir + "/" + specifier);
        
        if (isFile(resolved)) {
            result.filePath = resolved;
            if (resolved.size() >= 4 && resolved.substr(resolved.size() - 4) == ".mjs") {
                result.type = ModuleType::ESM;
            } else if (resolved.size() >= 8 && resolved.substr(resolved.size() - 8) == ".protojs") {
                result.type = ModuleType::Native;
            } else {
                result.type = ModuleType::CommonJS;
            }
            return result;
        }
        
        // Try with extensions
        std::string withExt = tryExtensions(resolved);
        if (!withExt.empty()) {
            result.filePath = withExt;
            if (result.filePath.size() >= 4 && result.filePath.substr(result.filePath.size() - 4) == ".mjs") {
                result.type = ModuleType::ESM;
            } else if (result.filePath.size() >= 8 && result.filePath.substr(result.filePath.size() - 8) == ".protojs") {
                result.type = ModuleType::Native;
            }
            return result;
        }
        
        // Try as directory
        if (isDirectory(resolved)) {
            std::string indexFile = tryDirectory(resolved);
            if (!indexFile.empty()) {
                result.filePath = indexFile;
                if (result.filePath.size() >= 4 && result.filePath.substr(result.filePath.size() - 4) == ".mjs") {
                    result.type = ModuleType::ESM;
                }
                return result;
            }
        }
        
        // Not found
        return result;
    }
    
    // Bare specifier - resolve from node_modules
    return resolveNodeModules(specifier, fromDir);
}

bool ModuleResolver::isFile(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISREG(st.st_mode);
    }
    return false;
}

bool ModuleResolver::isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

std::string ModuleResolver::getDirectory(const std::string& filePath) {
    size_t lastSlash = filePath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        return ".";
    }
    return filePath.substr(0, lastSlash);
}

std::string ModuleResolver::normalizePath(const std::string& path) {
    try {
        return fs::canonical(fs::path(path)).string();
    } catch (...) {
        // If canonical fails, try to normalize manually
        std::string normalized = path;
        // Replace // with /
        size_t pos = 0;
        while ((pos = normalized.find("//", pos)) != std::string::npos) {
            normalized.replace(pos, 2, "/");
        }
        // Handle . and ..
        // Simplified - full implementation would handle all cases
        return normalized;
    }
}

std::string ModuleResolver::findPackageJson(const std::string& dir) {
    std::string current = dir;
    
    while (!current.empty() && current != "/") {
        std::string packageJson = current + "/package.json";
        if (isFile(packageJson)) {
            return packageJson;
        }
        
        // Move to parent directory
        size_t lastSlash = current.find_last_of('/');
        if (lastSlash == std::string::npos) {
            break;
        }
        current = current.substr(0, lastSlash);
        if (current.empty()) {
            current = "/";
        }
    }
    
    return "";
}

ResolveResult ModuleResolver::resolveNodeModules(
    const std::string& specifier,
    const std::string& startDir
) {
    ResolveResult result;
    std::string current = normalizePath(startDir);
    
    // Check for scoped package (@scope/package)
    size_t scopePos = specifier.find('/');
    std::string packageName = (scopePos != std::string::npos) 
        ? specifier.substr(0, scopePos + 1) + specifier.substr(scopePos + 1, specifier.find('/', scopePos + 1) - scopePos - 1)
        : specifier.substr(0, specifier.find('/'));
    
    // Walk up directory tree looking for node_modules
    while (!current.empty() && current != "/") {
        std::string nodeModules = current + "/node_modules";
        
        if (isDirectory(nodeModules)) {
            // Check if package exists
            std::string packageDir = nodeModules + "/" + packageName;
            
            if (isDirectory(packageDir)) {
                std::string packageJson = packageDir + "/package.json";
                
                if (isFile(packageJson)) {
                    // Parse package.json
                    std::string main, module, type;
                    std::map<std::string, std::string> exports;
                    
                    if (parsePackageJson(packageJson, main, module, type, exports)) {
                        result.isPackage = true;
                        result.packageName = packageName;
                        
                        // Check if there's a subpath in specifier
                        std::string subpath;
                        if (scopePos != std::string::npos) {
                            size_t nextSlash = specifier.find('/', scopePos + 1);
                            if (nextSlash != std::string::npos) {
                                subpath = specifier.substr(nextSlash + 1);
                            }
                        } else {
                            size_t firstSlash = specifier.find('/');
                            if (firstSlash != std::string::npos) {
                                subpath = specifier.substr(firstSlash + 1);
                            }
                        }
                        
                        // Try exports field first (modern)
                        if (!exports.empty() && !subpath.empty()) {
                            ResolveResult exportsResult = resolveExports(packageJson, subpath, packageDir);
                            if (!exportsResult.filePath.empty()) {
                                return exportsResult;
                            }
                        }
                        
                        // Try main/module fields
                        std::string entryPoint;
                        if (!module.empty() && type == "module") {
                            entryPoint = module;
                            result.type = ModuleType::ESM;
                        } else if (!main.empty()) {
                            entryPoint = main;
                            result.type = (type == "module") ? ModuleType::ESM : ModuleType::CommonJS;
                        } else {
                            // Default to index.js
                            entryPoint = "index.js";
                            result.type = ModuleType::CommonJS;
                        }
                        
                        std::string fullPath = packageDir + "/" + entryPoint;
                        if (isFile(fullPath)) {
                            result.filePath = normalizePath(fullPath);
                            return result;
                        }
                        
                        // Try with extensions
                        std::string withExt = tryExtensions(packageDir + "/" + entryPoint);
                        if (!withExt.empty()) {
                            result.filePath = normalizePath(withExt);
                            if (result.filePath.size() >= 4 && result.filePath.substr(result.filePath.size() - 4) == ".mjs") {
                                result.type = ModuleType::ESM;
                            }
                            return result;
                        }
                        
                        // Try directory
                        if (isDirectory(packageDir + "/" + entryPoint)) {
                            std::string indexFile = tryDirectory(packageDir + "/" + entryPoint);
                            if (!indexFile.empty()) {
                                result.filePath = normalizePath(indexFile);
                                return result;
                            }
                        }
                    }
                }
            }
        }
        
        // Move to parent directory
        size_t lastSlash = current.find_last_of('/');
        if (lastSlash == std::string::npos) {
            break;
        }
        current = current.substr(0, lastSlash);
        if (current.empty()) {
            current = "/";
        }
    }
    
    return result;
}

ResolveResult ModuleResolver::resolveExports(
    const std::string& packageJsonPath,
    const std::string& specifier,
    const std::string& packageDir
) {
    ResolveResult result;
    
    std::string main, module, type;
    std::map<std::string, std::string> exports;
    
    if (!parsePackageJson(packageJsonPath, main, module, type, exports)) {
        return result;
    }
    
    // Simple exports resolution (full implementation would handle conditions)
    auto it = exports.find(specifier);
    if (it != exports.end()) {
        std::string exportPath = packageDir + "/" + it->second;
        if (isFile(exportPath)) {
            result.filePath = normalizePath(exportPath);
            result.type = (type == "module" || (exportPath.size() >= 4 && exportPath.substr(exportPath.size() - 4) == ".mjs"))
                ? ModuleType::ESM 
                : ModuleType::CommonJS;
            return result;
        }
        
        // Try with extensions
        std::string withExt = tryExtensions(exportPath);
        if (!withExt.empty()) {
            result.filePath = normalizePath(withExt);
            result.type = (type == "module" || (result.filePath.size() >= 4 && result.filePath.substr(result.filePath.size() - 4) == ".mjs"))
                ? ModuleType::ESM
                : ModuleType::CommonJS;
            return result;
        }
    }
    
    return result;
}

bool ModuleResolver::parsePackageJson(
    const std::string& packageJsonPath,
    std::string& main,
    std::string& module,
    std::string& type,
    std::map<std::string, std::string>& exports
) {
    std::ifstream file(packageJsonPath);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Simple JSON parsing (for Phase 2, full JSON parser would be better)
    // Extract main field
    size_t mainPos = content.find("\"main\"");
    if (mainPos != std::string::npos) {
        size_t colonPos = content.find(':', mainPos);
        size_t startQuote = content.find('"', colonPos);
        size_t endQuote = content.find('"', startQuote + 1);
        if (startQuote != std::string::npos && endQuote != std::string::npos) {
            main = content.substr(startQuote + 1, endQuote - startQuote - 1);
        }
    }
    
    // Extract module field
    size_t modulePos = content.find("\"module\"");
    if (modulePos != std::string::npos) {
        size_t colonPos = content.find(':', modulePos);
        size_t startQuote = content.find('"', colonPos);
        size_t endQuote = content.find('"', startQuote + 1);
        if (startQuote != std::string::npos && endQuote != std::string::npos) {
            module = content.substr(startQuote + 1, endQuote - startQuote - 1);
        }
    }
    
    // Extract type field
    size_t typePos = content.find("\"type\"");
    if (typePos != std::string::npos) {
        size_t colonPos = content.find(':', typePos);
        size_t startQuote = content.find('"', colonPos);
        size_t endQuote = content.find('"', startQuote + 1);
        if (startQuote != std::string::npos && endQuote != std::string::npos) {
            type = content.substr(startQuote + 1, endQuote - startQuote - 1);
        }
    }
    
    // Extract exports field (simplified - full implementation would parse object)
    size_t exportsPos = content.find("\"exports\"");
    if (exportsPos != std::string::npos) {
        // Simple parsing - look for key-value pairs
        // Full implementation would use proper JSON parser
        size_t braceStart = content.find('{', exportsPos);
        if (braceStart != std::string::npos) {
            size_t braceEnd = content.find('}', braceStart);
            if (braceEnd != std::string::npos) {
                std::string exportsStr = content.substr(braceStart + 1, braceEnd - braceStart - 1);
                // Parse simple exports object (simplified)
                // In full implementation, use proper JSON parser
            }
        }
    }
    
    return true;
}

std::string ModuleResolver::tryExtensions(const std::string& basePath, const std::vector<std::string>& extensions) {
    for (const auto& ext : extensions) {
        std::string withExt = basePath + ext;
        if (isFile(withExt)) {
            return withExt;
        }
    }
    return "";
}

std::string ModuleResolver::tryDirectory(const std::string& dirPath) {
    std::vector<std::string> indexFiles = {
        "index.js",
        "index.mjs",
        "index.protojs"
    };
    
    for (const auto& indexFile : indexFiles) {
        std::string fullPath = dirPath + "/" + indexFile;
        if (isFile(fullPath)) {
            return fullPath;
        }
    }
    
    return "";
}

} // namespace protojs
