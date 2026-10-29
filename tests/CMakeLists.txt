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

# The bytecode listings of docs/VM.md: every docs/vm-examples/NN-name.js is
# disassembled with `protojs --disasm` and compared with its golden listing and
# with VM.md's copy, so a compiler, loader or specialiser change that alters the
# bytecode fails here until the documentation is regenerated (--update) and
# reviewed.
add_test(NAME docs/vm-examples
    COMMAND ${PROTOJS_PYTHON} -I "${CMAKE_SOURCE_DIR}/docs/vm-examples/disasm_examples.py"
            "$<TARGET_FILE:protojs>" --check)

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

# Named property reads resolved by one protoCore lookup (namedGetFast in
# ProtoInterpreter.cpp) answer what the full [[Get]] path answers: the script's
# output must equal Node's.
add_test(NAME cli/attribute-access
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/attribute-access.sh"
            "$<TARGET_FILE:protojs>")

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
# numeric-index caches, the per-object behaviour cache, the symbol registries
# -- built-in prototypes made on first use and cached in a static, and the
# "undefined" string ToString(undefined) returns. Freed
# addresses are reused at random moments, so every script runs 10 times.
# About 90 s.
add_test(NAME cli/gc-stale-caches
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/gc-stress.sh"
            "$<TARGET_FILE:protojs>" 10 400000
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/stale_proto_override.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/stale_key_caches.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/stale_behavior_cache.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/stale_symbol_registry.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/collected_builtin_prototypes.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/cached_undefined_string.js")
set_tests_properties(cli/gc-stale-caches PROPERTIES TIMEOUT 900)

# gc-exceptions: an exception thrown by a callee outlives the callee's context.
# It travelled back only through C++ locals and the t_callException
# thread-local, so a native that ran more JavaScript before rethrowing it
# (IteratorClose calling return()) handed the catch block freed cells. Before
# the fix all 10 runs failed (wrong values or SIGSEGV). About 20 s.
add_test(NAME cli/gc-exceptions
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/gc-stress.sh"
            "$<TARGET_FILE:protojs>" 10 300000
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/exception_across_contexts.js")
set_tests_properties(cli/gc-exceptions PROPERTIES TIMEOUT 600)

# gc-frame-object: a call's frame object, the parent of every closure the call
# creates, was held only in the t_currentFrameObj thread-local. Once the closures
# made so far had died, a collection freed it and the next closure was built on
# the reused cell; the collector then aborted on a corrupt parent link. Before
# the fix all 10 runs failed (SIGABRT or SIGSEGV). About 90 s.
add_test(NAME cli/gc-frame-object
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/gc-stress.sh"
            "$<TARGET_FILE:protojs>" 10 400000
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/frame_object_outlives_closures.js")
set_tests_properties(cli/gc-frame-object PROPERTIES TIMEOUT 600)

# gc-promises: pending reactions, queued jobs and async functions suspended at
# an await are reachable only from the job queue and the promises themselves;
# the collector must see all of them. Three runs under a small ceiling: the
# 3000 pending promise groups need between 600,000 and 800,000 cells at the
# peak (measured: 600,000 runs out of memory), and 800,000 makes the collector
# run about a hundred times per run.
add_test(NAME cli/gc-promises
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/gc-stress.sh"
            "$<TARGET_FILE:protojs>" 3 800000
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/pending_promises.js")
set_tests_properties(cli/gc-promises PROPERTIES TIMEOUT 900)

# gc-deferred: Deferreds allocate on pool threads of the main thread's space
# while the main thread allocates and reads a shared table; every result is
# checked in full. A thread now parks whenever ANOTHER thread needs a
# collection, which is what exposed operands held only in C++ locals inside
# interpreter handlers (ProtoInterpreter.cpp, stackPop). Ten runs; before the
# fix about one run in fifteen lost attributes of objects under construction.
add_test(NAME cli/gc-deferred
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/gc-stress.sh"
            "$<TARGET_FILE:protojs>" 10 300000
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/gc/deferred_gc_stress.js")
set_tests_properties(cli/gc-deferred PROPERTIES TIMEOUT 900)

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

# A Deferred of an ES module (QuickJS-run) runs on the main wrapper's pool, in
# the one ProtoSpace, and settles its then callback.
add_test(NAME cli/module-mode-deferred
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/module-mode-deferred.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/module-mode-deferred")

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
# Promise and async-function job ordering, compared with Node's output for the
# same scripts (tests/integration/promises/*.expected; see the script header).
add_test(NAME cli/promise-fixtures
    COMMAND ${PROTOJS_SH} "${CMAKE_CURRENT_SOURCE_DIR}/cli/promise-fixtures.sh"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/promises")
set_tests_properties(cli/promise-fixtures PROPERTIES TIMEOUT 600)

# Settled promises are collectable: 100,000 rejected-and-handled promises under
# a heap ceiling far below what retaining them would need. Each `new Promise`
# used to register itself on the global object for the life of the process.
add_test(NAME cli/promise-rejections-bounded-memory
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/promise_rejections_bounded_memory.py"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/promise-rejections-bounded-memory")
set_tests_properties(cli/promise-rejections-bounded-memory PROPERTIES TIMEOUT 1200)

# 500,000 distinct computed keys (obj[k] = v and JSON.parse) through a
# long-running loop under a small heap ceiling: the resident set must stay flat.
# Keys that are data live in the object's property dictionary as collectable
# strings (src/runtime/PropertyDictionary.h); before, every one was interned in
# protoCore's SymbolTable for good. About 15 s.
add_test(NAME cli/dynamic-keys-bounded-memory
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/dynamic_keys_bounded_memory.py"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/dynamic-keys-bounded-memory")
set_tests_properties(cli/dynamic-keys-bounded-memory PROPERTIES TIMEOUT 900)

# 100,000 distinct eval() sources (direct, top-level and indirect, keeping a
# closure of every thousandth) under a small heap ceiling: the resident set
# must stay flat, and an eval of a repeated source must still hit the cache.
# Eval modules live in a bounded cache and while closures or activations need
# them (src/runtime/EvalModuleCache.h); before, every one was kept for good.
# About 10 s.
add_test(NAME cli/eval-bounded-memory
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/eval_bounded_memory.py"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/eval-bounded-memory")
set_tests_properties(cli/eval-bounded-memory PROPERTIES TIMEOUT 900)

# The default heap ceiling is 75 % of physical memory (or of a smaller cgroup
# limit), and PROTOCORE_HEAP_LIMIT_CELLS overrides it (src/HeapLimit.h).
add_test(NAME cli/heap-limit-default
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/heap_limit_default.py"
            "$<TARGET_FILE:protojs>")

# 200,000 live objects under a ceiling of 11 cells each and a checked peak
# resident set (memory per object; tests/cli/objects_bounded_memory.py).
add_test(NAME cli/objects-bounded-memory
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/objects_bounded_memory.py"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/objects-bounded-memory")
set_tests_properties(cli/objects-bounded-memory PROPERTIES TIMEOUT 900)

# Pending work keeps the process alive without a time limit (it used to be
# 180 s); PROTOJS_EXIT_TIMEOUT_SECONDS sets one.
add_test(NAME cli/exit-timeout
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/exit_timeout.py"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/exit-timeout")

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
        basic/new_target_calls
        basic/object_integrity
        basic/symbol_key_order
        basic/native_function_prototype
        basic/number_representation
        basic/number_to_string
        basic/string_to_number
        basic/bigint_number_compare
        basic/object_literal_build
        basic/put_field_groups
        basic/primitive_property_writes
        basic/sloppy_this_binding
        basic/global_var_bindings
        basic/function_kind_constructors
        basic/bigint_wrapper
        basic/typed_array_objects
        basic/put_field_group_cells
        basic/for_of_state
        basic/array_map_filter
        basic/regexp_global_advance
        basic/regexp_register_slots
        basic/string_cells_bounded
        basic/call_activation_kinds
        basic/string_nul
        basic/array_from_large
        basic/for_in_strings
        basic/string_access_scaling
        basic/string_utf16_units
        basic/array_bulk_scaling
        basic/array_push_length
        basic/array_from_of_this
        basic/escape_unescape
        basic/generator_state_keys
        basic/modules_nul
        basic/generator_for_of_yield
        basic/generator_resumption
        basic/typed_array_construction
        basic/typed_array_coercion
        basic/typed_array_callbacks
        modules/test_require_file
        modules/test_cross_module_exception
        modules/test_require
        modules/test_events
        collections/protoCore_collections
        collections/map_set_scaling
        collections/map_set_semantics
        workers/deep_recursion
        workers/promises_in_worker
        workers/prototypes_after_worker
        crypto/test_crypto_cipher
        crypto/test_crypto_keygen
        crypto/test_crypto_sign
        deferred/concurrent_deferred
        deferred/parallel_deferred
        deferred/put_field_group_atomicity
        deferred/test_deferred_reject
        test_deferred_basic
        basic/computed_accessor_keys
        basic/typed_array_sort
        basic/symbol_keyed_calls
        basic/computed_accessor_names
        basic/method_enumerability
        basic/super_in_computed_methods
        basic/super_property_accessors
        basic/dictionary_keys
        basic/dictionary_shadowing
        basic/specialiser_correctness
        basic/eval_and_with
        basic/class_members_and_calls
        basic/semantics_equality_tdz_iterators
        deferred/dictionary_keys_shared
        deferred/result_is_shared
        workers/dictionary_keys_message
        workers/one_space_messages)
    add_test(NAME js/${js_test}
        COMMAND "$<TARGET_FILE:protojs>"
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/${js_test}.js")
endforeach()

# Deferreds under a small heap budget: the first Deferred switches the space to
# protoCore's adaptive heap controller, whose budget is this variable; the test
# checks that the heap ends within it (tests/unit/test_adaptive_heap.cpp checks
# when the controller is on).
add_test(NAME js/deferred/adaptive_heap_budget
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/deferred/adaptive_heap_budget.js")
set_tests_properties(js/deferred/adaptive_heap_budget PROPERTIES
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=4000000")

# eval()'s modules under a small heap ceiling: closures of every kind made by
# eval code outlive the eviction of their module from the bounded eval cache
# (src/runtime/EvalModuleCache.h), and evals nested and recursive past the
# cache's capacity keep their modules while they run. Collections run during
# the loads, which before 2026-10-25 hid the caller's frames from the
# collector (wrong sums).
add_test(NAME js/basic/eval_module_lifetime
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/basic/eval_module_lifetime.js")
set_tests_properties(js/basic/eval_module_lifetime PROPERTIES
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=300000" TIMEOUT 300)

# Many Deferreds under a small heap budget: collections run while the pool
# allocates, results arrive intact, the kept ones survive, one ProtoSpace.
add_test(NAME js/deferred/many_deferreds_small_heap
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/deferred/many_deferreds_small_heap.js")
set_tests_properties(js/deferred/many_deferreds_small_heap PROPERTIES
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=3000000" TIMEOUT 600)

# Workers start and stop while the main thread allocates arrays, typed arrays,
# maps and sets under a small heap ceiling: nothing the main thread makes may
# refer to a destroyed worker space (docs/MEMORY_SAFETY_PROPOSALS.md, section 1).
add_test(NAME js/workers/workers_churn_small_heap
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/workers/workers_churn_small_heap.js")
set_tests_properties(js/workers/workers_churn_small_heap PROPERTIES
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=300000" TIMEOUT 300)

# The native addon tests load the addons THIS build directory produced, so two
# build directories never pick up each other's.  test_native_require gets the
# addon's path; test_resolution runs from the copy placed next to the fixture
# addon (CMakeLists.txt: the addon and the sibling fixture.js must share a
# directory).
add_test(NAME js/native_addons/test_native_require
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/native_addons/test_native_require.js"
            "$<TARGET_FILE:simple_addon>")
add_test(NAME js/native_addons/test_resolution
    COMMAND "$<TARGET_FILE:protojs>"
            "$<TARGET_FILE_DIR:fixture_addon>/test_resolution.js")

# Errors of a module written in C++ (protoCore's geometry example, loaded
# through UMD by the geometry addon) are caught by JavaScript handlers naming
# the module's types; protoCore's own errors and a C++ standard exception
# arrive as JavaScript errors (docs/EXCEPTIONS.md).  Built only when protoCore's
# example sources are available (CMakeLists.txt, geometry_addon).
if(TARGET geometry_addon)
    add_test(NAME js/native_addons/test_module_exceptions
        COMMAND "$<TARGET_FILE:protojs>"
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/native_addons/test_module_exceptions.js"
                "$<TARGET_FILE:geometry_addon>")
    # Uncaught, the module's error is reported from its name and message.
    add_test(NAME js/native_addons/module_exception_uncaught
        COMMAND "$<TARGET_FILE:protojs>"
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/native_addons/module_exception_uncaught.js"
                "$<TARGET_FILE:geometry_addon>")
    set_tests_properties(js/native_addons/module_exception_uncaught PROPERTIES
        PASS_REGULAR_EXPRESSION "NegativeRadius: circleArea: the radius -1.000000 is negative"
        FAIL_REGULAR_EXPRESSION "not reached")
endif()

# process.argv carries the arguments after the script; process.memoryUsage().
add_test(NAME js/modules/process_argv_memory
    COMMAND "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/modules/process_argv_memory.js"
            alpha "two words" --flag)

# Allocation is measured with no collection in between (the test checks it).
set_tests_properties(js/basic/put_field_group_cells PROPERTIES
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=100000000")

# A 100 KB string iterated, spread, passed to Array.from, destructured, indexed
# and run through global regular expressions, under a fixed heap with the
# adaptive controller off. The largest path needs 2.66 M cells on Linux
# (measurements in the file); before the string fixes Array.from and the
# global replace and match ran out of this limit. A 1,000,000-cell limit
# passed on Linux and failed on Windows (2026-10-27): it was below what the
# array-building paths need, and only collection timing let them through.
set_tests_properties(js/basic/string_cells_bounded PROPERTIES
    ENVIRONMENT "PROTOCORE_ADAPTIVE_HEAP=0;PROTOCORE_HEAP_LIMIT_CELLS=6000000"
    TIMEOUT 120)

# Array.from over 150,000-element iterators and a 250,000-element array-like.
set_tests_properties(js/basic/array_from_large PROPERTIES TIMEOUT 300)
# Times each string access for n and 4n characters and checks the ratio. It
# allocates about 12 GB of cells in all, nearly all garbage, so the heap policy
# decides what it measures. Under the default policy the heap grows up to a
# ceiling derived from physical memory: on a large Linux machine it never
# collected (0 cycles, 11.7 GB RSS, 18.6 s), while on the 7 GB macOS runner it
# reached its ceiling, collected twice over about 85 M cells, and took 130-149 s
# (2026-10-29): about 90 s of that was the exit, which waits for the
# collector's last cycle. A cycle that landed in a 4n sample also pushed a ratio
# past the bound once (10.9, "length after prepend"). The adaptive controller
# makes the heap the same on every machine: small, frequent cycles (37 cycles,
# 0.57 GB, 18.6 s on Linux; 19.3 s on the macOS runner), with collection cost
# spread evenly over the samples. The limit is about 6x the slowest run
# measured under it, for a loaded runner.
set_tests_properties(js/basic/string_access_scaling PROPERTIES
    ENVIRONMENT "PROTOCORE_ADAPTIVE_HEAP=1"
    TIMEOUT 120)
set_tests_properties(js/basic/array_bulk_scaling PROPERTIES TIMEOUT 300)

# A quadratic Map.set / Set.add would not finish within this limit.
set_tests_properties(js/collections/map_set_scaling PROPERTIES TIMEOUT 60)

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

# `protojs --dap`: debug sessions driven the way VS Code drives them --
# breakpoints, the call stack, variables, evaluate, stepping, stopOnEntry,
# conditional breakpoints, process.exit, and the program's output (more than
# 64 KB of non-ASCII text, a child reading stdin) arriving as `output` events
# without disturbing the protocol (docs/DEBUGGING.md). A few seconds.
add_test(NAME cli/dap-session
    COMMAND ${PROTOJS_PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cli/dap_session.py"
            "$<TARGET_FILE:protojs>"
            "${CMAKE_CURRENT_BINARY_DIR}/cli-scratch/dap-session")
set_tests_properties(cli/dap-session PROPERTIES TIMEOUT 300)
