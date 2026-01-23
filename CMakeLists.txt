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
    ${QUICKJS_DIR}/libbf.c
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
    ${QUICKJS_SRCS}
)

# Link Libs
target_link_libraries(protojs
    PRIVATE
    ${PROTOCORE_DIR}/libproto.a
    pthread
    dl
    m
)

# Build settings for ProtoCore
# Note: Assuming libproto.a is already built in the protoCore directory.
# If not, we might need to add_subdirectory(protoCore) if it supports it.
