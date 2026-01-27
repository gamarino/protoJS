#include "NPMRegistry.h"
#include "Semver.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <regex>
#include <cstring>

namespace protojs {

const std::string NPMRegistry::DEFAULT_REGISTRY = "https://registry.npmjs.org";

std::string NPMRegistry::httpGet(const std::string& url) {
    // Parse URL
    std::string protocol, host, path;
    size_t protocolEnd = url.find("://");
    if (protocolEnd == std::string::npos) return "";
    
    protocol = url.substr(0, protocolEnd);
    std::string rest = url.substr(protocolEnd + 3);
    
    size_t pathStart = rest.find('/');
    if (pathStart != std::string::npos) {
        host = rest.substr(0, pathStart);
        path = rest.substr(pathStart);
    } else {
        host = rest;
        path = "/";
    }
    
    // Resolve hostname
    struct hostent* hostEntry = gethostbyname(host.c_str());
    if (!hostEntry) return "";
    
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";
    
    // Connect
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(protocol == "https" ? 443 : 80);
    memcpy(&serverAddr.sin_addr, hostEntry->h_addr_list[0], hostEntry->h_length);
    
    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return "";
    }
    
    // Send HTTP GET request
    std::ostringstream request;
    request << "GET " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "User-Agent: protoJS/0.6.0\r\n";
    request << "Accept: application/json\r\n";
    request << "Connection: close\r\n";
    request << "\r\n";
    
    std::string requestStr = request.str();
    if (send(sock, requestStr.c_str(), requestStr.length(), 0) < 0) {
        close(sock);
        return "";
    }
    
    // Read response
    std::string response;
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        response += buffer;
    }
    
    close(sock);
    
    // Extract body (skip headers)
    size_t bodyStart = response.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        return response.substr(bodyStart + 4);
    }
    
    return response;
}

bool NPMRegistry::httpDownload(const std::string& url, const std::string& targetPath) {
    std::string data = httpGet(url);
    if (data.empty()) return false;
    
    std::ofstream file(targetPath, std::ios::binary);
    if (!file.is_open()) return false;
    
    file.write(data.c_str(), data.length());
    file.close();
    
    return true;
}

PackageMetadata NPMRegistry::fetchPackage(const std::string& packageName, const std::string& registry) {
    PackageMetadata metadata;
    metadata.name = packageName;
    
    std::string url = registry + "/" + packageName;
    std::string json = httpGet(url);
    
    if (json.empty()) {
        return metadata;
    }
    
    // Parse JSON (simplified - would use proper JSON parser in production)
    // Extract versions
    std::regex versionRegex("\"([0-9]+\\.[0-9]+\\.[0-9]+[^\"]*)\"\\s*:\\s*\\{");
    std::sregex_iterator iter(json.begin(), json.end(), versionRegex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        std::string version = iter->str(1);
        PackageVersion pv;
        pv.version = version;
        
        // Extract dist.tarball
        size_t versionStart = iter->position();
        size_t versionEnd = json.find("}", versionStart);
        if (versionEnd != std::string::npos) {
            std::string versionJson = json.substr(versionStart, versionEnd - versionStart);
            
            size_t tarballPos = versionJson.find("\"tarball\"");
            if (tarballPos != std::string::npos) {
                size_t urlStart = versionJson.find("\"", tarballPos + 9) + 1;
                size_t urlEnd = versionJson.find("\"", urlStart);
                if (urlEnd != std::string::npos) {
                    pv.dist_tarball = versionJson.substr(urlStart, urlEnd - urlStart);
                }
            }
            
            size_t mainPos = versionJson.find("\"main\"");
            if (mainPos != std::string::npos) {
                size_t mainStart = versionJson.find("\"", mainPos + 6) + 1;
                size_t mainEnd = versionJson.find("\"", mainStart);
                if (mainEnd != std::string::npos) {
                    pv.main = versionJson.substr(mainStart, mainEnd - mainStart);
                }
            }
        }
        
        metadata.versions[version] = pv;
    }
    
    // Extract latest version
    size_t latestPos = json.find("\"latest\"");
    if (latestPos != std::string::npos) {
        size_t latestStart = json.find("\"", latestPos + 8) + 1;
        size_t latestEnd = json.find("\"", latestStart);
        if (latestEnd != std::string::npos) {
            metadata.latest = json.substr(latestStart, latestEnd - latestStart);
            metadata.dist_tags_latest = metadata.latest;
        }
    }
    
    return metadata;
}

std::string NPMRegistry::resolveVersion(const std::string& packageName, const std::string& versionRange, const std::string& registry) {
    PackageMetadata metadata = fetchPackage(packageName, registry);
    
    if (metadata.versions.empty()) {
        return "";
    }
    
    // Collect all versions
    std::vector<std::string> versions;
    for (const auto& pair : metadata.versions) {
        versions.push_back(pair.first);
    }
    
    // Find highest version satisfying range
    std::string resolved = Semver::findHighest(versions, versionRange);
    
    if (resolved.empty() && versionRange == "latest") {
        resolved = metadata.latest;
    }
    
    return resolved;
}

bool NPMRegistry::downloadPackage(const std::string& packageName, const std::string& version, const std::string& targetDir, const std::string& registry) {
    PackageMetadata metadata = fetchPackage(packageName, registry);
    
    if (metadata.versions.find(version) == metadata.versions.end()) {
        return false;
    }
    
    PackageVersion pv = metadata.versions[version];
    if (pv.dist_tarball.empty()) {
        return false;
    }
    
    // Create target directory
    std::filesystem::create_directories(targetDir);
    
    // Download tarball
    std::string tarballPath = targetDir + "/" + packageName + "-" + version + ".tgz";
    if (!httpDownload(pv.dist_tarball, tarballPath)) {
        return false;
    }
    
    return true;
}

std::vector<std::string> NPMRegistry::searchPackages(const std::string& query, int limit, const std::string& registry) {
    std::vector<std::string> results;
    
    std::string url = registry + "/-/v1/search?text=" + query + "&size=" + std::to_string(limit);
    std::string json = httpGet(url);
    
    if (json.empty()) {
        return results;
    }
    
    // Parse search results (simplified)
    std::regex packageRegex("\"name\"\\s*:\\s*\"([^\"]+)\"");
    std::sregex_iterator iter(json.begin(), json.end(), packageRegex);
    std::sregex_iterator end;
    
    for (; iter != end && results.size() < static_cast<size_t>(limit); ++iter) {
        results.push_back(iter->str(1));
    }
    
    return results;
}

} // namespace protojs
