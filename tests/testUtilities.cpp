#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <string>

TEST_CASE("Lists factories return frozen lists")
{
    const cppstream::ArrayList<int> empty = cppstream::Lists::empty<int>();
    CHECK(empty.isEmpty());
    CHECK(empty.isFrozen());

    cppstream::ArrayList<std::string> words =
        cppstream::Lists::of<std::string>(std::string("a"), std::string("b"), std::string("c"));
    CHECK(words.size() == 3);
    CHECK(words.get(2) == "c");
    CHECK(words.isFrozen());
    CHECK_THROWS_AS(words.add(std::string("d")), cppstream::UnsupportedOperationException);

    // Java's List.copyOf is an immutable snapshot, not a view.
    cppstream::ArrayList<int> source{1, 2, 3};
    const cppstream::ArrayList<int> copy = cppstream::Lists::copyOf<int>(source);
    source.add(4);
    CHECK(copy.size() == 3);
    CHECK(copy.isFrozen());

    // freeze() is a property of the value, so a copy of a frozen list is frozen
    // too -- `ArrayList<int> copy2 = copy;` picks the copy constructor, not the
    // Collection constructor, and a value copy behaves like its original.
    cppstream::ArrayList<int> copiedByValue = copy;
    CHECK(copiedByValue.isFrozen());
    CHECK_THROWS_AS(copiedByValue.add(4), cppstream::UnsupportedOperationException);

    // The escape hatch back to a mutable list is addAll into a fresh list, which
    // is how Java spells it as well: `new ArrayList<>(); addAll(x)`.
    cppstream::ArrayList<int> thawed;
    thawed.addAll(copy);
    thawed.add(4);
    CHECK(thawed.size() == 4);
    CHECK_FALSE(thawed.isFrozen());
}

TEST_CASE("Lists::unmodifiableList and reversed")
{
    cppstream::ArrayList<int> values{1, 2, 3};
    const cppstream::ArrayList<int> frozen =
        cppstream::Lists::unmodifiableList(std::move(values));
    CHECK(frozen.isFrozen());
    CHECK(frozen.size() == 3);

    const cppstream::ArrayList<int> backwards = cppstream::Lists::reversed(frozen);
    CHECK(backwards.get(0) == 3);
    CHECK(backwards.get(2) == 1);
    CHECK_FALSE(backwards.isFrozen());
}

TEST_CASE("Sets factories return frozen sets")
{
    CHECK(cppstream::Sets::empty<int>().isEmpty());

    cppstream::HashSet<std::string> letters =
        cppstream::Sets::of<std::string>(std::string("x"), std::string("y"), std::string("x"));
    CHECK(letters.size() == 2);
    CHECK(letters.isFrozen());
    CHECK_THROWS_AS(letters.add(std::string("z")), cppstream::UnsupportedOperationException);

    const cppstream::ArrayList<int> source{1, 2, 2, 3};
    const cppstream::HashSet<int> copy = cppstream::Sets::copyOf<int>(source);
    CHECK(copy.size() == 3);
    CHECK(copy.isFrozen());

    cppstream::HashSet<int> thawed(copy);
    CHECK(cppstream::Sets::unmodifiableSet(std::move(thawed)).isFrozen());
}

TEST_CASE("Maps factories return frozen maps")
{
    CHECK(cppstream::Maps::empty<std::string, int>().isEmpty());

    const cppstream::HashMap<std::string, int> ages =
        cppstream::Maps::of<std::string, int>({{std::string("ada"), 36}});
    CHECK(ages.size() == 1);
    CHECK(*ages.get(std::string("ada")) == 36);
    CHECK(ages.isFrozen());

    const cppstream::HashMap<std::string, int> entries = cppstream::Maps::ofEntries<std::string, int>(
        cppstream::Maps::entry(std::string("a"), 1), cppstream::Maps::entry(std::string("b"), 2));
    CHECK(entries.size() == 2);
    CHECK(entries.isFrozen());

    const cppstream::HashMap<std::string, int> copy = cppstream::Maps::copyOf<std::string, int>(entries);
    CHECK(copy.equals(entries));
    CHECK(copy.isFrozen());
}

TEST_CASE("Maps::entry is an immutable-ish pair with Java's equals and hashCode")
{
    const auto left = cppstream::Maps::entry(std::string("a"), 1);
    const auto right = cppstream::Maps::entry(std::string("a"), 1);

    CHECK(left.equals(right));
    CHECK(left.hashCode() == right.hashCode());
    CHECK(left.getKey() == "a");
    CHECK(left.getValue() == 1);

    // Snapshot semantics: setValue writes to this copy only.
    auto mutableEntry = left;
    CHECK(mutableEntry.setValue(2) == 1);
    CHECK(mutableEntry.getValue() == 2);
    CHECK(left.getValue() == 1);
}
