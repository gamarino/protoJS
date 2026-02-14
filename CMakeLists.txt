# CMakeLists.txt for protoJS

cmake_minimum_required(VERSION 3.16)
project(protoJS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

include(GNUInstallDirs)

set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs)

# --- protoCore: use installed package or build from sibling directory ---
# For packaging: set PROTO_CORE_PREFIX to the install prefix of protoCore (e.g. /usr/local or $HOME/.local).
# For development: leave unset to find protoCore in ../protoCore/build or ../protoCore/build_check.
if(DEFINED PROTO_CORE_PREFIX)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES lib lib64)
    find_path(PROTO_CORE_INCLUDE_DIR NAMES protoCore.h
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES include)
    if(PROTOCORE_LIBRARY AND PROTO_CORE_INCLUDE_DIR)
        set(PROTOCORE_INCLUDE_DIRS "${PROTO_CORE_INCLUDE_DIR}")
        include_directories(${PROTO_CORE_INCLUDE_DIR})
        message(STATUS "Using installed protoCore: ${PROTOCORE_LIBRARY}")
    else()
        message(FATAL_ERROR "PROTO_CORE_PREFIX=${PROTO_CORE_PREFIX} set but protoCore not found. "
            "Need libprotoCore and include/protoCore.h under that prefix.")
    endif()
else()
    set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
    find_library(PROTOCORE_LIBRARY NAMES protoCore
        PATHS ${PROTOCORE_DIR}/build ${PROTOCORE_DIR}/build_check
        NO_DEFAULT_PATH
    )
    if(NOT PROTOCORE_LIBRARY)
        message(FATAL_ERROR "protoCore shared library not found. Build protoCore first:\n  cd ${PROTOCORE_DIR} && cmake -B build -S . && cmake --build build --target protoCore")
    endif()
    set(PROTOCORE_INCLUDE_DIRS ${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    include_directories(${PROTOCORE_DIR} ${PROTOCORE_DIR}/headers)
    message(STATUS "Found protoCore: ${PROTOCORE_LIBRARY}")
    # Build-tree RPATH so protojs finds libprotoCore without LD_LIBRARY_PATH/DYLD_LIBRARY_PATH
    get_filename_component(PROTOCORE_LIBRARY_DIR "${PROTOCORE_LIBRARY}" DIRECTORY)
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

# Main Executable
add_executable(protojs
    src/main.cpp
    src/TypeBridge.cpp
    src/JSContext.cpp
    src/GCBridge.cpp
    src/ExecutionEngine.cpp
    src/ErrorHandler.cpp
    src/logging/Logger.cpp
    src/monitoring/Metrics.cpp
    src/Deferred.cpp
    src/console.cpp
    src/ThreadPoolExecutor.cpp
    src/CPUThreadPool.cpp
    src/IOThreadPool.cpp
    src/EventLoop.cpp
    # Module system
    src/modules/ModuleResolver.cpp
    src/modules/ModuleCache.cpp
    src/modules/ESModuleLoader.cpp
    src/modules/CommonJSLoader.cpp
    src/modules/ModuleInterop.cpp
    src/modules/AsyncModuleLoader.cpp
    # Core modules
    src/modules/IOModule.cpp
    src/modules/ProtoCoreModule.cpp
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

# Link Libs (protoCore is the official name of the shared library)
# -rdynamic: export symbols so dlopen'd native addons can resolve QuickJS/protoCore symbols
target_link_libraries(protojs
    PRIVATE
    ${PROTOCORE_LIBRARY}
    pthread
    dl
    m
    ssl
    crypto
)
target_link_options(protojs PRIVATE -rdynamic)

# Build settings for protoCore
# protoCore must be built as a shared library first (libprotoCore.so / .dylib / .dll).
# Build: cd ../protoCore && cmake -B build -S . && cmake --build build --target protoCore

# Native addon for tests (shared library; symbols resolved when loaded by protojs)
add_library(simple_addon SHARED tests/native_addons/simple/simple_addon.cpp)
target_include_directories(simple_addon PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps/quickjs
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
    ${CMAKE_SOURCE_DIR}/deps/quickjs
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
