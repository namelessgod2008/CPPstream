#include <doctest/doctest.h>

#include <cppstream/cppstream.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Person {
    std::string name;
    int age = 0;
};

}  // namespace

TEST_CASE("naturalOrder and reverseOrder follow Comparable's sign contract")
{
    const auto ascending = cppstream::Comparator<int>::naturalOrder();
    CHECK(ascending.compare(1, 2) < 0);
    CHECK(ascending.compare(2, 1) > 0);
    CHECK(ascending.compare(2, 2) == 0);

    const auto descending = cppstream::Comparator<std::string>::reverseOrder();
    CHECK(descending.compare("b", "a") < 0);
    CHECK(descending.compare("a", "b") > 0);
    CHECK(descending.compare("a", "a") == 0);
}

TEST_CASE("a Comparator can be built directly from a lambda")
{
    const cppstream::Comparator<int> byAbsoluteValue([](const int& left, const int& right) {
        const auto magnitude = [](int value) { return value < 0 ? -value : value; };
        if (magnitude(left) < magnitude(right)) {
            return -1;
        }
        if (magnitude(right) < magnitude(left)) {
            return 1;
        }
        return 0;
    });

    CHECK(byAbsoluteValue.compare(-7, 3) > 0);
    CHECK(byAbsoluteValue.compare(3, -7) < 0);
    CHECK(byAbsoluteValue.compare(-3, 3) == 0);
}

TEST_CASE("reversed flips the ordering and is its own inverse")
{
    const auto ascending = cppstream::Comparator<int>::naturalOrder();

    CHECK(ascending.reversed().compare(1, 2) > 0);
    CHECK(ascending.reversed().compare(2, 1) < 0);
    CHECK(ascending.reversed().reversed().compare(1, 2) < 0);
}

TEST_CASE("operator() makes a Comparator usable as a strict weak ordering")
{
    const auto comparator = cppstream::Comparator<int>::naturalOrder();

    std::vector<int> values{3, 1, 4, 1, 5, 9, 2, 6};
    std::sort(values.begin(), values.end(), comparator);

    const std::vector<int> expected{1, 1, 2, 3, 4, 5, 6, 9};
    REQUIRE(values.size() == expected.size());
    CHECK(std::equal(values.begin(), values.end(), expected.begin()));
}

TEST_CASE("comparing extracts a key and orders by its natural ordering")
{
    const auto byName = cppstream::Comparator<Person>::comparing(
        [](const Person& person) { return person.name; });

    const Person alice{"alice", 30};
    const Person bob{"bob", 25};

    CHECK(byName.compare(alice, bob) < 0);
    CHECK(byName.compare(bob, alice) > 0);
    CHECK(byName.compare(alice, Person{"alice", 99}) == 0);
}

TEST_CASE("comparing accepts an explicit key comparator")
{
    const auto oldestFirst = cppstream::Comparator<Person>::comparing(
        [](const Person& person) { return person.age; },
        cppstream::Comparator<int>::reverseOrder());

    CHECK(oldestFirst.compare(Person{"a", 40}, Person{"b", 30}) < 0);
    CHECK(oldestFirst.compare(Person{"a", 30}, Person{"b", 40}) > 0);
}

TEST_CASE("thenComparing only consults the tie-breaker when keys are equal")
{
    const auto byName = cppstream::Comparator<Person>::comparing(
        [](const Person& person) { return person.name; });
    const auto byNameThenAge = byName.thenComparing(
        [](const Person& person) { return person.age; });

    const Person alice30{"alice", 30};
    const Person alice40{"alice", 40};
    const Person bob25{"bob", 25};

    CHECK(byNameThenAge.compare(alice30, alice40) < 0);
    CHECK(byNameThenAge.compare(alice40, alice30) > 0);
    CHECK(byNameThenAge.compare(alice30, Person{"alice", 30}) == 0);

    // The name decides first, so age never overrides it.
    CHECK(byNameThenAge.compare(bob25, alice40) > 0);
}

TEST_CASE("thenComparing accepts an explicit key comparator and a full Comparator")
{
    const auto byName = cppstream::Comparator<Person>::comparing(
        [](const Person& person) { return person.name; });

    const auto nameThenAgeDescending = byName.thenComparing(
        [](const Person& person) { return person.age; },
        cppstream::Comparator<int>::reverseOrder());

    CHECK(nameThenAgeDescending.compare(Person{"alice", 30}, Person{"alice", 40}) > 0);
    CHECK(nameThenAgeDescending.compare(Person{"alice", 40}, Person{"alice", 30}) < 0);

    // A whole comparator is accepted directly, not just a key extractor.
    const auto byAge = cppstream::Comparator<Person>::comparing(
        [](const Person& person) { return person.age; });
    const auto nameThenAge = byName.thenComparing(byAge);
    CHECK(nameThenAge.compare(Person{"alice", 30}, Person{"alice", 40}) < 0);
}

TEST_CASE("nullsFirst and nullsLast order the null pointer explicitly")
{
    const int one = 1;
    const int two = 2;
    const int* const pointerToOne = &one;
    const int* const pointerToTwo = &two;

    const auto byPointedToValue = cppstream::Comparator<const int*>(
        [](const int* left, const int* right) {
            if (*left < *right) {
                return -1;
            }
            if (*right < *left) {
                return 1;
            }
            return 0;
        });

    const auto nullsFirst = cppstream::Comparator<const int*>::nullsFirst(byPointedToValue);
    CHECK(nullsFirst.compare(nullptr, pointerToOne) < 0);
    CHECK(nullsFirst.compare(pointerToOne, nullptr) > 0);
    CHECK(nullsFirst.compare(nullptr, nullptr) == 0);
    CHECK(nullsFirst.compare(pointerToOne, pointerToTwo) < 0);

    const auto nullsLast = cppstream::Comparator<const int*>::nullsLast(byPointedToValue);
    CHECK(nullsLast.compare(nullptr, pointerToOne) > 0);
    CHECK(nullsLast.compare(pointerToOne, nullptr) < 0);
    CHECK(nullsLast.compare(nullptr, nullptr) == 0);
    CHECK(nullsLast.compare(pointerToOne, pointerToTwo) < 0);
}
