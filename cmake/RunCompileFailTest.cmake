# Runs one "this must not compile" case and decides pass/fail itself.
#
# tests/CMakeLists.txt invokes this through `cmake -P`, one process per case,
# instead of registering the compiler with CTest's WILL_FAIL. WILL_FAIL accepts
# *any* non-zero exit, so a missing compiler, a broken include path or a renamed
# source file would all look like a passing test. This script instead requires a
# located compiler error on the exact line the case marks with a
# `// EXPECTED-ERROR-LINE: N` comment, which pins the verdict to the misuse the
# case is about rather than to any failure at all.
#
# Required -D variables:
#   CPPSTREAM_CASE      case name, used in messages
#   CPPSTREAM_SOURCE    absolute path to the .cpp that must be rejected
#   CPPSTREAM_COMPILER  compiler executable
#   CPPSTREAM_FLAGS     compile flags joined with '|' (';' would be split by -D)
#   CPPSTREAM_KIND      "gnu" or "msvc", selecting the diagnostic shape

foreach(required CPPSTREAM_CASE CPPSTREAM_SOURCE CPPSTREAM_COMPILER CPPSTREAM_FLAGS CPPSTREAM_KIND)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "RunCompileFailTest: missing -D${required}")
    endif()
endforeach()

if(NOT EXISTS "${CPPSTREAM_SOURCE}")
    message(FATAL_ERROR "compile-fail '${CPPSTREAM_CASE}': source not found: ${CPPSTREAM_SOURCE}")
endif()

file(READ "${CPPSTREAM_SOURCE}" caseSource)
if(caseSource MATCHES "EXPECTED-ERROR-LINE:[ \t]*([0-9]+)")
    set(expectedLine "${CMAKE_MATCH_1}")
else()
    message(FATAL_ERROR
        "compile-fail '${CPPSTREAM_CASE}': ${CPPSTREAM_SOURCE} has no 'EXPECTED-ERROR-LINE: N' marker")
endif()

string(REPLACE "|" ";" compileFlags "${CPPSTREAM_FLAGS}")
execute_process(
    COMMAND "${CPPSTREAM_COMPILER}" ${compileFlags} "${CPPSTREAM_SOURCE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdoutText
    ERROR_VARIABLE stderrText)
set(output "${stdoutText}\n${stderrText}")

# A missing compiler leaves a non-numeric result such as "No such file or directory".
if(NOT result MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR
        "compile-fail '${CPPSTREAM_CASE}': could not run '${CPPSTREAM_COMPILER}': ${result}\n${output}")
endif()
if(result STREQUAL "0")
    message(FATAL_ERROR
        "compile-fail '${CPPSTREAM_CASE}': the case compiled, but it must be rejected.\n${CPPSTREAM_SOURCE}")
endif()

# Failures from the plumbing are not a rejection: a missing input or a missing
# library header must fail the test, not masquerade as a successful rejection.
if(output MATCHES "No such file or directory" OR output MATCHES "fatal error")
    message(FATAL_ERROR
        "compile-fail '${CPPSTREAM_CASE}': failure came from a missing file or include, not a rejection:\n${output}")
endif()

get_filename_component(sourceName "${CPPSTREAM_SOURCE}" NAME)
if(CPPSTREAM_KIND STREQUAL "msvc")
    set(errorPattern "${sourceName}\\(${expectedLine}\\): (fatal )?error [A-Z]+[0-9]+")
else()
    set(errorPattern "${sourceName}:${expectedLine}:[0-9]+:.*error:")
endif()

if(NOT output MATCHES "${errorPattern}")
    message(FATAL_ERROR
        "compile-fail '${CPPSTREAM_CASE}': expected an error at ${sourceName}:${expectedLine}, got:\n${output}")
endif()

message(STATUS "compile-fail '${CPPSTREAM_CASE}': rejected as expected (${sourceName}:${expectedLine})")
