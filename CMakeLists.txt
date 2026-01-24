# CMakeLists.txt for protoJS

cmake_minimum_required(VERSION 3.16)
project(protoJS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Paths
set(PROTOCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protoCore)
set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs)

# ProtoCore Headers
include_directories(${PROTOCORE_DIR})
include_directories(${PROTOCORE_DIR}/headers)

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
    # Native modules
    src/native/DynamicLibraryLoader.cpp
    src/native/NativeModuleWrapper.cpp
    # npm
    src/npm/PackageResolver.cpp
    src/npm/PackageInstaller.cpp
    src/npm/ScriptExecutor.cpp
    ${QUICKJS_SRCS}
)

# Link Libs
target_link_libraries(protojs
    PRIVATE
    ${PROTOCORE_DIR}/libproto.a
    pthread
    dl
    m
    ssl
    crypto
)

# Build settings for ProtoCore
# Note: Assuming libproto.a is already built in the protoCore directory.
# If not, we might need to add_subdirectory(protoCore) if it supports it.

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
