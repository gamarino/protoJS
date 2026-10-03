#include <catch2/catch_all.hpp>
#include "../../src/HeapLimit.h"

#include <climits>

using namespace protojs::heaplimit;

TEST_CASE("Default heap limit is 75% of physical memory", "[HeapLimit]") {
    const unsigned long long GiB = 1ULL << 30;
    REQUIRE(defaultHeapLimitCells(16 * GiB, 0) == static_cast<long long>(16 * GiB * 3 / 4 / 64));
    REQUIRE(defaultHeapLimitCells(62 * GiB, 0) == static_cast<long long>(62 * GiB * 3 / 4 / 64));
}

TEST_CASE("A smaller cgroup limit wins over physical memory", "[HeapLimit]") {
    const unsigned long long GiB = 1ULL << 30;
    REQUIRE(defaultHeapLimitCells(16 * GiB, 4 * GiB) == static_cast<long long>(4 * GiB * 3 / 4 / 64));
    // A cgroup limit above physical memory does not raise the ceiling.
    REQUIRE(defaultHeapLimitCells(16 * GiB, 64 * GiB) == static_cast<long long>(16 * GiB * 3 / 4 / 64));
    // Unknown physical memory: the cgroup limit alone.
    REQUIRE(defaultHeapLimitCells(0, 2 * GiB) == static_cast<long long>(2 * GiB * 3 / 4 / 64));
}

TEST_CASE("Heap limit fallbacks and bounds", "[HeapLimit]") {
    REQUIRE(defaultHeapLimitCells(0, 0) == kFallbackCells);
    // protoCore's ceiling is an int: 2 PiB of RAM still fits.
    REQUIRE(defaultHeapLimitCells(1ULL << 51, 0) == INT_MAX);
}

TEST_CASE("The process default matches the policy for this machine", "[HeapLimit]") {
    const long long cells = processDefaultHeapLimitCells();
    REQUIRE(cells == defaultHeapLimitCells(physicalMemoryBytes(), cgroupMemoryLimitBytes()));
    REQUIRE(cells > 0);
}
