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
        target_compile_options(protojs_tests PRIVATE ${PROTOJS_MSVC_WARNINGS})
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

# GC safety under a small heap ceiling (tests/cli/gc-stress.sh; the scripts are
# in tests/integration/gc/, each with its defect described at the top).
#
# gc-frame-garbage: a long-running frame's garbage is collected. The GCC build
# dispatched with a computed goto, which skips the destructors of the scopes it
# leaves, so a handler that dispatched from inside a CriticalSection leaked it
# and the frame's young generation was never handed to the collector: one array
# element written in a loop exhausted any ceiling. One run, about 10 s.
add_test(NAME cli/gc-frame-garbage
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/gc-stress.sh"
            "$<TARGET_FILE:protojs>" 1 400000
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/frame_garbage_is_collected.js")
set_tests_properties(cli/gc-frame-garbage PROPERTIES TIMEOUT 300)

# gc-stale-caches: caches and registries that held cell addresses the collector
# did not see -- the [[Prototype]] override map, the key-interning and
# numeric-index caches, the per-object behaviour cache, the symbol registries.
# Freed addresses are reused at random moments, so every script runs 10 times.
# About 70 s.
add_test(NAME cli/gc-stale-caches
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/gc-stress.sh"
            "$<TARGET_FILE:protojs>" 10 400000
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/stale_proto_override.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/stale_key_caches.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/stale_behavior_cache.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/stale_symbol_registry.js")
set_tests_properties(cli/gc-stale-caches PROPERTIES TIMEOUT 900)

# The REPL reads lines that may end in "\r\n" (a Windows console, or input piped
# from a Windows program) and must still recognise ".exit" and blank lines, and
# it must end at end of input. Feeds "1+1\r\n\r\n.exit\r\n" on every platform.
add_test(NAME cli/repl-commands
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/repl-commands.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/repl")

# A module reached through two spellings of one file (a symbolic link; on
# Windows also a different letter case) is loaded once, by require() and by
# import alike: both loaders take the identity from platform::canonicalPath.
add_test(NAME cli/module-identity
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/module-identity.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/module-identity")

# child.kill(0) is an existence check that leaves the child running, and kill()
# reports whether the signal was delivered. On Windows it also answers false
# once the child has exited. Takes about 2 s.
add_test(NAME cli/child-kill-signal0
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/child-kill-signal0.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/child-kill-signal0")

# An exception that crosses a module boundary is the one thrown: caught by the
# caller's try statement or, uncaught, reported with exit status 1 (CommonJS and
# ES modules).
add_test(NAME cli/cross-module-exceptions
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/cross-module-exceptions.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/cross-module-exceptions")

# process.exit(code) at top level and from event callbacks (setImmediate, a
# promise reaction, an I/O completion, a worker's 'exit' handler) ends the
# process with that code after flushing what the program wrote.
add_test(NAME cli/process-exit
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/process-exit.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/process-exit")

# An exception that escapes an event-loop callback, or a promise rejection no
# handler has claimed by the end of its turn, ends the process with status 1 (as
# in Node); handled rejections and caught exceptions do not.
add_test(NAME cli/uncaught-errors
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/uncaught-errors.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/uncaught-errors")

# A loop creating 2,000,000 closures under the default heap ceiling: must print
# the right sum with a bounded peak resident set (the script kills the run at a
# safety cap so a regression cannot exhaust the machine). About a minute.
add_test(NAME cli/closure-loop-bounded-memory
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/closure_loop_bounded_memory.py"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/closure-loop-bounded-memory")
set_tests_properties(cli/closure-loop-bounded-memory PROPERTIES TIMEOUT 1200)

# The same with an ordinary `function` expression, whose `prototype` object
# (and the cycle fn.prototype.constructor === fn it brings) is now created only
# on first need (src/runtime/LazyPrototype.h). Before that, every such closure
# was retained and the loop filled the default ceiling.
add_test(NAME cli/function-closure-loop-bounded-memory
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/closure_loop_bounded_memory.py"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/function-closure-loop-bounded-memory"
            function)
set_tests_properties(cli/function-closure-loop-bounded-memory PROPERTIES TIMEOUT 1200)

# --- Asserting JavaScript tests ------------------------------------------------
#
# Each script checks the values it computes and exits 1 on any failure, so its
# exit code is the verdict. Until these were registered they ran only from
# tests/run_all_tests.sh, which CI does not call: the require() test covered
# exported functions but never an exported class, and nothing gated it anyway.
# Limited to what is platform-independent -- language and module semantics,
# crypto, Deferred, worker threads and native addons; nothing here touches the
# network or depends on timing -- so the same list runs on Linux, macOS and
# Windows. The "js/" prefix keeps them inside the gating
# `ctest -E "integration|network"` invocation.
foreach(js_test
        basic/array_elisions
        basic/captured_local_update
        basic/class_closure
        basic/closure_scopes
        basic/closure_set_prototype
        basic/lazy_function_prototype
        basic/new_non_constructor
        basic/object_integrity
        basic/symbol_key_order
        basic/native_function_prototype
        basic/regexp_global_advance
        modules/test_require_file
        modules/test_cross_module_exception
        modules/test_require
        collections/protoCore_collections
        workers/deep_recursion
        crypto/test_crypto_cipher
        crypto/test_crypto_keygen
        crypto/test_crypto_sign
        deferred/concurrent_deferred
        deferred/test_deferred_reject
        test_deferred_basic
        native_addons/test_native_require
        native_addons/test_resolution)
    add_test(NAME js/${js_test}
        COMMAND "$<TARGET_FILE:protojs>"
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/${js_test}.js")
endforeach()

# util.format / util.inspect: Node.js's own test/parallel/test-util-format.js,
# adapted like the path tests below (the cases protoJS cannot run are commented
# out, each with the reason).
add_test(NAME js/util/test-util-format
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/util/test-util-format.js")

# The fs callback API (fs.readFile(path, cb) and siblings). The checks run in
# callbacks, so the test must also print its final line: a callback that never
# runs would otherwise end the process with status 0.
add_test(NAME js/fs/test_fs_callbacks
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/fs/test_fs_callbacks.js")
set_tests_properties(js/fs/test_fs_callbacks PROPERTIES
    PASS_REGULAR_EXPRESSION "test_fs_callbacks: all [0-9]+ checks passed"
    FAIL_REGULAR_EXPRESSION "FAILED|Uncaught exception")

# dns.lookup(name, callback): the pending lookup alone must keep the process
# alive until the callback has run. 'localhost' resolves without the network.
# Same final-line rule as the fs callbacks: a callback that never runs fails.
add_test(NAME js/dns/test_dns_lookup_callback
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/dns/test_dns_lookup_callback.js")
set_tests_properties(js/dns/test_dns_lookup_callback PROPERTIES
    PASS_REGULAR_EXPRESSION "test_dns_lookup_callback: all [0-9]+ checks passed"
    FAIL_REGULAR_EXPRESSION "FAILED|Uncaught exception")

# tests/conformity: the asserting conformity scripts (each throws on the first
# failed check, so a non-zero exit is the verdict). Platform-independent, so they
# run on Linux, macOS and Windows alike.
foreach(conf_test
        builtins/test_array_conformity
        builtins/test_number_conformity
        builtins/test_object_conformity
        builtins/test_string_conformity
        import/test_module_identity)
    add_test(NAME conformity/${conf_test}
        COMMAND "$<TARGET_FILE:protojs>"
                "${CMAKE_CURRENT_SOURCE_DIR}/conformity/${conf_test}.js")
endforeach()

# tests/integration/path: Node.js's own tests of the `path` module
# (test/parallel/test-path-*.js), adapted to run without Node's harness (see
# tests/integration/path/node_shim.js). path.win32 and path.posix are lexical,
# so both are exercised on every platform; the tests that depend on the default
# `path` check that it is path.win32 on Windows and path.posix elsewhere.
foreach(path_test
        test-path
        test-path-basename
        test-path-dirname
        test-path-extname
        test-path-isabsolute
        test-path-join
        test-path-makelong
        test-path-normalize
        test-path-parse-format
        test-path-posix-exists
        test-path-posix-relative-on-windows
        test-path-relative
        test-path-resolve
        test-path-win32-exists
        test-path-win32-normalize-device-names
        test-path-zero-length-strings)
    add_test(NAME js/path/${path_test}
        COMMAND "$<TARGET_FILE:protojs>"
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/path/${path_test}.js")
endforeach()
