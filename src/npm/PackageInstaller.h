#ifndef PROTOJS_PACKAGEINSTALLER_H
#define PROTOJS_PACKAGEINSTALLER_H
#include <string>
namespace protojs {
class PackageInstaller {
public:
    static bool install(const std::string& packageJsonPath, bool production = false);
    static bool installPackage(const std::string& packageName, const std::string& version = "latest");
};
} // namespace protojs
#endif
