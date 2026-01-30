#ifndef PROTOJS_NPMREGISTRY_H
#define PROTOJS_NPMREGISTRY_H

#include "JsonParser.h"
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <chrono>

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

// Progress callback: (bytesReceived, totalBytes). totalBytes may be 0 if unknown.
using ProgressCallback = std::function<void(size_t, size_t)>;

class NPMRegistry {
public:
    static const std::string DEFAULT_REGISTRY;

    // Fetch package metadata from npm registry (uses cache when enabled).
    static PackageMetadata fetchPackage(const std::string& packageName, const std::string& registry = DEFAULT_REGISTRY);

    // Resolve version (supports semver ranges).
    static std::string resolveVersion(const std::string& packageName, const std::string& versionRange, const std::string& registry = DEFAULT_REGISTRY);

    // Download package tarball. Optional progress( bytesReceived, totalBytes ).
    static bool downloadPackage(const std::string& packageName, const std::string& version, const std::string& targetDir, const std::string& registry = DEFAULT_REGISTRY, ProgressCallback progress = nullptr);

    // Search packages.
    static std::vector<std::string> searchPackages(const std::string& query, int limit = 20, const std::string& registry = DEFAULT_REGISTRY);

    // Cache: set TTL in seconds (0 = disable cache). Default 300.
    static void setCacheTTL(std::chrono::seconds ttl);
    static void clearCache();

    // Progress: set global callback for downloads (used when downloadPackage progress is null).
    static void setProgressCallback(ProgressCallback cb);

private:
    static std::string httpGet(const std::string& url);
    static bool httpDownload(const std::string& url, const std::string& targetPath, ProgressCallback progress = nullptr);

    static PackageMetadata parsePackageMetadata(const std::string& json, const std::string& packageName);
    static PackageVersion parsePackageVersion(const JsonValue& v);
};

} // namespace protojs

#endif // PROTOJS_NPMREGISTRY_H
