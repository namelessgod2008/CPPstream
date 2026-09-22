cmake_minimum_required(VERSION 3.25)

project(CppStream
    VERSION 0.1.0
    DESCRIPTION "C++23 reimplementation of the Java Stream API and Collection hierarchy"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Predictable binary location so the CLI and CLion agree on where things land.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CPPSTREAM_IS_TOP_LEVEL ON)
else()
    set(CPPSTREAM_IS_TOP_LEVEL OFF)
endif()

# Single-config generators default to an empty CMAKE_BUILD_TYPE, which silently
# produces an unoptimised build with no debug info. Pin it to Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

option(CPPSTREAM_BUILD_TESTS        "Build the doctest test suite"  ${CPPSTREAM_IS_TOP_LEVEL})
option(CPPSTREAM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"  OFF)
option(CPPSTREAM_ENABLE_SANITIZERS  "Enable ASan + UBSan on test targets"  OFF)
option(CPPSTREAM_BUILD_EXAMPLES     "Build the example programs"  ${CPPSTREAM_IS_TOP_LEVEL})

# ---------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------

# Header-only INTERFACE target: the library is almost entirely templates, so a
# static library would only add explicit instantiation boilerplate.
add_library(cppstream INTERFACE)
add_library(cppstream::cppstream ALIAS cppstream)

target_include_directories(cppstream INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_compile_features(cppstream INTERFACE cxx_std_23)

# Warnings live on a separate INTERFACE target so they reach our tests and
# examples but are never forced onto consumers of the library.
add_library(cppstream_warnings INTERFACE)

include(cmake/CppStreamWarnings.cmake)
cppstream_enable_warnings(cppstream_warnings)

if(CPPSTREAM_WARNINGS_AS_ERRORS)
    cppstream_warnings_as_errors(cppstream_warnings)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(CPPSTREAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------

if(CPPSTREAM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
