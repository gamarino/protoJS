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

# Proves the Test262 regression gate can go red. Needs no protojs binary and no
# corpus: it feeds the gate synthetic snapshots, which is what lets it assert the
# cases a real run cannot produce on demand -- notably a commit that repairs one
# test and breaks another, which leaves the pass rate unchanged.
add_test(NAME cli/test262-regression-gate-self-test
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/test262-regression-gate-self-test.sh"
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/regression-gate")

# Asserts that no native method cell is bound to the object that holds it, which
# would be a permanent reference cycle through a mutable. Needs the real binary,
# because the cells under test are created while the global object is built and
# that setup lives in src/main.cpp, outside protojs_core.
add_test(NAME cli/no-method-self-cycles
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/no-method-self-cycles.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/method-cycles")

# protoCore's finalizer contract, asserted against the source because three of the
# five finalizers cannot be reached at run time at all (each pins the object that
# carries its own ExternalPointer). Follows calls one whole call graph deep, which is
# what the original net finalizers needed: their join was in teardownServer, not in
# the finalizer body.
add_test(NAME cli/finalizers-do-not-block
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/cli/finalizers_do_not_block.py"
                    "${CMAKE_SOURCE_DIR}")

# End-to-end coverage of the reworked teardown paths under a heap ceiling: seven
# cases, each asserting the value its program computed and, where it can be
# established, that collection really ran while the program waited. Takes about 2 s.
add_test(NAME cli/blocking-joins-and-finalizers
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cli/blocking-joins-and-finalizers.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/joins")
