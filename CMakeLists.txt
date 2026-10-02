# CMakeLists.txt for protoJS

cmake_minimum_required(VERSION 3.16)
project(protoJS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

include(GNUInstallDirs)

# Compiler and platform differences, gathered here so the targets below read the
# same on every platform. Outside Windows these are the libraries and options the
# build always used.
if(MSVC)
    # Sources are UTF-8; the interpreter and QuickJS need more sections than the
    # default object format allows.
    add_compile_options(/utf-8 /bigobj)
    # M_PI & co. from <cmath>; the POSIX names of the C runtime without
    # deprecation warnings.
    add_compile_definitions(_USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()
if(WIN32)
    # Native stack reservation of protojs and its threads (see target protojs).
    set(PROTOJS_WINDOWS_STACK_BYTES 67108864)
    # <windows.h> without min/max macros and without the old Winsock 1 header.
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    # Threads, dynamic loading and libm come with the C/C++ runtime and the
    # Windows API; sockets are Winsock (ws2_32), process information is psapi,
    # the event loop's timer resolution is winmm (timeBeginPeriod), and OpenSSL
    # is found as a package (its DLLs are copied next to protojs).
    find_package(OpenSSL REQUIRED)
    set(PROTOJS_SYSTEM_LIBS OpenSSL::SSL OpenSSL::Crypto ws2_32 iphlpapi psapi winmm)
    # Windows has no rpath: a program finds its DLLs next to itself, so every
    # executable and DLL of the build goes to one directory.
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
elseif(APPLE)
    # Homebrew's OpenSSL is not on the default include or library path, so it
    # is found as a package (OPENSSL_ROOT_DIR=$(brew --prefix openssl@3)).
    find_package(OpenSSL REQUIRED)
    set(PROTOJS_SYSTEM_LIBS pthread dl m OpenSSL::SSL OpenSSL::Crypto)
else()
    set(PROTOJS_SYSTEM_LIBS pthread dl m ssl crypto)
endif()

set(QUICKJS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/quickjs)

# --- protoCore -----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.7 CONFIG).
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
# The version floor is 2.7: protoJS spells protoCore's 64-bit integers
# proto::proto_long / proto::proto_ulong (and PROTO_L / PROTO_UL / PROTO_FMT_U),
# which protoCore declares from 2.7.0, its first release that builds on Windows.
# On Linux and macOS those ARE long / unsigned long, so the ABI is the one 2.0
# already had; the floor is about the headers. src/ProtoCoreTypes.h refuses an
# older protoCore by name in the developer fallback, which checks no version.
# The ceiling is the next major, because protoCore's major version and its
# soname move together.
set(PROTOCORE_MIN_VERSION      "2.7")
set(PROTOCORE_MIN_VERSION_FULL "2.7.0")
set(PROTOCORE_NEXT_MAJOR       "3.0.0")
set(PROTOCORE_ABI_SOVERSION    "3")
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
    if(APPLE)
        # libprotoCore's install name is @rpath/...: name the package's library
        # directory in the build tree's rpath, or protojs and the tests built
        # against a package outside dyld's default paths cannot start.
        get_target_property(_protoCore_location protoCore::protoCore LOCATION)
        get_filename_component(_protoCore_libdir "${_protoCore_location}" DIRECTORY)
        list(APPEND CMAKE_BUILD_RPATH "${_protoCore_libdir}")
        unset(_protoCore_location)
        unset(_protoCore_libdir)
    endif()
else()
    if(WIN32)
        # The developer fallback finds a shared object by its soname, which a
        # Windows DLL does not have: on Windows protoJS builds against an
        # installed protoCore package only.
        message(FATAL_ERROR
            "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was found. "
            "On Windows install protoCore (cmake --install of its build) and pass "
            "its prefix: -DCMAKE_PREFIX_PATH=<prefix> or -DPROTO_CORE_PREFIX=<prefix>.")
    endif()
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
    src/ThreadProtoContext.cpp
    src/GcOrphanQueue.cpp
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
    src/modules/path/PathAlgorithms.cpp
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
    ${PROTOJS_SYSTEM_LIBS}
)

# MSVC warns at /W1 unless told otherwise (CMake no longer adds /W3 by default).
# protoJS's own sources build at /W3; the vendored QuickJS sources keep the
# default, since they are not clean at /W3 and are not protoJS's to restyle.
# PROTOJS_MSVC_WARNINGS is applied to protoJS's other targets below.
if(MSVC)
    set(PROTOJS_MSVC_WARNINGS /W3)
    get_target_property(_protojs_core_sources protojs_core SOURCES)
    list(REMOVE_ITEM _protojs_core_sources ${QUICKJS_SRCS})
    set_property(SOURCE ${_protojs_core_sources} APPEND PROPERTY COMPILE_OPTIONS ${PROTOJS_MSVC_WARNINGS})
    unset(_protojs_core_sources)
else()
    set(PROTOJS_MSVC_WARNINGS "")
endif()

if(WIN32)
    # The native-module ABI is exported by protojs.exe (PROTOJS_ABI_API).
    target_compile_definitions(protojs_core PUBLIC PROTOJS_BUILDING_RUNTIME)
    # Windows finds a DLL next to the program that loads it. Copy the DLLs
    # protojs needs -- protoCore and OpenSSL -- into the build's bin/, so protojs
    # and the tests run in place. A target protojs_core depends on, not a
    # post-build step: the unit tests are run to list their cases as soon as
    # they are linked.
    set(PROTOJS_RUNTIME_DLLS "")
    if(TARGET ${PROTOCORE_LIBRARY})
        list(APPEND PROTOJS_RUNTIME_DLLS "$<TARGET_FILE:${PROTOCORE_LIBRARY}>")
    endif()
    get_filename_component(PROTOJS_OPENSSL_BIN "${OPENSSL_INCLUDE_DIR}/../bin" ABSOLUTE)
    file(GLOB PROTOJS_OPENSSL_DLLS
        "${PROTOJS_OPENSSL_BIN}/libcrypto*.dll" "${PROTOJS_OPENSSL_BIN}/libssl*.dll")
    list(APPEND PROTOJS_RUNTIME_DLLS ${PROTOJS_OPENSSL_DLLS})
    add_custom_target(protojs_runtime_dlls
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROTOJS_RUNTIME_DLLS}
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        VERBATIM)
    add_dependencies(protojs_core protojs_runtime_dlls)
