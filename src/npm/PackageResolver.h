#ifndef PROTOJS_PACKAGERESOLVER_H
#define PROTOJS_PACKAGERESOLVER_H
#include <string>
#include <map>
namespace protojs {
struct PackageInfo {
    std::string name, version, path, main, module, type;
    std::map<std::string, std::string> exports;
};
class PackageResolver {
public:
    static PackageInfo resolvePackage(const std::string& packageName, const std::string& fromPath);
    static std::string findPackageRoot(const std::string& filePath);
};
} // namespace protojs
#endif
