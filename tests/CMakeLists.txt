# Root of the CppStream tree, resolved relative to this file so the tests still
# work when the library is consumed with add_subdirectory().
set(CPPSTREAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CPPSTREAM_INCLUDE_DIR "${CPPSTREAM_ROOT}/include")

# Vendored doctest, exposed as a SYSTEM include directory so that the vendored
# header never contributes warnings to our build.
add_library(doctest INTERFACE)
add_library(doctest::doctest ALIAS doctest)
target_include_directories(doctest SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest)

add_executable(cppstream_tests
    testMain.cpp
    testSmoke.cpp
    testException.cpp
    testOptional.cpp
    testComparator.cpp
    testStream.cpp
    testArrayList.cpp
    testLinkedList.cpp
    testDeque.cpp
    testListView.cpp
    testRangeViews.cpp
    testDescendingViews.cpp
    testCollector.cpp
    testCollectors.cpp
    testGatherer.cpp
    testParallel.cpp
    testRanges.cpp
    testSet.cpp
    testMap.cpp
    testMapViews.cpp
    testUtilities.cpp
)

target_link_libraries(cppstream_tests PRIVATE
    cppstream::cppstream
    doctest::doctest
    cppstream_warnings
)

if(CPPSTREAM_ENABLE_SANITIZERS)
    cppstream_enable_sanitizers(cppstream_tests)
endif()

# Plain add_test rather than doctest_discover_tests: the latter needs doctest's
# own CMake module, which we do not vendor alongside the single header. The
# timeout keeps a hung test from stalling the whole suite.
add_test(NAME cppstream_tests COMMAND cppstream_tests)
set_tests_properties(cppstream_tests PROPERTIES
    TIMEOUT 600
    LABELS "unit"
)

# ---------------------------------------------------------------------------
# Compile-failure tests
# ---------------------------------------------------------------------------
#
# Single consumption, the collector constraint and the exact-element-type rule are
# compile-time guarantees, so the only honest way to test them is to hand the
# compiler a misuse and assert that it is rejected. cmake/RunCompileFailTest.cmake
# checks that the rejection really happened at the line each case marks, rather
# than trusting CTest's WILL_FAIL, which passes on any non-zero exit.
set(CPPSTREAM_COMPILE_FAIL_CASES
    lvalueIntermediate
    lvalueTerminal
    copyStream
    wrongCollectorElement
    implicitStringPromotion)

if(MSVC)
    set(CPPSTREAM_COMPILE_FAIL_KIND msvc)
    set(CPPSTREAM_COMPILE_FAIL_FLAGS /std:c++latest /Zs /I${CPPSTREAM_INCLUDE_DIR})
else()
    set(CPPSTREAM_COMPILE_FAIL_KIND gnu)
    set(CPPSTREAM_COMPILE_FAIL_FLAGS
        -std=c++${CMAKE_CXX_STANDARD} -fsyntax-only -I${CPPSTREAM_INCLUDE_DIR})
endif()
string(REPLACE ";" "|" CPPSTREAM_COMPILE_FAIL_FLAGS_STR "${CPPSTREAM_COMPILE_FAIL_FLAGS}")

foreach(case IN LISTS CPPSTREAM_COMPILE_FAIL_CASES)
    add_test(NAME compileFail_${case}
        COMMAND ${CMAKE_COMMAND}
                -DCPPSTREAM_CASE=${case}
                -DCPPSTREAM_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${case}.cpp
                -DCPPSTREAM_COMPILER=${CMAKE_CXX_COMPILER}
                "-DCPPSTREAM_FLAGS=${CPPSTREAM_COMPILE_FAIL_FLAGS_STR}"
                -DCPPSTREAM_KIND=${CPPSTREAM_COMPILE_FAIL_KIND}
                -P ${CPPSTREAM_ROOT}/cmake/RunCompileFailTest.cmake)
    set_tests_properties(compileFail_${case} PROPERTIES
        TIMEOUT 60
        LABELS "compile-fail"
    )
endforeach()
