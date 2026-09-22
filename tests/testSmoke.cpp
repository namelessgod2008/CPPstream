#include <doctest/doctest.h>

#include <cppstream/cppstream.h>

#include <string_view>
#include <type_traits>

TEST_CASE("toolchain provides the C++23 features the library depends on")
{
    CHECK(cppstream::hasMoveOnlyFunction);
    CHECK(cppstream::hasOptionalMonadic);
}

TEST_CASE("library version metadata is available")
{
    CHECK(cppstream::versionMajor == 0);
    CHECK(cppstream::versionMinor == 1);
    CHECK(cppstream::versionPatch == 0);
    CHECK(cppstream::versionString == "0.1.0");

    static_assert(std::is_same_v<decltype(cppstream::versionString), const std::string_view>);
    static_assert(std::is_same_v<decltype(cppstream::versionMajor), const int>);
}
