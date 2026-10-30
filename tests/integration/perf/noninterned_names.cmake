# Runs one protojs script with protoCore's PROTOCORE_NONINTERNED_NAMES report
# on, and fails when the process passed more than MAX non-interned names to
# protoCore attribute operations.
#
# protoCore attribute keys are interned symbols.  A heap-string name (any
# string of seven or more bytes that is not a symbol) is canonicalised
# against the symbol table on every call; protoCore counts each such call
# and, with the variable set, appends a report at exit.  Fixed names must be
# symbols built once, and names derived from an index or a key must not be
# built on the normal path, so the count of a script is a small constant,
# not a per-operation figure.
#
#   cmake -DPROTOJS=<protojs> -DSCRIPT=<script.js> -DREPORT=<file> -DMAX=<n>
#         -P noninterned_names.cmake
#
# A protoCore without the report (before 3.4) writes nothing: the test prints
# "SKIP" (SKIP_REGULAR_EXPRESSION in tests/CMakeLists.txt).
foreach(v PROTOJS SCRIPT REPORT MAX)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "noninterned_names.cmake: -D${v}=... is required")
    endif()
endforeach()
file(REMOVE "${REPORT}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "PROTOCORE_NONINTERNED_NAMES=${REPORT}"
            "${PROTOJS}" "${SCRIPT}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}")
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${SCRIPT} failed (exit ${rc}):\n${out}\n${err}")
endif()
if(NOT EXISTS "${REPORT}")
    message("SKIP: this protoCore has no PROTOCORE_NONINTERNED_NAMES report")
    return()
endif()
file(READ "${REPORT}" report)
if(NOT report MATCHES "total=([0-9]+)")
    message(FATAL_ERROR "unreadable report:\n${report}")
endif()
set(total "${CMAKE_MATCH_1}")
message("non-interned attribute names: ${total} (at most ${MAX})")
if(total GREATER MAX)
    message(FATAL_ERROR "${SCRIPT}: ${total} non-interned attribute names, more than ${MAX}:\n${report}")
endif()
