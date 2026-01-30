// Phase 6: Unit tests for NPM registry client (structure and Semver integration)
#include <catch2/catch_all.hpp>
#include "../../src/npm/NPMRegistry.h"
#include "../../src/npm/Semver.h"
#include <vector>
#include <string>

using namespace protojs;

TEST_CASE("NPMRegistry::DEFAULT_REGISTRY", "[NPMRegistry][Phase6]") {
    REQUIRE_FALSE(NPMRegistry::DEFAULT_REGISTRY.empty());
    REQUIRE(NPMRegistry::DEFAULT_REGISTRY.find("npmjs") != std::string::npos);
}

TEST_CASE("NPMRegistry and Semver integration", "[NPMRegistry][Phase6]") {
    // Version resolution logic used by NPMRegistry::resolveVersion
    std::vector<std::string> versions = {"1.0.0", "1.1.0", "2.0.0", "1.2.3"};
    REQUIRE(Semver::findHighest(versions, "^1.0.0") == "1.2.3");
    REQUIRE(Semver::findHighest(versions, "2.0.0") == "2.0.0");
}

#if defined(PROTOJS_NPM_NETWORK_TESTS)
TEST_CASE("NPMRegistry::fetchPackage (requires network)", "[NPMRegistry][Phase6][.network]") {
    auto meta = NPMRegistry::fetchPackage("lodash");
    REQUIRE_FALSE(meta.name.empty());
    REQUIRE(meta.versions.size() > 0);
    REQUIRE_FALSE(meta.latest.empty());
}
#endif
