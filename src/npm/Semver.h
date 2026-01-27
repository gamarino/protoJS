#ifndef PROTOJS_SEMVER_H
#define PROTOJS_SEMVER_H

#include <string>
#include <vector>

namespace protojs {

class Semver {
public:
    // Parse version string (e.g., "1.2.3", "1.2.3-alpha.1")
    static bool parse(const std::string& version, int& major, int& minor, int& patch, std::string& prerelease, std::string& build);
    
    // Compare two versions (-1: v1 < v2, 0: v1 == v2, 1: v1 > v2)
    static int compare(const std::string& v1, const std::string& v2);
    
    // Check if version satisfies range (simplified semver range matching)
    static bool satisfies(const std::string& version, const std::string& range);
    
    // Find highest version satisfying range
    static std::string findHighest(const std::vector<std::string>& versions, const std::string& range);
    
    // Normalize version string
    static std::string normalize(const std::string& version);

private:
    static int comparePrerelease(const std::string& p1, const std::string& p2);
    static bool parseRange(const std::string& range, std::string& op, std::string& version);
};

} // namespace protojs

#endif // PROTOJS_SEMVER_H
