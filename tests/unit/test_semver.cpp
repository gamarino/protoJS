// Phase 6: Comprehensive unit tests for Semver (npm version resolution)
#include <catch2/catch_all.hpp>
#include "../../src/npm/Semver.h"
#include <vector>
#include <string>

using namespace protojs;

TEST_CASE("Semver::parse", "[Semver][Phase6]") {
    int major, minor, patch;
    std::string prerelease, build;

    SECTION("valid full version") {
        REQUIRE(Semver::parse("1.2.3", major, minor, patch, prerelease, build));
        REQUIRE(major == 1);
        REQUIRE(minor == 2);
        REQUIRE(patch == 3);
        REQUIRE(prerelease.empty());
        REQUIRE(build.empty());
    }

    SECTION("with leading v") {
        REQUIRE(Semver::parse("v2.0.1", major, minor, patch, prerelease, build));
        REQUIRE(major == 2);
        REQUIRE(minor == 0);
        REQUIRE(patch == 1);
    }

    SECTION("with prerelease") {
        REQUIRE(Semver::parse("1.0.0-alpha.1", major, minor, patch, prerelease, build));
        REQUIRE(major == 1);
        REQUIRE(minor == 0);
        REQUIRE(patch == 0);
        REQUIRE(prerelease == "alpha.1");
    }

    SECTION("with build") {
        REQUIRE(Semver::parse("1.0.0+build.42", major, minor, patch, prerelease, build));
        REQUIRE(major == 1);
        REQUIRE(patch == 0);
        REQUIRE(build == "build.42");
    }

    SECTION("invalid version") {
        REQUIRE_FALSE(Semver::parse("not-a-version", major, minor, patch, prerelease, build));
        REQUIRE_FALSE(Semver::parse("1.2", major, minor, patch, prerelease, build));
    }
}

TEST_CASE("Semver::compare", "[Semver][Phase6]") {
    REQUIRE(Semver::compare("1.0.0", "2.0.0") == -1);
    REQUIRE(Semver::compare("2.0.0", "1.0.0") == 1);
    REQUIRE(Semver::compare("1.2.3", "1.2.3") == 0);
    REQUIRE(Semver::compare("1.2.4", "1.2.3") == 1);
    REQUIRE(Semver::compare("1.2.3", "1.2.4") == -1);
    REQUIRE(Semver::compare("2.0.0", "1.9.9") == 1);
    REQUIRE(Semver::compare("1.0.0-alpha", "1.0.0") == -1);
    REQUIRE(Semver::compare("1.0.0", "1.0.0-alpha") == 1);
}

TEST_CASE("Semver::satisfies", "[Semver][Phase6]") {
    SECTION("exact match") {
        REQUIRE(Semver::satisfies("1.2.3", "1.2.3"));
        REQUIRE(Semver::satisfies("1.2.3", "=1.2.3"));
        REQUIRE_FALSE(Semver::satisfies("1.2.4", "1.2.3"));
    }

    SECTION("wildcard and latest") {
        REQUIRE(Semver::satisfies("1.2.3", "*"));
        REQUIRE(Semver::satisfies("2.0.0", "latest"));
    }

    SECTION("comparison operators") {
        REQUIRE(Semver::satisfies("2.0.0", ">=1.0.0"));
        REQUIRE(Semver::satisfies("1.0.0", ">=1.0.0"));
        REQUIRE_FALSE(Semver::satisfies("0.9.0", ">=1.0.0"));
        REQUIRE(Semver::satisfies("1.0.0", "<=2.0.0"));
        REQUIRE(Semver::satisfies("1.0.0", ">0.9.0"));
        REQUIRE(Semver::satisfies("1.0.0", "<2.0.0"));
    }

    SECTION("tilde range") {
        REQUIRE(Semver::satisfies("1.2.3", "~1.2.0"));
        REQUIRE(Semver::satisfies("1.2.9", "~1.2.0"));
        REQUIRE_FALSE(Semver::satisfies("1.3.0", "~1.2.0"));
    }

    SECTION("caret range") {
        REQUIRE(Semver::satisfies("1.2.3", "^1.0.0"));
        REQUIRE(Semver::satisfies("1.9.9", "^1.0.0"));
        REQUIRE_FALSE(Semver::satisfies("2.0.0", "^1.0.0"));
    }
}

TEST_CASE("Semver::findHighest", "[Semver][Phase6]") {
    std::vector<std::string> versions = {"1.0.0", "1.2.0", "1.2.3", "2.0.0", "1.2.4"};
    REQUIRE(Semver::findHighest(versions, "*") == "2.0.0");
    REQUIRE(Semver::findHighest(versions, "^1.0.0") == "1.2.4");
    REQUIRE(Semver::findHighest(versions, "~1.2.0") == "1.2.4");
    REQUIRE(Semver::findHighest(versions, ">=1.0.0") == "2.0.0");
    REQUIRE(Semver::findHighest(versions, "1.2.3") == "1.2.3");
    REQUIRE(Semver::findHighest(versions, "3.0.0").empty());
}

TEST_CASE("Semver::normalize", "[Semver][Phase6]") {
    REQUIRE(Semver::normalize("v1.2.3") == "1.2.3");
    REQUIRE(Semver::normalize("1.2.3-alpha.1") == "1.2.3-alpha.1");
    REQUIRE(Semver::normalize("1.2.3+build") == "1.2.3+build");
    REQUIRE(Semver::normalize("invalid") == "invalid");
}
