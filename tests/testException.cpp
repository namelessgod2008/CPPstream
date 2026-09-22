#include <doctest/doctest.h>

#include <cppstream/cppstream.h>

#include <stdexcept>
#include <string>
#include <type_traits>

TEST_CASE("every library exception derives from RuntimeException")
{
    static_assert(std::is_base_of_v<std::runtime_error, cppstream::RuntimeException>);

    static_assert(std::is_base_of_v<cppstream::RuntimeException, cppstream::IllegalStateException>);
    static_assert(std::is_base_of_v<cppstream::RuntimeException, cppstream::NoSuchElementException>);
    static_assert(
        std::is_base_of_v<cppstream::RuntimeException, cppstream::UnsupportedOperationException>);
    static_assert(std::is_base_of_v<cppstream::RuntimeException, cppstream::NullPointerException>);
    static_assert(
        std::is_base_of_v<cppstream::RuntimeException, cppstream::IllegalArgumentException>);
    static_assert(
        std::is_base_of_v<cppstream::RuntimeException, cppstream::ConcurrentModificationException>);
    static_assert(std::is_base_of_v<cppstream::RuntimeException, cppstream::ArithmeticException>);

    CHECK(true);
}

TEST_CASE("exception subtypes preserve Java's catch-compatibility hierarchy")
{
    // Java throws a bare IllegalStateException for duplicate toMap keys; the
    // subtype must remain catchable as such.
    static_assert(std::is_base_of_v<cppstream::IllegalStateException,
        cppstream::IllegalCollectorStateException>);

    // IndexOutOfBoundsException sits under IllegalArgumentException, so one
    // catch handles both the Java-style and the C++-style root.
    static_assert(std::is_base_of_v<cppstream::IllegalArgumentException,
        cppstream::IndexOutOfBoundsException>);
    static_assert(
        std::is_base_of_v<cppstream::RuntimeException, cppstream::IndexOutOfBoundsException>);

    CHECK(true);
}

TEST_CASE("messages survive construction from both a string and a string literal")
{
    const cppstream::IllegalStateException fromLiteral(
        "stream has already been operated upon or closed");
    CHECK(std::string(fromLiteral.what()) == "stream has already been operated upon or closed");

    const std::string message = "No value present";
    const cppstream::NoSuchElementException fromString(message);
    CHECK(std::string(fromString.what()) == message);
}

TEST_CASE("exceptions can be caught through their supertypes")
{
    const auto throwDuplicateKey = [] {
        throw cppstream::IllegalCollectorStateException("Duplicate key");
    };

    CHECK_THROWS_AS(throwDuplicateKey(), cppstream::IllegalCollectorStateException);
    CHECK_THROWS_AS(throwDuplicateKey(), cppstream::IllegalStateException);
    CHECK_THROWS_AS(throwDuplicateKey(), cppstream::RuntimeException);
    CHECK_THROWS_AS(throwDuplicateKey(), std::runtime_error);

    const auto throwIndexError = [] {
        throw cppstream::IndexOutOfBoundsException("Index 5 out of bounds for length 3");
    };

    CHECK_THROWS_AS(throwIndexError(), cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_AS(throwIndexError(), cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(throwIndexError(), cppstream::RuntimeException);
}
