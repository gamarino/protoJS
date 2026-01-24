#include "PackageResolver.h"
#include "../modules/ModuleResolver.h"
#include <filesystem>
#include <fstream>
#include <sstream>
namespace protojs {
namespace fs = std::filesystem;
PackageInfo PackageResolver::resolvePackage(const std::string& packageName, const std::string& fromPath) {
    PackageInfo info;
    std::string current = ModuleResolver::normalizePath(fromPath);
    while (!current.empty() && current != "/") {
        std::string nodeModules = current + "/node_modules";
        if (ModuleResolver::isDirectory(nodeModules)) {
            std::string packageDir = nodeModules + "/" + packageName;
            if (ModuleResolver::isDirectory(packageDir)) {
                std::string packageJson = packageDir + "/package.json";
                if (ModuleResolver::isFile(packageJson)) {
                    std::ifstream file(packageJson);
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string content = buffer.str();
                    size_t namePos = content.find("\"name\"");
                    if (namePos != std::string::npos) {
                        size_t start = content.find('"', namePos + 6) + 1;
                        size_t end = content.find('"', start);
                        if (end != std::string::npos) info.name = content.substr(start, end - start);
                    }
                    info.path = packageDir;
                    return info;
                }
            }
        }
        size_t lastSlash = current.find_last_of('/');
        if (lastSlash == std::string::npos) break;
        current = current.substr(0, lastSlash);
        if (current.empty()) current = "/";
    }
    return info;
}
std::string PackageResolver::findPackageRoot(const std::string& filePath) {
    std::string current = ModuleResolver::getDirectory(filePath);
    while (!current.empty() && current != "/") {
        std::string packageJson = current + "/package.json";
        if (ModuleResolver::isFile(packageJson)) return current;
        size_t lastSlash = current.find_last_of('/');
        if (lastSlash == std::string::npos) break;
        current = current.substr(0, lastSlash);
        if (current.empty()) current = "/";
    }
    return "";
}
} // namespace protojs
