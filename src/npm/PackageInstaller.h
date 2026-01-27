#ifndef PROTOJS_PACKAGEINSTALLER_H
#define PROTOJS_PACKAGEINSTALLER_H
#include <string>
#include <map>
#include <vector>

namespace protojs {

struct InstallOptions {
    bool production = false;
    bool save = true;
    bool saveDev = false;
    std::string registry = "https://registry.npmjs.org";
    std::string installDir = "./node_modules";
};

class PackageInstaller {
public:
    // Install dependencies from package.json
    static bool install(const std::string& packageJsonPath, const InstallOptions& options = InstallOptions());
    
    // Install a single package
    static bool installPackage(const std::string& packageName, const std::string& version = "latest", const InstallOptions& options = InstallOptions());
    
    // Install multiple packages
    static bool installPackages(const std::map<std::string, std::string>& packages, const InstallOptions& options = InstallOptions());
    
    // Uninstall a package
    static bool uninstallPackage(const std::string& packageName, const std::string& installDir = "./node_modules");
    
    // Update a package
    static bool updatePackage(const std::string& packageName, const std::string& versionRange = "latest", const InstallOptions& options = InstallOptions());

private:
    // Extract tarball
    static bool extractTarball(const std::string& tarballPath, const std::string& targetDir);
    
    // Parse package.json
    static std::map<std::string, std::string> parseDependencies(const std::string& packageJsonPath, bool production);
    
    // Resolve dependencies recursively
    static bool resolveDependencies(const std::map<std::string, std::string>& dependencies, const InstallOptions& options, int depth = 0);
};

} // namespace protojs
#endif
