#include "PackageInstaller.h"
#include "NPMRegistry.h"
#include "Semver.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <cstdlib>

namespace protojs {
namespace fs = std::filesystem;

bool PackageInstaller::install(const std::string& packageJsonPath, const InstallOptions& options) {
    if (!fs::exists(packageJsonPath)) {
        std::cerr << "Error: package.json not found at " << packageJsonPath << std::endl;
        return false;
    }
    
    std::map<std::string, std::string> dependencies = parseDependencies(packageJsonPath, options.production);
    
    if (dependencies.empty()) {
        std::cout << "No dependencies to install." << std::endl;
        return true;
    }
    
    std::cout << "Installing " << dependencies.size() << " package(s)..." << std::endl;
    
    return resolveDependencies(dependencies, options);
}

bool PackageInstaller::installPackage(const std::string& packageName, const std::string& version, const InstallOptions& options) {
    std::cout << "Installing " << packageName << "@" << version << "..." << std::endl;
    
    // Resolve version
    std::string resolvedVersion = NPMRegistry::resolveVersion(packageName, version, options.registry);
    if (resolvedVersion.empty()) {
        std::cerr << "Error: Could not resolve version " << version << " for package " << packageName << std::endl;
        return false;
    }
    
    // Create install directory
    std::string packageDir = options.installDir + "/" + packageName;
    fs::create_directories(packageDir);
    
    // Download package
    std::string tempDir = packageDir + "/.tmp";
    fs::create_directories(tempDir);
    
    if (!NPMRegistry::downloadPackage(packageName, resolvedVersion, tempDir, options.registry)) {
        std::cerr << "Error: Failed to download " << packageName << "@" << resolvedVersion << std::endl;
        fs::remove_all(tempDir);
        return false;
    }
    
    // Extract tarball
    std::string tarballPath = tempDir + "/" + packageName + "-" + resolvedVersion + ".tgz";
    if (!extractTarball(tarballPath, packageDir)) {
        std::cerr << "Error: Failed to extract " << packageName << std::endl;
        fs::remove_all(tempDir);
        return false;
    }
    
    // Cleanup
    fs::remove_all(tempDir);
    
    std::cout << "Successfully installed " << packageName << "@" << resolvedVersion << std::endl;
    
    return true;
}

bool PackageInstaller::installPackages(const std::map<std::string, std::string>& packages, const InstallOptions& options) {
    bool success = true;
    for (const auto& pair : packages) {
        if (!installPackage(pair.first, pair.second, options)) {
            success = false;
        }
    }
    return success;
}

bool PackageInstaller::uninstallPackage(const std::string& packageName, const std::string& installDir) {
    std::string packagePath = installDir + "/" + packageName;
    if (fs::exists(packagePath)) {
        fs::remove_all(packagePath);
        std::cout << "Uninstalled " << packageName << std::endl;
        return true;
    }
    return false;
}

bool PackageInstaller::updatePackage(const std::string& packageName, const std::string& versionRange, const InstallOptions& options) {
    // Uninstall old version
    uninstallPackage(packageName, options.installDir);
    
    // Install new version
    return installPackage(packageName, versionRange, options);
}

bool PackageInstaller::extractTarball(const std::string& tarballPath, const std::string& targetDir) {
    // Use tar command to extract (simplified - would use libarchive in production)
    std::string command = "tar -xzf \"" + tarballPath + "\" -C \"" + targetDir + "\" --strip-components=1 2>/dev/null";
    int result = std::system(command.c_str());
    return result == 0;
}

std::map<std::string, std::string> PackageInstaller::parseDependencies(const std::string& packageJsonPath, bool production) {
    std::map<std::string, std::string> dependencies;
    
    std::ifstream file(packageJsonPath);
    if (!file.is_open()) return dependencies;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Parse dependencies (simplified JSON parsing)
    std::string depKey = production ? "\"dependencies\"" : "\"dependencies\"";
    size_t depsStart = content.find(depKey);
    if (depsStart == std::string::npos && !production) {
        depsStart = content.find("\"devDependencies\"");
    }
    
    if (depsStart == std::string::npos) {
        return dependencies;
    }
    
    size_t depsEnd = content.find("}", depsStart);
    if (depsEnd == std::string::npos) {
        return dependencies;
    }
    
    std::string depsSection = content.substr(depsStart, depsEnd - depsStart);
    
    // Extract package names and versions
    std::regex depRegex("\"([^\"]+)\"\\s*:\\s*\"([^\"]+)\"");
    std::sregex_iterator iter(depsSection.begin(), depsSection.end(), depRegex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        dependencies[iter->str(1)] = iter->str(2);
    }
    
    return dependencies;
}

bool PackageInstaller::resolveDependencies(const std::map<std::string, std::string>& dependencies, const InstallOptions& options, int depth) {
    if (depth > 10) { // Prevent infinite recursion
        std::cerr << "Warning: Maximum dependency depth reached" << std::endl;
        return false;
    }
    
    bool success = true;
    for (const auto& pair : dependencies) {
        if (!installPackage(pair.first, pair.second, options)) {
            success = false;
        }
    }
    
    return success;
}

} // namespace protojs
