#ifndef PROTOJS_NPMREGISTRY_H
#define PROTOJS_NPMREGISTRY_H

#include <string>
#include <map>
#include <vector>

namespace protojs {

struct PackageVersion {
    std::string version;
    std::string dist_tarball;
    std::string dist_shasum;
    std::map<std::string, std::string> dependencies;
    std::string main;
    std::string module;
    std::string type;
};

struct PackageMetadata {
    std::string name;
    std::string description;
    std::map<std::string, PackageVersion> versions;
    std::string latest;
    std::string dist_tags_latest;
    std::map<std::string, std::string> dist_tags;
};

class NPMRegistry {
public:
    static const std::string DEFAULT_REGISTRY;
    
    // Fetch package metadata from npm registry
    static PackageMetadata fetchPackage(const std::string& packageName, const std::string& registry = DEFAULT_REGISTRY);
    
    // Resolve version (supports semver ranges)
    static std::string resolveVersion(const std::string& packageName, const std::string& versionRange, const std::string& registry = DEFAULT_REGISTRY);
    
    // Download package tarball
    static bool downloadPackage(const std::string& packageName, const std::string& version, const std::string& targetDir, const std::string& registry = DEFAULT_REGISTRY);
    
    // Search packages
    static std::vector<std::string> searchPackages(const std::string& query, int limit = 20, const std::string& registry = DEFAULT_REGISTRY);

private:
    // HTTP request helper
    static std::string httpGet(const std::string& url);
    static bool httpDownload(const std::string& url, const std::string& targetPath);
    
    // Parse JSON (simplified)
    static PackageMetadata parsePackageMetadata(const std::string& json);
    static PackageVersion parsePackageVersion(const std::string& json);
};

} // namespace protojs

#endif // PROTOJS_NPMREGISTRY_H
