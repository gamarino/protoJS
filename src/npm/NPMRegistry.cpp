#include "NPMRegistry.h"
#include "Semver.h"
#include "JsonParser.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <mutex>

#ifdef __linux__
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace protojs {

const std::string NPMRegistry::DEFAULT_REGISTRY = "https://registry.npmjs.org";

namespace {
    std::mutex g_cacheMutex;
    std::map<std::string, std::pair<PackageMetadata, std::chrono::steady_clock::time_point>> g_cache;
    std::chrono::seconds g_cacheTTL(300);
    ProgressCallback g_progressCallback;

    void parseUrl(const std::string& url, std::string& protocol, std::string& host, std::string& path) {
        size_t protocolEnd = url.find("://");
        if (protocolEnd == std::string::npos) { protocol.clear(); host.clear(); path = "/"; return; }
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
    }

#ifdef __linux__
    struct TlsConn {
        int sock = -1;
        SSL* ssl = nullptr;
        SSL_CTX* ctx = nullptr;
        ~TlsConn() {
            if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
            if (ctx) SSL_CTX_free(ctx);
            if (sock >= 0) close(sock);
        }
    };

    bool tlsConnect(const std::string& host, int port, TlsConn& out) {
        struct hostent* he = gethostbyname(host.c_str());
        if (!he) return false;
        out.sock = socket(AF_INET, SOCK_STREAM, 0);
        if (out.sock < 0) return false;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        memcpy(&addr.sin_addr, he->h_addr_list[0], static_cast<size_t>(he->h_length));
        if (connect(out.sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(out.sock);
            out.sock = -1;
            return false;
        }
        out.ctx = SSL_CTX_new(TLS_client_method());
        if (!out.ctx) return false;
        out.ssl = SSL_new(out.ctx);
        if (!out.ssl) return false;
        SSL_set_fd(out.ssl, out.sock);
        if (SSL_connect(out.ssl) != 1) return false;
        return true;
    }

    ssize_t tlsRead(SSL* ssl, void* buf, size_t len) {
        return static_cast<ssize_t>(SSL_read(ssl, buf, static_cast<int>(len)));
    }
    ssize_t tlsWrite(SSL* ssl, const void* buf, size_t len) {
        return static_cast<ssize_t>(SSL_write(ssl, buf, static_cast<int>(len)));
    }
#endif

    std::string tcpGet(const std::string& host, int port, const std::string& path) {
        struct hostent* he = gethostbyname(host.c_str());
        if (!he) return "";
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return "";
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        memcpy(&addr.sin_addr, he->h_addr_list[0], static_cast<size_t>(he->h_length));
        if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(sock);
            return "";
        }
        std::ostringstream req;
        req << "GET " << path << " HTTP/1.1\r\nHost: " << host << "\r\nUser-Agent: protoJS/0.6.0\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
        std::string reqStr = req.str();
        if (send(sock, reqStr.c_str(), reqStr.size(), 0) < 0) { close(sock); return ""; }
        std::string response;
        char buf[4096];
        ssize_t n;
        while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
            buf[n] = '\0';
            response += buf;
        }
        close(sock);
        size_t bodyStart = response.find("\r\n\r\n");
        if (bodyStart != std::string::npos) return response.substr(bodyStart + 4);
        return response;
    }

    size_t parseContentLength(const std::string& headers) {
        size_t pos = headers.find("Content-Length:");
        if (pos == std::string::npos) return 0;
        pos += 14;
        while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) pos++;
        size_t end = pos;
        while (end < headers.size() && headers[end] >= '0' && headers[end] <= '9') end++;
        if (end == pos) return 0;
        try { return static_cast<size_t>(std::stoull(headers.substr(pos, end - pos))); } catch (...) { return 0; }
    }
} // anonymous namespace

void NPMRegistry::setCacheTTL(std::chrono::seconds ttl) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_cacheTTL = ttl;
}

void NPMRegistry::clearCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_cache.clear();
}

void NPMRegistry::setProgressCallback(ProgressCallback cb) {
    g_progressCallback = std::move(cb);
}

