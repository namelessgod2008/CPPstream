#include <doctest/doctest.h>

#include <cppstream/cppstream.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

TEST_CASE("an empty Optional reports itself as empty")
{
    const cppstream::Optional<int> absent;

    CHECK(absent.isEmpty());
    CHECK_FALSE(absent.isPresent());
    CHECK(absent.equals(cppstream::Optional<int>()));
    CHECK(absent.hashCode() == std::size_t{0});
}

TEST_CASE("a value Optional can be built by every Java factory")
{
    const auto byConstructor = cppstream::Optional<int>{42};
    CHECK(byConstructor.isPresent());
    CHECK(byConstructor.get() == 42);

    // Implicit construction from a value keeps call sites as short as Java's
    // Optional.of without spelling the factory out.
    const cppstream::Optional<int> byImplicit = 42;
    CHECK(byImplicit.get() == 42);

    CHECK(cppstream::Optional<int>::of(42).get() == 42);
    CHECK(cppstream::Optional<int>::ofNullable(42).get() == 42);

    // An empty braced initialiser must select the default constructor rather
    // than the converting constructors.
    const cppstream::Optional<int> fromEmptyBraces = {};
    CHECK(fromEmptyBraces.isEmpty());

    const cppstream::Optional<int> fromNullopt = std::nullopt;
    CHECK(fromNullopt.isEmpty());
}

TEST_CASE("ofNullable collapses a null pointer, and only a pointer")
{
    int value = 7;
    int* const present = &value;
    int* const absent = nullptr;

    CHECK(cppstream::Optional<int*>::ofNullable(present).get() == present);
    CHECK(cppstream::Optional<int*>::ofNullable(absent).isEmpty());

    // For a non-pointer payload there is no null to collapse, so ofNullable is
    // just of().
    CHECK(cppstream::Optional<int>::ofNullable(7).isPresent());
}

TEST_CASE("get() throws NoSuchElementException with Java's message")
{
    const cppstream::Optional<int> absent;

    CHECK_THROWS_WITH_AS(absent.get(), "No value present", cppstream::NoSuchElementException);
    CHECK_THROWS_AS(absent.get(), cppstream::RuntimeException);
}

TEST_CASE("get() on a non-const lvalue yields a mutable reference")
{
    cppstream::Optional<std::string> greeting = std::string("hello");
    greeting.get() += " world";

    CHECK(greeting.get() == "hello world");
}

TEST_CASE("orElse and orElseGet supply a fallback only when needed")
{
    const cppstream::Optional<int> present = cppstream::Optional<int>::of(1);
    const cppstream::Optional<int> absent;

    CHECK(present.orElse(99) == 1);
    CHECK(absent.orElse(99) == 99);

    int supplierCalls = 0;
    const auto supplier = [&supplierCalls] {
        ++supplierCalls;
        return 99;
    };

    CHECK(present.orElseGet(supplier) == 1);
    CHECK(supplierCalls == 0);
    CHECK(absent.orElseGet(supplier) == 99);
    CHECK(supplierCalls == 1);
}

TEST_CASE("orElseThrow supports both the Java 10 no-arg form and a custom supplier")
{
    const cppstream::Optional<int> present = cppstream::Optional<int>::of(1);
    const cppstream::Optional<int> absent;

    CHECK(present.orElseThrow() == 1);
    CHECK_THROWS_AS(absent.orElseThrow(), cppstream::NoSuchElementException);

    CHECK(present.orElseThrow([] {
        return cppstream::IllegalStateException("unreachable");
    }) == 1);
    CHECK_THROWS_WITH_AS(absent.orElseThrow([] {
        return cppstream::NoSuchElementException("nothing here");
    }), "nothing here", cppstream::NoSuchElementException);
}

TEST_CASE("ifPresent and ifPresentOrElse run exactly one branch")
{
    const cppstream::Optional<int> present = cppstream::Optional<int>::of(5);
    const cppstream::Optional<int> absent;

    int seen = 0;
    present.ifPresent([&seen](int value) { seen = value; });
    CHECK(seen == 5);

    seen = 0;
    absent.ifPresent([&seen](int value) { seen = value; });
    CHECK(seen == 0);

    int presentBranch = 0;
    int emptyBranch = 0;
    present.ifPresentOrElse([&presentBranch](int) { ++presentBranch; }, [&emptyBranch] { ++emptyBranch; });
    CHECK(presentBranch == 1);
    CHECK(emptyBranch == 0);

    absent.ifPresentOrElse([&presentBranch](int) { ++presentBranch; }, [&emptyBranch] { ++emptyBranch; });
    CHECK(presentBranch == 1);
    CHECK(emptyBranch == 1);
}