endif()

# Main Executable (only main.cpp; everything else lives in protojs_core)
add_executable(protojs src/main.cpp)
target_compile_options(protojs PRIVATE ${PROTOJS_MSVC_WARNINGS})

# Link Libs (protoCore is the official name of the shared library)
# -rdynamic: export symbols so dlopen'd native addons can resolve QuickJS/protoCore symbols
target_link_libraries(protojs PRIVATE protojs_core)
if(WIN32)
    # A DLL cannot see the symbols of the program that loads it. protojs exports
    # the native-module ABI (protojs_make_function & co., marked PROTOJS_ABI_API in
    # src/native/NativeModuleABI.h) and the build writes protojs.lib, which addons
    # link against together with protoCore.lib.
    set_target_properties(protojs PROPERTIES ENABLE_EXPORTS ON)
    # The interpreter recurses on the native stack: every JavaScript call is a
    # runBytecode frame. Linux gives the main thread 8 MiB; Windows reserves
    # 1 MiB unless the program asks for more, and threads created without an
    # explicit size get the same reservation. MSVC also gives runBytecode a
    # frame six times GCC's (about 46 KiB against 7.7 KiB: it does not share
    # stack slots between the handlers' block-scoped locals), so the same
    # recursion depth as on Linux takes about 48 MiB. The reservation is address
    # space; pages are committed only as the stack grows.
    target_link_options(protojs PRIVATE /STACK:${PROTOJS_WINDOWS_STACK_BYTES})
    # The threads that run JavaScript (workers, protoCore threads) get the same
    # reservation: src/main.cpp, configureThreadStacks.
    target_compile_definitions(protojs PRIVATE
        PROTOJS_MAIN_STACK_BYTES=${PROTOJS_WINDOWS_STACK_BYTES})
    # UTF-8 process code page: argv, getenv and paths are UTF-8 as elsewhere.
    target_sources(protojs PRIVATE src/windows/utf8.manifest)
    set(PROTOJS_ADDON_LIBS protojs ${PROTOCORE_LIBRARY})
