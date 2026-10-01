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

    if(WIN32)
        # As protojs itself: UTF-8 process code page and the same stack.
        target_sources(protojs_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/windows/utf8.manifest)
        target_link_options(protojs_tests PRIVATE /STACK:${PROTOJS_WINDOWS_STACK_BYTES})
    endif()

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

# The fixtures are bash scripts. Windows cannot run a script directly, so there
# they run through the bash of Git for Windows (not System32's bash.exe, which is
# WSL); elsewhere the prefix is empty and the script is the command. The Python
# fixture uses the interpreter CMake finds, which is python3 outside Windows.
if(WIN32)
    find_program(PROTOJS_BASH bash HINTS "C:/Program Files/Git/bin" "C:/Program Files/Git/usr/bin"
                 NO_DEFAULT_PATH)
    if(NOT PROTOJS_BASH)
        find_program(PROTOJS_BASH bash)
    endif()
    set(PROTOJS_SH "${PROTOJS_BASH}")
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        set(PROTOJS_PYTHON "${Python3_EXECUTABLE}")
    else()
        set(PROTOJS_PYTHON python)
    endif()
else()
    set(PROTOJS_SH "")
    set(PROTOJS_PYTHON python3)
endif()

# Proves the Test262 regression gate can go red. Needs no protojs binary and no
# corpus: it feeds the gate synthetic snapshots, which is what lets it assert the
# cases a real run cannot produce on demand -- notably a commit that repairs one
# test and breaks another, which leaves the pass rate unchanged.
add_test(NAME cli/test262-regression-gate-self-test
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/test262-regression-gate-self-test.sh"
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/regression-gate")
if(WIN32)
    # "python3" is the Microsoft Store stub on Windows; name the interpreter.
    set_tests_properties(cli/test262-regression-gate-self-test PROPERTIES
        ENVIRONMENT "PROTOJS_PYTHON=${PROTOJS_PYTHON}")
endif()

# Asserts that no native method cell is bound to the object that holds it, which
# would be a permanent reference cycle through a mutable. Needs the real binary,
# because the cells under test are created while the global object is built and
# that setup lives in src/main.cpp, outside protojs_core.
add_test(NAME cli/no-method-self-cycles
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/no-method-self-cycles.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/method-cycles")

# protoCore's finalizer contract, asserted against the source because three of the
# five finalizers cannot be reached at run time at all (each pins the object that
# carries its own ExternalPointer). Follows calls one whole call graph deep, which is
# what the original net finalizers needed: their join was in teardownServer, not in
# the finalizer body.
add_test(NAME cli/finalizers-do-not-block
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/finalizers_do_not_block.py"
                    "${CMAKE_SOURCE_DIR}")

# End-to-end coverage of the reworked teardown paths under a heap ceiling: seven
# cases, each asserting the value its program computed and, where it can be
# established, that collection really ran while the program waited. Takes about 2 s.
add_test(NAME cli/blocking-joins-and-finalizers
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/blocking-joins-and-finalizers.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/joins")

# Top-level bindings (var/let/const) must survive a collection that runs while the
# script is executing and after it has returned. They live in the interpreter's
# module scope, which nothing in the heap references; until the wrapper pinned it,
# a collection swept it and the bindings read back as undefined (or the process
# crashed). The failure was intermittent on Linux, so the program runs 50 times
# under a heap ceiling, plus 10 runs of the after-eval case. Takes about 20 s.
add_test(NAME cli/toplevel-bindings-survive-gc
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/toplevel-bindings-survive-gc.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/toplevel-bindings"
            50)

# --- Asserting JavaScript tests ------------------------------------------------
#
# Each script checks the values it computes and exits 1 on any failure, so its
# exit code is the verdict. Until these were registered they ran only from
# tests/run_all_tests.sh, which CI does not call: the require() test covered
# exported functions but never an exported class, and nothing gated it anyway.
# Limited to language and module semantics -- nothing here touches the network
# or depends on timing. The "js/" prefix keeps them inside the gating
# `ctest -E "integration|network"` invocation.
foreach(js_test
        basic/class_closure
        basic/closure_scopes
        basic/closure_set_prototype
        basic/object_integrity
        modules/test_require_file
        modules/test_require
        collections/protoCore_collections)
    add_test(NAME js/${js_test}
        COMMAND "$<TARGET_FILE:protojs>"
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/${js_test}.js")
endforeach()
