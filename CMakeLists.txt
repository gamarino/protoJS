# CMakeLists.txt for protoJS

cmake_minimum_required(VERSION 3.16)
project(protoJS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

include(GNUInstallDirs)

set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs)

# --- protoCore -----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.0 CONFIG).
#      The only mode that checks protoCore's version and ABI, and therefore the
#      only mode a distributable package may be built in. Name a prefix with
#      -DPROTO_CORE_PREFIX=<prefix> or -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: a sibling build directory, searched in the order
#      ../protoCore/build_release, ../protoCore/build, ../protoCore/build_check.
#      build_release comes first because it is the directory protoCore's own
#      release workflow writes to, so a developer who keeps a stale
#      ../protoCore/build around does not silently link protoJS against it.
#      protoJS does not build protoCore itself; it must already be built.
#      Refused by -DPROTOCORE_REQUIRE_PACKAGE=ON.
#
# The version floor is 2.0: protoJS uses no protoCore API newer than 2.0.0. The
# ceiling is the next major, because protoCore's major version and its soname
# move together.
set(PROTOCORE_MIN_VERSION      "2.0")
set(PROTOCORE_MIN_VERSION_FULL "2.0.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "2")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoJS is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    set(PROTOCORE_LIBRARY protoCore::protoCore)
    set(PROTOCORE_INCLUDE_DIRS "${protoCore_INCLUDE_DIR}")
    include_directories(${PROTOCORE_INCLUDE_DIRS})
    message(STATUS "protoJS: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
else()
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    if(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
        message(FATAL_ERROR
            "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was "
            "found${_protoCore_where}.\n"
            "  A prefix holding only libprotoCore and protoCore.h is no longer "
            "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there "
            "is no way to tell protoCore 1.x from 2.x. Install protoCore "
            "${PROTOCORE_MIN_VERSION_FULL} or later:\n"
            "    cmake -S ../protoCore -B ../protoCore/build_release "
            "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
            "    cmake --build ../protoCore/build_release --target protoCore\n"
            "    cmake --install ../protoCore/build_release --component protoCore")
    endif()
    unset(_protoCore_where)
    message(WARNING
        "protoJS: no installed protoCore package found; using the developer "
        "build in ../protoCore. This mode performs no package version check "
        "and must not be used to produce a distributable package (configure "
        "with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build_release ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH
    )
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR "protoCore shared library not found. Build protoCore first:\n  cd ${PROTOCORE_DIR} && cmake -B build_release -S . && cmake --build build_release --target protoCore")
    endif()
    get_filename_component(PROTOCORE_LIBRARY_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
    if(APPLE)
        set(_protoCore_abi_file "${PROTOCORE_LIBRARY_DIR}/libprotoCore.${PROTOCORE_ABI_SOVERSION}.dylib")
    else()
        set(_protoCore_abi_file "${PROTOCORE_LIBRARY_DIR}/libprotoCore.so.${PROTOCORE_ABI_SOVERSION}")
    endif()
    if(NOT EXISTS "${_protoCore_abi_file}")
        message(FATAL_ERROR
            "The protoCore build in ${PROTOCORE_LIBRARY_DIR} does not provide "
            "${_protoCore_abi_file}, so it is not SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Rebuild ../protoCore from clean.")
    endif()
    unset(_protoCore_abi_file)
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    include_directories(${PROTOCORE_INCLUDE_DIRS})
    message(STATUS "Found protoCore: ${PROTOCORE_LIBRARY}")
    # Build-tree RPATH so protojs finds libprotoCore without LD_LIBRARY_PATH/DYLD_LIBRARY_PATH
    set(CMAKE_BUILD_RPATH "${PROTOCORE_LIBRARY_DIR}")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()

# QuickJS Headers
include_directories(${QUICKJS_DIR})

# QuickJS Sources
set(QUICKJS_SRCS
    ${QUICKJS_DIR}/quickjs.c
    ${QUICKJS_DIR}/libregexp.c
    ${QUICKJS_DIR}/libunicode.c
    ${QUICKJS_DIR}/cutils.c
    ${QUICKJS_DIR}/dtoa.c
)

# QuickJS Compiler Flags
set_source_files_properties(${QUICKJS_SRCS} PROPERTIES COMPILE_FLAGS "-D_GNU_SOURCE -DCONFIG_VERSION=\\\"2024-01-13\\\"")

# Core static library: all shared sources (reused by both protojs and protojs_tests)
add_library(protojs_core STATIC
    src/TypeBridge.cpp
    src/JSContext.cpp
    src/GCBridge.cpp
    src/ExecutionEngine.cpp
    src/ErrorHandler.cpp
    src/logging/Logger.cpp
    src/monitoring/Metrics.cpp
    src/Deferred.cpp
    src/ProtoDeferred.cpp
    src/ProtoCoreNativeBindings.cpp
    src/native/NativeModuleABI.cpp
    src/console.cpp
    src/ProtoNativeModule.cpp
    src/ThreadPoolExecutor.cpp
    src/CPUThreadPool.cpp
    src/IOThreadPool.cpp
    src/EventLoop.cpp
    src/EventLoopBindings.cpp
    # Module system
    src/modules/ModuleResolver.cpp
    src/modules/ModuleCache.cpp
    src/modules/ESModuleLoader.cpp
    src/modules/CommonJSLoader.cpp
    src/modules/ModuleInterop.cpp
    src/modules/AsyncModuleLoader.cpp
    # Core modules
    src/modules/IOModule.cpp
    src/modules/ProcessModule.cpp
    src/modules/path/PathModule.cpp
    src/modules/fs/FSModule.cpp
    src/modules/url/URLModule.cpp
    src/modules/http/HTTPModule.cpp
    src/modules/events/EventsModule.cpp
    src/modules/stream/StreamModule.cpp
    src/modules/util/UtilModule.cpp
    src/modules/crypto/CryptoModule.cpp
    src/modules/buffer/BufferModule.cpp
    src/modules/net/NetModule.cpp
    src/modules/worker_threads/WorkerThreadsModule.cpp
    src/modules/cluster/ClusterModule.cpp
    src/modules/dgram/DgramModule.cpp
    src/modules/child_process/ChildProcessModule.cpp
    src/modules/dns/DNSModule.cpp
    src/memory/MemoryAnalyzer.cpp
    src/profiling/Profiler.cpp
    src/profiling/VisualProfiler.cpp
    src/debugging/IntegratedDebugger.cpp
    # protoCore-backed array adapter and QuickJS integration
    src/JSSymbols.cpp
    src/NumberPrototype.cpp
    src/StringPrototype.cpp
    src/BooleanPrototype.cpp
    src/DatePrototype.cpp
    src/BigIntPrototype.cpp
    src/PrototypeUtils.cpp
    src/MapPrototype.cpp
    src/SetPrototype.cpp
    src/RegExpPrototype.cpp
    src/RegExpStringIterator.cpp
    src/IteratorPrototype.cpp
    src/ProxyBuiltin.cpp
    src/ArrayPrototype.cpp
    src/ArrayBufferPrototype.cpp
    src/TypedArrayPrototype.cpp
    src/DataViewPrototype.cpp
    src/MathBuiltin.cpp
    src/JSONBuiltin.cpp
    src/ObjectPrototype.cpp
    src/FunctionPrototype.cpp
    src/PromisePrototype.cpp
    src/JSPrototypes.cpp
    src/ProtoArrayAdapter.cpp
    src/ProtoArgumentsAdapter.cpp
    src/QuickJSArrayBridge.cpp
    # ProtoCore runtime (compile-only + loader + interpreter)
    src/runtime/ProtoCompileOnly.cpp
    src/runtime/ProtoBytecodeLoader.cpp
    src/runtime/ProtoInterpreter.cpp
    src/runtime/BehaviorRegistry.cpp
    src/runtime/BytecodeSpecialiser.cpp
    # Native modules
    src/native/DynamicLibraryLoader.cpp
    src/native/NativeModuleWrapper.cpp
    # npm
    src/npm/PackageResolver.cpp
    src/npm/PackageInstaller.cpp
    src/npm/ScriptExecutor.cpp
    src/npm/JsonParser.cpp
    src/npm/NPMRegistry.cpp
    src/npm/Semver.cpp
    # Benchmarking
    src/benchmarking/BenchmarkRunner.cpp
    # Testing
    src/testing/NodeJSTestRunner.cpp
    # REPL
    src/repl/REPL.cpp
    ${QUICKJS_SRCS}
)

target_link_libraries(protojs_core
    PUBLIC
    ${PROTOCORE_LIBRARY}
    pthread
    dl
    m
    ssl
    crypto
)

# Main Executable (only main.cpp; everything else lives in protojs_core)
add_executable(protojs src/main.cpp)

# Link Libs (protoCore is the official name of the shared library)
# -rdynamic: export symbols so dlopen'd native addons can resolve QuickJS/protoCore symbols
target_link_libraries(protojs PRIVATE protojs_core)
target_link_options(protojs PRIVATE -rdynamic)

# Build settings for protoCore
# protoCore must be built as a shared library first (libprotoCore.so / .dylib / .dll).
# Build: cd ../protoCore && cmake -B build -S . && cmake --build build --target protoCore

# Native addon for tests (shared library; symbols resolved when loaded by protojs)
add_library(simple_addon SHARED tests/native_addons/simple/simple_addon.cpp)
# ABI v2 addons use protoCore objects only: no QuickJS headers are needed.
target_include_directories(simple_addon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PROTOCORE_INCLUDE_DIRS}
)
set_target_properties(simple_addon PROPERTIES
    PREFIX ""
    OUTPUT_NAME "simple"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/native_addons/simple
)
target_link_libraries(simple_addon PRIVATE dl)

# Fixture addon for resolution-order test (output next to fixture.js)
add_library(fixture_addon SHARED tests/native_addons/fixture/fixture_addon.cpp)
target_include_directories(fixture_addon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${PROTOCORE_INCLUDE_DIRS}
)
set_target_properties(fixture_addon PROPERTIES
    PREFIX ""
    OUTPUT_NAME "fixture"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/integration/native_addons
)
target_link_libraries(fixture_addon PRIVATE dl)

# --- Install RPATH: installed binary finds libprotoCore without LD_LIBRARY_PATH ---
if(UNIX)
    if(APPLE)
        set(_rpath_exe "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
    else()
        set(_rpath_exe "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    set_target_properties(protojs PROPERTIES INSTALL_RPATH "${_rpath_exe}")
endif()

# --- Install rules ---
install(TARGETS protojs
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT protoJS
)

# Testing Framework (Catch2)
option(BUILD_TESTING "Build the testing tree" ON)
if(BUILD_TESTING)
    include(CTest)
    # Try to find Catch2, if not found, download it
    find_package(Catch2 QUIET)
    if(NOT Catch2_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.5.2
        )
        FetchContent_MakeAvailable(Catch2)
    endif()
    add_subdirectory(tests)
endif()

# Build options
option(CMAKE_BUILD_TYPE "Build type" Release)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Coverage (optional)
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
if(ENABLE_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
if(ENABLE_COVERAGE)
    message(STATUS "Coverage: Enabled")
endif()

# --- Packaging (CPack) ---
# Native installers for the protojs runtime. The install(TARGETS protojs ...)
# rule above places the binary under <prefix>/bin; the install RPATH lets it
# find libprotoCore without LD_LIBRARY_PATH. protojs's standard modules (fs,
# path, http, ...) are compiled into the binary, so there is nothing else to
# install. The package declares a dependency on protoCore's package, which
# provides libprotoCore.
set(CPACK_PACKAGE_NAME "protojs")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "protoJS - a JavaScript runtime on the protoCore object model")
set(CPACK_PACKAGE_VENDOR "numaes")
set(CPACK_PACKAGE_CONTACT "gamarino@gmail.com")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_COMPONENTS_ALL protoJS)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "protoJS")

# Platform-specific generators.
#
# Note: this repository has two packaging paths. This CPack configuration
# produces a package named "protojs"; packaging/build_deb.sh produces one named
# "protoJS" from the templates under packaging/templates/. Both are supported
# (decision D-I4); dpkg treats them as unrelated packages, so they must not be
# installed at the same time -- both claim /usr/bin/protojs.
if(APPLE)
    # macOS drag-and-drop .dmg.
    # UNVERIFIED: configured and reviewed, never built - no macOS host (D-I6).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    # Enable only the generators whose tools are present: cpack aborts the whole
    # run when one of them is missing, which would take the TGZ down with it.
    # Same pattern as protoCore's own CPack block.
    set(CPACK_GENERATOR "TGZ")
    find_program(PROTOJS_DPKG_EXECUTABLE dpkg)
    find_program(PROTOJS_RPMBUILD_EXECUTABLE rpmbuild)
    if(PROTOJS_DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    else()
        message(STATUS "CPack: DEB generator disabled (dpkg not found)")
    endif()
    if(PROTOJS_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    else()
        message(STATUS "CPack: RPM generator disabled (rpmbuild not found)")
    endif()
    # protojs links libprotoCore — the package metadata must declare it.
    # protoCore's package is named "protocore" for DEB and "protoCore" for RPM,
    # both pinned in protoCore/CMakeLists.txt rather than inferred from each
    # generator's default casing. Its major version and its soname move
    # together, so the dependency is bounded on both sides.
    set(CPACK_DEBIAN_PACKAGE_NAME "protojs")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "protocore (>= ${PROTOCORE_MIN_VERSION_FULL}), protocore (<< ${PROTOCORE_NEXT_MAJOR})")
    set(CPACK_RPM_PACKAGE_NAME "protoJS")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # Windows NSIS installer + plain ZIP.
    # UNVERIFIED: configured and reviewed, never built - no Windows host (D-I6).
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_PACKAGE_NAME "protoJS")
endif()

include(CPack)
