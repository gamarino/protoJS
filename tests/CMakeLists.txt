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

    # protojs_core provides all protojs sources (TypeBridge, JSContext, QuickJS, etc.)
    # and transitively pulls in protoCore, pthread, ssl, crypto, etc.
    target_link_libraries(protojs_tests
        PRIVATE
        Catch2::Catch2WithMain
        protojs_core
    )

    # Allow Phase 6 integration tests to find test scripts (set PROTOJS_TEST_PROJECT_ROOT)
    target_compile_definitions(protojs_tests PRIVATE
        PROTOJS_TEST_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    )

    # Add tests to CTest
    include(Catch)
    catch_discover_tests(protojs_tests)
endif()

# --- Command-line fixtures -----------------------------------------------------
#
# Shell cases registered directly with CTest, following protoClojure's
# tests/CMakeLists.txt: a plain add_test, the script path from
# CMAKE_CURRENT_SOURCE_DIR, and the target binary passed as $<TARGET_FILE:...>.
# These are the first add_test calls in protoJS; everything else reaches CTest
# through catch_discover_tests.
#
# Names must not match "integration" or "network": the gating ctest invocation is
# `ctest -E "integration|network"`, and a fixture excluded from the gate is not a
# gate.

# Asserts that no native method cell is bound to the object that holds it, which
# would be a permanent reference cycle through a mutable. Needs the real binary,
# because the cells under test are created while the global object is built and
# that setup lives in src/main.cpp, outside protojs_core.
add_test(NAME cli/no-method-self-cycles
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/no-method-self-cycles.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/method-cycles")