TEST_CASE("map transforms a present value and short-circuits on an empty one")
{
    const auto greeting = cppstream::Optional<std::string>::of(std::string("hello"));

    const auto length = greeting.map([](const std::string& value) {
        return static_cast<int>(value.size());
    });
    CHECK(length.get() == 5);

    int mapperCalls = 0;
    const auto mappedNothing = cppstream::Optional<std::string>::empty().map(
        [&mapperCalls](const std::string& value) {
            ++mapperCalls;
            return static_cast<int>(value.size());
        });
    CHECK(mappedNothing.isEmpty());
    CHECK(mapperCalls == 0);
}

TEST_CASE("map also collapses a mapper that returns a null pointer")
{
    const auto wrapper = cppstream::Optional<int>::of(1);
    const auto neverPresent = wrapper.map([](int) -> int* { return nullptr; });

    CHECK(neverPresent.isEmpty());
}

TEST_CASE("flatMap takes a mapper that already returns an Optional")
{
    const auto lengthOf = [](const std::string& value) {
        if (value.empty()) {
            return cppstream::Optional<int>::empty();
        }
        return cppstream::Optional<int>::of(static_cast<int>(value.size()));
    };

    const auto greeting = cppstream::Optional<std::string>::of(std::string("hello"));
    CHECK(greeting.flatMap(lengthOf).get() == 5);
    CHECK(cppstream::Optional<std::string>::of(std::string()).flatMap(lengthOf).isEmpty());
    CHECK(cppstream::Optional<std::string>::empty().flatMap(lengthOf).isEmpty());
}

TEST_CASE("filter keeps a value only when the predicate accepts it")
{
    const auto even = cppstream::Optional<int>::of(4);
    const auto odd = cppstream::Optional<int>::of(5);
    const cppstream::Optional<int> absent;

    CHECK(even.filter([](int value) { return value % 2 == 0; }).get() == 4);
    CHECK(odd.filter([](int value) { return value % 2 == 0; }).isEmpty());

    int predicateCalls = 0;
    CHECK(absent.filter([&predicateCalls](int) { ++predicateCalls; return true; }).isEmpty());
    CHECK(predicateCalls == 0);
}

TEST_CASE("equals and hashCode follow Java's Optional contract")
{
    const cppstream::Optional<int> five = cppstream::Optional<int>::of(5);
    const cppstream::Optional<int> alsoFive = cppstream::Optional<int>::of(5);
    const cppstream::Optional<int> six = cppstream::Optional<int>::of(6);
    const cppstream::Optional<int> absent;

    CHECK(five.equals(alsoFive));
    CHECK_FALSE(five.equals(six));
    CHECK_FALSE(five.equals(absent));
    CHECK(absent.equals(cppstream::Optional<int>()));

    CHECK(five.hashCode() == std::hash<int>{}(5));
    CHECK(absent.hashCode() == std::size_t{0});
}

TEST_CASE("Optional and std::optional convert in both directions")
{
    const std::optional<int> stdValue{5};

    const cppstream::Optional<int> wrapped = stdValue;
    CHECK(wrapped.get() == 5);

    const std::optional<int> unwrapped = wrapped;
    REQUIRE(unwrapped.has_value());
    CHECK(*unwrapped == 5);

    CHECK(wrapped.unwrap() == stdValue);
    CHECK(cppstream::Optional<int>::fromStd(stdValue).get() == 5);

    const auto takesStdOptional = [](std::optional<int> value) { return value.has_value(); };
    CHECK(takesStdOptional(wrapped));
    CHECK_FALSE(takesStdOptional(cppstream::Optional<int>::empty()));

    const std::optional<int> stdEmpty;
    CHECK(cppstream::Optional<int>(stdEmpty).isEmpty());
}

TEST_CASE("the Optional trait recognises instantiations")
{
    static_assert(cppstream::isOptional<cppstream::Optional<int>>);
    static_assert(cppstream::isOptional<cppstream::Optional<std::string>&>);
    static_assert(cppstream::isOptional<const cppstream::Optional<int>>);
    static_assert(!cppstream::isOptional<int>);
    static_assert(!cppstream::isOptional<std::optional<int>>);

    CHECK(true);
}