std::string NPMRegistry::httpGet(const std::string& url) {
    std::string protocol, host, path;
    parseUrl(url, protocol, host, path);
    if (host.empty()) return "";

    int port = (protocol == "https") ? 443 : 80;

#ifdef __linux__
    if (protocol == "https") {
        static bool openssl_init = false;
        if (!openssl_init) { OPENSSL_init_ssl(0, nullptr); openssl_init = true; }
        TlsConn conn;
        if (!tlsConnect(host, port, conn)) return "";
        std::ostringstream req;
        req << "GET " << path << " HTTP/1.1\r\nHost: " << host << "\r\nUser-Agent: protoJS/0.6.0\r\nAccept: application/json\r\nConnection: close\r\n\r\n";
        std::string reqStr = req.str();
        if (tlsWrite(conn.ssl, reqStr.c_str(), reqStr.size()) <= 0) return "";
        std::string response;
        char buf[4096];
        ssize_t n;
        while ((n = tlsRead(conn.ssl, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            response += buf;
        }
        size_t bodyStart = response.find("\r\n\r\n");
        if (bodyStart != std::string::npos) return response.substr(bodyStart + 4);
        return response;
    }
#endif

    return tcpGet(host, port, path);
}

bool NPMRegistry::httpDownload(const std::string& url, const std::string& targetPath, ProgressCallback progress) {
    std::string protocol, host, path;
    parseUrl(url, protocol, host, path);
    if (host.empty()) return false;

    int port = (protocol == "https") ? 443 : 80;
    std::ostringstream req;
    req << "GET " << path << " HTTP/1.1\r\nHost: " << host << "\r\nUser-Agent: protoJS/0.6.0\r\nAccept: application/octet-stream\r\nConnection: close\r\n\r\n";
    std::string reqStr = req.str();

    if (!progress) {
        std::string data = httpGet(url);
        if (data.empty()) return false;
        std::ofstream f(targetPath, std::ios::binary);
        if (!f) return false;
        f.write(data.c_str(), data.size());
        return true;
    }

#ifdef __linux__
    if (protocol == "https") {
        TlsConn conn;
        if (!tlsConnect(host, port, conn)) return false;
        if (tlsWrite(conn.ssl, reqStr.c_str(), reqStr.size()) <= 0) return false;
        std::string headers;
        char buf[4096];
        ssize_t n;
        while ((n = tlsRead(conn.ssl, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            headers += buf;
            size_t sep = headers.find("\r\n\r\n");
            if (sep != std::string::npos) {
                size_t bodyStart = sep + 4;
                size_t contentLength = parseContentLength(headers.substr(0, sep));
                std::ofstream f(targetPath, std::ios::binary);
                if (!f) return false;
                std::string body = headers.substr(bodyStart);
                headers.clear();
                size_t totalReceived = body.size();
                f.write(body.c_str(), body.size());
                progress(totalReceived, contentLength > 0 ? contentLength : 0);
                while (contentLength == 0 || totalReceived < contentLength) {
                    n = tlsRead(conn.ssl, buf, sizeof(buf));
                    if (n <= 0) break;
                    f.write(buf, n);
                    totalReceived += static_cast<size_t>(n);
                    progress(totalReceived, contentLength > 0 ? contentLength : 0);
                }
                return true;
            }
        }
        return false;
    }
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    struct hostent* he = gethostbyname(host.c_str());
    if (!he) { close(sock); return false; }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    memcpy(&addr.sin_addr, he->h_addr_list[0], static_cast<size_t>(he->h_length));
    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) { close(sock); return false; }
    if (send(sock, reqStr.c_str(), reqStr.size(), 0) < 0) { close(sock); return false; }
    std::string headers;
    char buf[4096];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[n] = '\0';
        headers += buf;
        size_t sep = headers.find("\r\n\r\n");
        if (sep != std::string::npos) {
            size_t bodyStart = sep + 4;
            size_t contentLength = parseContentLength(headers.substr(0, sep));
            std::ofstream f(targetPath, std::ios::binary);
            if (!f) { close(sock); return false; }
            std::string body = headers.substr(bodyStart);
            size_t totalReceived = body.size();
            f.write(body.c_str(), body.size());
            progress(totalReceived, contentLength > 0 ? contentLength : 0);
            while (contentLength == 0 || totalReceived < contentLength) {
                n = recv(sock, buf, sizeof(buf), 0);
                if (n <= 0) break;
                f.write(buf, n);
                totalReceived += static_cast<size_t>(n);
                progress(totalReceived, contentLength > 0 ? contentLength : 0);
            }
            close(sock);
            return true;
        }
    }
    close(sock);
    return false;
}

PackageVersion NPMRegistry::parsePackageVersion(const JsonValue& v) {
    PackageVersion pv;
    pv.version = v.getString("version");
    const JsonValue* dist = v.get("dist");
    if (dist) {
        pv.dist_tarball = dist->getString("tarball");
        pv.dist_shasum = dist->getString("shasum");
    }
    pv.main = v.getString("main");
    pv.module = v.getString("module");
    pv.type = v.getString("type");
    const JsonValue* deps = v.get("dependencies");
    if (deps && deps->isObject()) {
        for (const auto& kv : deps->asObject())
            pv.dependencies[kv.first] = kv.second.isString() ? kv.second.asString() : "";
    }
    return pv;
}

PackageMetadata NPMRegistry::parsePackageMetadata(const std::string& json, const std::string& packageName) {
    PackageMetadata metadata;
    metadata.name = packageName;
    JsonValue root = JsonParse(json);
    if (root.isNull()) return metadata;
    metadata.name = root.getString("name");
    if (metadata.name.empty()) metadata.name = packageName;
    metadata.description = root.getString("description");
    const JsonValue* versions = root.get("versions");
    if (versions && versions->isObject()) {
        for (const auto& kv : versions->asObject())
            metadata.versions[kv.first] = parsePackageVersion(kv.second);
    }
    const JsonValue* distTags = root.get("dist-tags");
    if (distTags && distTags->isObject()) {
        metadata.latest = distTags->getString("latest");
        metadata.dist_tags_latest = metadata.latest;
        for (const auto& kv : distTags->asObject())
            if (kv.second.isString()) metadata.dist_tags[kv.first] = kv.second.asString();
    }
    return metadata;
}

PackageMetadata NPMRegistry::fetchPackage(const std::string& packageName, const std::string& registry) {
    std::string key = registry + "/" + packageName;
    if (g_cacheTTL.count() > 0) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto it = g_cache.find(key);
        if (it != g_cache.end()) {
            auto elapsed = std::chrono::steady_clock::now() - it->second.second;
            if (elapsed <= g_cacheTTL) return it->second.first;
        }
    }

    std::string url = registry + "/" + packageName;
    std::string json = httpGet(url);
    if (json.empty()) return PackageMetadata();

    PackageMetadata metadata = parsePackageMetadata(json, packageName);
    if (g_cacheTTL.count() > 0) {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_cache[key] = { metadata, std::chrono::steady_clock::now() };
    }
    return metadata;
}

std::string NPMRegistry::resolveVersion(const std::string& packageName, const std::string& versionRange, const std::string& registry) {
    PackageMetadata metadata = fetchPackage(packageName, registry);
    if (metadata.versions.empty()) return "";
    std::vector<std::string> versions;
    for (const auto& p : metadata.versions) versions.push_back(p.first);
    std::string resolved = Semver::findHighest(versions, versionRange);
    if (resolved.empty() && versionRange == "latest") resolved = metadata.latest;
    return resolved;
}

bool NPMRegistry::downloadPackage(const std::string& packageName, const std::string& version, const std::string& targetDir, const std::string& registry, ProgressCallback progress) {
    PackageMetadata metadata = fetchPackage(packageName, registry);
    if (metadata.versions.find(version) == metadata.versions.end()) return false;
    PackageVersion pv = metadata.versions[version];
    if (pv.dist_tarball.empty()) return false;
    std::filesystem::create_directories(targetDir);
    std::string tarballPath = targetDir + "/" + packageName + "-" + version + ".tgz";
    ProgressCallback cb = progress ? progress : g_progressCallback;
    if (!httpDownload(pv.dist_tarball, tarballPath, cb)) return false;
    return true;
}

std::vector<std::string> NPMRegistry::searchPackages(const std::string& query, int limit, const std::string& registry) {
    std::vector<std::string> results;
    std::string url = registry + "/-/v1/search?text=" + query + "&size=" + std::to_string(limit);
    std::string json = httpGet(url);
    if (json.empty()) return results;
    JsonValue root = JsonParse(json);
    if (root.isNull()) return results;
    const JsonValue* objects = root.get("objects");
    if (objects && objects->isArray()) {
        for (const auto& obj : objects->asArray()) {
            if (!obj.isObject()) continue;
            const JsonValue* pkg = obj.get("package");
            if (pkg && pkg->isObject()) {
                std::string name = pkg->getString("name");
                if (!name.empty()) results.push_back(name);
            }
            if (results.size() >= static_cast<size_t>(limit)) break;
        }
    }
    return results;
}

} // namespace protojs