else()
    target_link_options(protojs PRIVATE -rdynamic)
    set(PROTOJS_ADDON_LIBS dl)
endif()
# An addon's protoCore and protojs_* symbols are resolved from the protojs that
# loads it. Linux allows a shared library undefined symbols; macOS refuses them
# unless the link says they are looked up at load time.
if(APPLE)
    set(PROTOJS_ADDON_LINK_OPTIONS "LINKER:-undefined,dynamic_lookup")
else()
    set(PROTOJS_ADDON_LINK_OPTIONS "")
endif()

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
    # A DLL is a RUNTIME artifact on Windows.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/native_addons/simple
)
target_link_libraries(simple_addon PRIVATE ${PROTOJS_ADDON_LIBS})
target_compile_options(simple_addon PRIVATE ${PROTOJS_MSVC_WARNINGS})
target_link_options(simple_addon PRIVATE ${PROTOJS_ADDON_LINK_OPTIONS})

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
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests/integration/native_addons
)
target_link_libraries(fixture_addon PRIVATE ${PROTOJS_ADDON_LIBS})
target_compile_options(fixture_addon PRIVATE ${PROTOJS_MSVC_WARNINGS})
target_link_options(fixture_addon PRIVATE ${PROTOJS_ADDON_LINK_OPTIONS})

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
# On Windows protojs.lib, the import library native addons link against, is the
# executable's ARCHIVE artifact; an executable has none on Linux and macOS.
install(TARGETS protojs
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT protoJS
)
# OpenSSL is a DLL on Windows: install it next to protojs.exe, where Windows
# looks for it. (protoCore is a package of its own and installs its DLL into its
# own prefix's bin/.)
if(WIN32 AND PROTOJS_OPENSSL_DLLS)
    install(FILES ${PROTOJS_OPENSSL_DLLS}
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT protoJS)
endif()

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
    # SHLIBDEPS is meant to add a SONAME dependency (libprotoCore.so.N)
    # derived from the binary's own NEEDED entry, because the version range
    # above is not an ABI check on its own: a protoCore in range but built
    # with an older SOVERSION is accepted by dpkg and then fails at load
    # time (installers-sep2026 report, F4). Verified (in protoClojure, same
    # mechanism) that it does NOT add that dependency here: protoCore's own
    # .deb ships no shlibs/symbols file, so dpkg-shlibdeps has nothing to
    # emit for libprotoCore.so.3 and CPack's default --ignore-missing-info
    # drops it silently. Left on because it is harmless and adds real
    # transitive deps (libc6, libstdc++6, ...); closing F4 needs a shlibs or
    # symbols file in protoCore's own package, not this flag alone. (This is
    # the CPack DEB path; protoJS's separate packaging/ pipeline is fixed
    # under D5.)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_RPM_PACKAGE_NAME "protoJS")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # Plain ZIP, plus an NSIS installer when makensis is present: cpack aborts
    # the whole run when one generator's tool is missing.
    # The ZIP is verified on Windows 11 (protojs.exe, protojs.lib and the
    # OpenSSL DLLs); the NSIS installer is not (no NSIS on that host). Neither
    # carries protoCore.dll: protoCore is a package of its own, and its bin/
    # must be on PATH, as a shared library's directory is on Linux.
    set(CPACK_GENERATOR "ZIP")
    find_program(PROTOJS_MAKENSIS_EXECUTABLE makensis)
    if(PROTOJS_MAKENSIS_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "NSIS")
        message(STATUS "CPack: NSIS generator enabled (makensis found)")
    else()
        message(STATUS "CPack: NSIS generator disabled (makensis not found)")
    endif()
    set(CPACK_NSIS_PACKAGE_NAME "protoJS")
endif()

include(CPack)
