#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

template <class T>
std::vector<T> keysOf(const cppstream::Set<T>& view)
{
    return view.toArray();
}

}  // namespace

TEST_CASE("map views track the map instead of copying it")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);

    const auto keys = ages.keySet();
    const auto values = ages.values();
    const auto entries = ages.entrySet();

    CHECK(keys.size() == 1);
    CHECK(values.size() == 1);
    CHECK(entries.size() == 1);

    ages.put(std::string("alan"), 41);
    CHECK(keys.size() == 2);
    CHECK(values.size() == 2);
    CHECK(entries.size() == 2);

    ages.clear();
    CHECK(keys.isEmpty());
    CHECK(values.isEmpty());
    CHECK(entries.isEmpty());
}

TEST_CASE("keySet is a Set view whose remove deletes the mapping")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.put(std::string("alan"), 41);
    ages.put(std::string("grace"), 45);

    auto keys = ages.keySet();
    CHECK(keys.isSet());
    CHECK(keys.contains(std::string("ada")));
    CHECK_FALSE(keys.contains(std::string("nobody")));

    CHECK(keys.remove(std::string("ada")));
    CHECK_FALSE(ages.containsKey(std::string("ada")));
    CHECK_FALSE(keys.remove(std::string("ada")));

    // Iterator removal is the same operation.
    auto iterator = keys.iterator();
    CHECK(iterator->hasNext());
    static_cast<void>(iterator->next());
    iterator->remove();
    CHECK(ages.size() == 1);

    // A stale iterator fails fast rather than walking a window that moved.
    auto stale = keys.iterator();
    ages.put(std::string("kay"), 30);
    CHECK_THROWS_AS(stale->next(), cppstream::ConcurrentModificationException);
}

TEST_CASE("values is a Collection view whose remove targets the first match")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.put(std::string("alan"), 36);
    ages.put(std::string("grace"), 45);

    auto values = ages.values();
    CHECK(values.contains(45));
    CHECK_FALSE(values.contains(99));
    CHECK(values.stream().sorted(cppstream::Comparator<int>::naturalOrder()).toList().toArray() ==
        std::vector<int>({36, 36, 45}));

    CHECK(values.remove(36));
    CHECK(ages.size() == 2);
    CHECK_FALSE(values.remove(99));

    auto iterator = values.iterator();
    static_cast<void>(iterator->next());
    iterator->remove();
    CHECK(ages.size() == 1);
}

TEST_CASE("entrySet compares, hashes and removes by mapping")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.put(std::string("alan"), 41);

    using Entry = cppstream::HashMap<std::string, int>::Entry;

    auto entries = ages.entrySet();
    CHECK(entries.contains(Entry(std::string("ada"), 36)));
    CHECK_FALSE(entries.contains(Entry(std::string("ada"), 99)));
    CHECK_FALSE(entries.contains(Entry(std::string("nobody"), 36)));

    // Java's entry-set hash code is the map's own, by definition.
    CHECK(entries.hashCode() == ages.hashCode());

    // Removing an entry only takes effect when the value still matches.
    CHECK_FALSE(entries.remove(Entry(std::string("ada"), 99)));
    CHECK(ages.size() == 2);
    CHECK(entries.remove(Entry(std::string("ada"), 36)));
    CHECK(ages.size() == 1);

    // Two maps with the same mappings have equal entry sets.
    cppstream::HashMap<std::string, int> other;
    other.put(std::string("alan"), 41);
    CHECK(entries.equals(other.entrySet()));
}

TEST_CASE("map views can be read through a const map and through range-for")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.put(std::string("alan"), 41);

    const cppstream::HashMap<std::string, int>& readOnly = ages;

    std::vector<std::string> seenKeys;
    for (const std::string& key : readOnly.keySet()) {
        seenKeys.push_back(key);
    }
    std::ranges::sort(seenKeys);
    CHECK(seenKeys == std::vector<std::string>({"ada", "alan"}));

    std::size_t seenEntries = 0;
    for (const auto& entry : readOnly.entrySet()) {
        CHECK(entry.getValue() == *readOnly.get(entry.getKey()));
        ++seenEntries;
    }
    CHECK(seenEntries == 2);

    int sum = 0;
    for (const int value : readOnly.values()) {
        sum += value;
    }
    CHECK(sum == 77);

    // A view of a const map is read-only: iterator() cannot hand out mutable
    // references, and neither can any mutator.
    auto keys = readOnly.keySet();
    CHECK_THROWS_AS(static_cast<void>(keys.iterator()), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(keys.remove(std::string("ada"))),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(keys.clear()), cppstream::UnsupportedOperationException);

    auto values = readOnly.values();
    CHECK_THROWS_AS(static_cast<void>(values.add(1)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(values.clear()), cppstream::UnsupportedOperationException);

    auto entries = readOnly.entrySet();
    CHECK_THROWS_AS(static_cast<void>(entries.clear()), cppstream::UnsupportedOperationException);
    CHECK(entries.size() == 2);
}

TEST_CASE("a frozen map refuses writes through its views")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.freeze();

    auto keys = ages.keySet();
    CHECK_THROWS_AS(static_cast<void>(keys.remove(std::string("ada"))),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(keys.clear()), cppstream::UnsupportedOperationException);
    auto iterator = keys.iterator();
    static_cast<void>(iterator->next());
    CHECK_THROWS_AS(static_cast<void>(iterator->remove()),
        cppstream::UnsupportedOperationException);

    // Reading still works: a frozen map is only unwritable.
    CHECK(keys.size() == 1);
    CHECK(keys.contains(std::string("ada")));
}

TEST_CASE("TreeMap views come out in key order")
{
    cppstream::TreeMap<int, std::string> names;
    names.put(20, std::string("twenty"));
    names.put(10, std::string("ten"));
    names.put(30, std::string("thirty"));

    // Not guaranteed by the type -- our Set is not a SortedSet -- but it is what
    // the tree-backed snapshot produces, which is what Java's TreeMap.keySet does.
    CHECK(keysOf(names.keySet()) == std::vector<int>({10, 20, 30}));

    std::vector<int> entryKeys;
    for (const auto& entry : names.entrySet()) {
        entryKeys.push_back(entry.getKey());
    }
    CHECK(entryKeys == std::vector<int>({10, 20, 30}));

    CHECK(names.values().toArray() == std::vector<std::string>({"ten", "twenty", "thirty"}));
}

TEST_CASE("map views are ordinary containers")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.put(std::string("alan"), 41);

    CHECK(ages.keySet().stream().map([](const std::string& key) { return key.size(); }).sum() == 7);
    CHECK(ages.values().stream().sum() == 77);
    CHECK(ages.entrySet().stream().count() == 2);

    // toArray() is the documented way to get the snapshot back when plain
    // std::sort-friendly storage is what a caller actually wanted.
    const std::vector<cppstream::HashMap<std::string, int>::Entry> snapshot =
        ages.entrySet().toArray();
    CHECK(snapshot.size() == 2);
}
