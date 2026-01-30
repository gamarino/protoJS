# CMakeLists.txt for tests

# Collect test files (only include if they exist)
file(GLOB TEST_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*/*.cpp"
)

if(TEST_SOURCES)
    # Test executable for unit tests
    add_executable(protojs_tests ${TEST_SOURCES})

    target_link_libraries(protojs_tests
        PRIVATE
        Catch2::Catch2WithMain
        ${PROTOCORE_LIBRARY}
        pthread
        dl
        m
    )
    
    # Add source files for linking
    target_sources(protojs_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ThreadPoolExecutor.cpp
        ${CMAKE_SOURCE_DIR}/src/CPUThreadPool.cpp
        ${CMAKE_SOURCE_DIR}/src/IOThreadPool.cpp
        ${CMAKE_SOURCE_DIR}/src/EventLoop.cpp
        # Phase 6: npm, benchmarking, Node.js test compatibility
        ${CMAKE_SOURCE_DIR}/src/npm/Semver.cpp
        ${CMAKE_SOURCE_DIR}/src/npm/NPMRegistry.cpp
        ${CMAKE_SOURCE_DIR}/src/benchmarking/BenchmarkRunner.cpp
        ${CMAKE_SOURCE_DIR}/src/testing/NodeJSTestRunner.cpp
    )

    # Include directories
    target_include_directories(protojs_tests
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PROTOCORE_DIR}
        ${PROTOCORE_DIR}/headers
    )

    # Allow Phase 6 integration tests to find test scripts (set PROTOJS_TEST_PROJECT_ROOT)
    target_compile_definitions(protojs_tests PRIVATE
        PROTOJS_TEST_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    )

    # Add tests to CTest
    include(Catch)
    catch_discover_tests(protojs_tests)
endif()
