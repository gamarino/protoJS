#include "Semver.h"
#include <sstream>
#include <algorithm>
#include <regex>
#include <cstdlib>

namespace protojs {

bool Semver::parse(const std::string& version, int& major, int& minor, int& patch, std::string& prerelease, std::string& build) {
    // Reset
    major = minor = patch = 0;
    prerelease.clear();
    build.clear();
    
    // Remove leading 'v' if present
    std::string v = version;
    if (!v.empty() && v[0] == 'v') {
        v = v.substr(1);
    }
    
    // Find prerelease and build
    size_t prereleasePos = v.find('-');
    size_t buildPos = v.find('+');
    
    if (buildPos != std::string::npos) {
        build = v.substr(buildPos + 1);
        v = v.substr(0, buildPos);
    }
    
    if (prereleasePos != std::string::npos) {
        prerelease = v.substr(prereleasePos + 1);
        v = v.substr(0, prereleasePos);
    }
    
    // Parse version numbers
    std::istringstream iss(v);
    char dot1, dot2;
    
    if (!(iss >> major >> dot1 >> minor >> dot2 >> patch) || dot1 != '.' || dot2 != '.') {
        return false;
    }
    
    return true;
}

int Semver::compare(const std::string& v1, const std::string& v2) {
    int major1, minor1, patch1;
    int major2, minor2, patch2;
    std::string prerelease1, build1, prerelease2, build2;
    
    if (!parse(v1, major1, minor1, patch1, prerelease1, build1)) return 0;
    if (!parse(v2, major2, minor2, patch2, prerelease2, build2)) return 0;
    
    // Compare major
    if (major1 < major2) return -1;
    if (major1 > major2) return 1;
    
    // Compare minor
    if (minor1 < minor2) return -1;
    if (minor1 > minor2) return 1;
    
    // Compare patch
    if (patch1 < patch2) return -1;
    if (patch1 > patch2) return 1;
    
    // Compare prerelease
    if (prerelease1.empty() && !prerelease2.empty()) return 1;  // v1 is stable, v2 is prerelease
    if (!prerelease1.empty() && prerelease2.empty()) return -1; // v1 is prerelease, v2 is stable
    if (!prerelease1.empty() && !prerelease2.empty()) {
        int prereleaseCmp = comparePrerelease(prerelease1, prerelease2);
        if (prereleaseCmp != 0) return prereleaseCmp;
    }
    
    return 0;
}

int Semver::comparePrerelease(const std::string& p1, const std::string& p2) {
    // Simple string comparison for prerelease
    if (p1 < p2) return -1;
    if (p1 > p2) return 1;
    return 0;
}

bool Semver::satisfies(const std::string& version, const std::string& range) {
    if (range == "*" || range == "latest") {
        return true;
    }
    
    // Handle simple ranges: >=, <=, >, <, =, ~, ^
    std::string op, targetVersion;
    if (!parseRange(range, op, targetVersion)) {
        // If not a range, treat as exact match
        return compare(version, range) == 0;
    }
    
    int cmp = compare(version, targetVersion);
    
    if (op == "=" || op == "") {
        return cmp == 0;
    } else if (op == ">") {
        return cmp > 0;
    } else if (op == ">=") {
        return cmp >= 0;
    } else if (op == "<") {
        return cmp < 0;
    } else if (op == "<=") {
        return cmp <= 0;
    } else if (op == "~") {
        // Tilde range: ~1.2.3 means >=1.2.3 <1.3.0
        int major, minor, patch;
        std::string prerelease, build;
        if (!parse(targetVersion, major, minor, patch, prerelease, build)) return false;
        int vMajor, vMinor, vPatch;
        std::string vPrerelease, vBuild;
        if (!parse(version, vMajor, vMinor, vPatch, vPrerelease, vBuild)) return false;
        
        if (vMajor != major) return false;
        if (vMinor != minor) return false;  // ~1.2.x means <1.3.0, so same minor only
        return vPatch >= patch;
    } else if (op == "^") {
        // Caret range: ^1.2.3 means >=1.2.3 <2.0.0
        int major, minor, patch;
        std::string prerelease, build;
        if (!parse(targetVersion, major, minor, patch, prerelease, build)) return false;
        int vMajor, vMinor, vPatch;
        std::string vPrerelease, vBuild;
        if (!parse(version, vMajor, vMinor, vPatch, vPrerelease, vBuild)) return false;
        
        if (vMajor != major) return false;
        if (vMajor == 0) {
            if (vMinor != minor) return false;
            return vPatch >= patch;
        }
        return true; // vMajor == major, so it's in range
    }
    
    return false;
}

std::string Semver::findHighest(const std::vector<std::string>& versions, const std::string& range) {
    std::string highest;
    for (const auto& version : versions) {
        if (satisfies(version, range)) {
            if (highest.empty() || compare(version, highest) > 0) {
                highest = version;
            }
        }
    }
    return highest;
}

std::string Semver::normalize(const std::string& version) {
    int major, minor, patch;
    std::string prerelease, build;
    
    if (!parse(version, major, minor, patch, prerelease, build)) {
        return version; // Return as-is if can't parse
    }
    
    std::ostringstream oss;
    oss << major << "." << minor << "." << patch;
    if (!prerelease.empty()) {
        oss << "-" << prerelease;
    }
    if (!build.empty()) {
        oss << "+" << build;
    }
    return oss.str();
}

bool Semver::parseRange(const std::string& range, std::string& op, std::string& version) {
    // Remove whitespace
    std::string r = range;
    r.erase(std::remove_if(r.begin(), r.end(), ::isspace), r.end());
    
    if (r.empty()) {
        return false;
    }
    
    // Check for operators
    if (r.substr(0, 2) == ">=") {
        op = ">=";
        version = r.substr(2);
    } else if (r.substr(0, 2) == "<=") {
        op = "<=";
        version = r.substr(2);
    } else if (r[0] == '>') {
        op = ">";
        version = r.substr(1);
    } else if (r[0] == '<') {
        op = "<";
        version = r.substr(1);
    } else if (r[0] == '=') {
        op = "=";
        version = r.substr(1);
    } else if (r[0] == '~') {
        op = "~";
        version = r.substr(1);
    } else if (r[0] == '^') {
        op = "^";
        version = r.substr(1);
    } else {
        op = "";
        version = r;
    }
    
    return true;
}

} // namespace protojs
