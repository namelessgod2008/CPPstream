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
# own CMake module, which we do not vendor alongside the single header.
add_test(NAME cppstream_tests COMMAND cppstream_tests)

# ---------------------------------------------------------------------------
# Compile-failure tests
# ---------------------------------------------------------------------------
#
# Single consumption, the collector constraint and the exact-element-type rule are
# compile-time guarantees, so the only honest way to test them is to hand the
# compiler a misuse and assert that it is rejected. CTest's WILL_FAIL on a
# -fsyntax-only invocation does exactly that with no extra machinery.
set(CPPSTREAM_COMPILE_FAIL_CASES
    lvalueIntermediate
    lvalueTerminal
    copyStream
    wrongCollectorElement
    implicitStringPromotion)

foreach(case IN LISTS CPPSTREAM_COMPILE_FAIL_CASES)
    add_test(NAME compileFail_${case}
        COMMAND ${CMAKE_CXX_COMPILER} -std=c++23 -fsyntax-only
                -I${PROJECT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${case}.cpp)
    set_tests_properties(compileFail_${case} PROPERTIES WILL_FAIL TRUE)
endforeach()
