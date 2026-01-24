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
        ${PROTOCORE_DIR}/libproto.a
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
    )

    # Include directories
    target_include_directories(protojs_tests
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PROTOCORE_DIR}
        ${PROTOCORE_DIR}/headers
    )

    # Add tests to CTest
    include(Catch)
    catch_discover_tests(protojs_tests)
endif()
