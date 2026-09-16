#include <catch2/catch_all.hpp>

#include "../../src/native/DynamicLibraryLoader.h"

using protojs::DynamicLibraryLoader;
using protojs::ProtoJSNativeModuleInfo;

namespace {

int dummyInit(proto::ProtoContext*, const proto::ProtoObject*) { return 0; }

} // namespace

TEST_CASE("DynamicLibraryLoader::validateABI", "[native][abi]") {
    SECTION("accepts a module declaring the current ABI") {
        ProtoJSNativeModuleInfo info(PROTOJS_ABI_VERSION, "ok", "1.0.0",
                                     dummyInit, nullptr);
        REQUIRE(DynamicLibraryLoader::validateABI(&info));
    }

    SECTION("rejects an ABI v1 addon") {
        // v1 addons built their exports with the QuickJS C API; the protoCore
        // interpreter cannot call those, so they must be rebuilt.
        ProtoJSNativeModuleInfo info(1, "legacy", "1.0.0", dummyInit, nullptr);
        REQUIRE_FALSE(DynamicLibraryLoader::validateABI(&info));
    }

    SECTION("rejects a future ABI version") {
        ProtoJSNativeModuleInfo info(PROTOJS_ABI_VERSION + 1, "future", "1.0.0",
                                     dummyInit, nullptr);
        REQUIRE_FALSE(DynamicLibraryLoader::validateABI(&info));
    }

    SECTION("rejects a null info pointer") {
        REQUIRE_FALSE(DynamicLibraryLoader::validateABI(nullptr));
    }

    SECTION("rejects a module without a name") {
        ProtoJSNativeModuleInfo info(PROTOJS_ABI_VERSION, nullptr, "1.0.0",
                                     dummyInit, nullptr);
        REQUIRE_FALSE(DynamicLibraryLoader::validateABI(&info));
    }

    SECTION("rejects a module without an init function") {
        ProtoJSNativeModuleInfo info(PROTOJS_ABI_VERSION, "no-init", "1.0.0",
                                     nullptr, nullptr);
        REQUIRE_FALSE(DynamicLibraryLoader::validateABI(&info));
    }

    SECTION("a default-constructed descriptor is invalid") {
        ProtoJSNativeModuleInfo info;
        REQUIRE_FALSE(DynamicLibraryLoader::validateABI(&info));
    }
}
