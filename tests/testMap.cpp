#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <cstddef>
#include <string>
#include <vector>

TEST_CASE("put returns the previous value as an Optional")
{
    cppstream::HashMap<std::string, int> ages;

    // Java's null-as-absent becomes an empty Optional.
    CHECK(ages.put(std::string("ada"), 36).isEmpty());
    CHECK(ages.put(std::string("ada"), 37).get() == 36);

    CHECK(ages.size() == 1);
    CHECK(*ages.get(std::string("ada")) == 37);
    CHECK(ages.get(std::string("missing")) == nullptr);
}

TEST_CASE("get hands out a writable pointer for existing keys")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);

    int* value = ages.get(std::string("ada"));
    REQUIRE(value != nullptr);
    *value = 40;
    CHECK(*ages.get(std::string("ada")) == 40);

    const auto& view = ages;
    CHECK(*view.get(std::string("ada")) == 40);
    CHECK(view.get(std::string("nobody")) == nullptr);
}

TEST_CASE("putIfAbsent leaves the existing mapping alone")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);

    CHECK(ages.putIfAbsent(std::string("ada"), 99).get() == 36);
    CHECK(*ages.get(std::string("ada")) == 36);

    CHECK(ages.putIfAbsent(std::string("alan"), 41).isEmpty());
    CHECK(*ages.get(std::string("alan")) == 41);
}

TEST_CASE("getOrDefault and remove")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);

    CHECK(ages.getOrDefault(std::string("ada"), 0) == 36);
    CHECK(ages.getOrDefault(std::string("nobody"), -1) == -1);

    CHECK(ages.remove(std::string("ada")).get() == 36);
    CHECK(ages.remove(std::string("ada")).isEmpty());
    CHECK(ages.isEmpty());
}

TEST_CASE("computeIfAbsent, computeIfPresent and merge")
{
    cppstream::HashMap<std::string, int> counts;

    // computeIfAbsent hands back a copy of the installed value, exactly as Java's
    // does, so bind it by value rather than holding a reference into a temporary.
    const int installed = counts
                              .computeIfAbsent(std::string("a"),
                                  [](const std::string&) { return cppstream::Optional<int>::of(1); })
                              .get();
    CHECK(installed == 1);

    // A mapping function that returns an empty Optional declines to install, which
    // is Java's `return null`.
    counts.computeIfAbsent(std::string("b"),
        [](const std::string&) { return cppstream::Optional<int>::empty(); });
    CHECK_FALSE(counts.containsKey(std::string("b")));

    counts.computeIfPresent(std::string("a"),
        [](const std::string&, const int& current) { return cppstream::Optional<int>::of(current + 10); });
    CHECK(*counts.get(std::string("a")) == 11);

    // Returning empty removes the entry, as Java does.
    counts.computeIfPresent(std::string("a"),
        [](const std::string&, const int&) { return cppstream::Optional<int>::empty(); });
    CHECK_FALSE(counts.containsKey(std::string("a")));

    CHECK(counts.merge(std::string("c"), 5, [](const int& left, const int& right) { return left + right; })
              .get() == 5);
    CHECK(counts.merge(std::string("c"), 5, [](const int& left, const int& right) { return left + right; })
              .get() == 10);
    CHECK(counts.merge(std::string("c"), 5,
              [](const int&, const int&) { return cppstream::Optional<int>::empty(); })
              .isEmpty());
    CHECK_FALSE(counts.containsKey(std::string("c")));
}

TEST_CASE("keySet, values and entrySet are live views")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.put(std::string("alan"), 41);

    auto keys = ages.keySet();
    CHECK(keys.size() == 2);
    CHECK(keys.contains(std::string("ada")));
    CHECK(keys.isSet());

    auto values = ages.values();
    CHECK(values.size() == 2);
    CHECK(values.contains(41));
    CHECK_FALSE(values.contains(99));

    auto entries = ages.entrySet();
    CHECK(entries.size() == 2);

    // Live, not a snapshot: the map moved on and the views moved with it.
    ages.put(std::string("grace"), 45);
    CHECK(keys.size() == 3);
    CHECK(values.size() == 3);
    CHECK(entries.size() == 3);
    CHECK(keys.contains(std::string("grace")));

    // remove() on a view is a map operation, exactly as in Java.
    CHECK(keys.remove(std::string("grace")));
    CHECK(ages.size() == 2);
    CHECK_FALSE(keys.remove(std::string("nobody")));

    // add() is not a map operation, so it throws, exactly as in Java.
    CHECK_THROWS_AS(static_cast<void>(keys.add(std::string("late"))),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(values.add(1)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(entries.add(cppstream::HashMap<std::string, int>::Entry(
                        std::string("late"), 1))),
        cppstream::UnsupportedOperationException);
}

TEST_CASE("Entry exposes getKey, getValue and an independent setValue")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);

    auto entries = ages.entrySet();
    REQUIRE(entries.size() == 1);

    auto iterator = entries.iterator();
    REQUIRE(iterator->hasNext());
    cppstream::HashMap<std::string, int>::Entry& entry = iterator->next();
    CHECK(entry.getKey() == "ada");
    CHECK(entry.getValue() == 36);

    // setValue writes to the copy the iterator handed out. Map::Entry is a value
    // type here, unlike Java's live node, and that is the one remaining gap in
    // the entry view (divergence 12).
    CHECK(entry.setValue(99) == 36);
    CHECK(entry.getValue() == 99);
    CHECK(*ages.get(std::string("ada")) == 36);
}

TEST_CASE("Map equality and hashCode follow AbstractMap")
{
    cppstream::HashMap<std::string, int> left;
    left.put(std::string("ada"), 36);
    left.put(std::string("alan"), 41);

    cppstream::TreeMap<std::string, int> reordered;
    reordered.put(std::string("alan"), 41);
    reordered.put(std::string("ada"), 36);

    CHECK(left.equals(reordered));
    CHECK(reordered.equals(left));
    CHECK(left.hashCode() == reordered.hashCode());

    reordered.put(std::string("grace"), 45);
    CHECK_FALSE(left.equals(reordered));
}

TEST_CASE("forEach hands out writable values")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.put(std::string("alan"), 41);

    ages.forEach([](const std::string&, int& value) { value += 1; });

    CHECK(*ages.get(std::string("ada")) == 37);
    CHECK(*ages.get(std::string("alan")) == 42);
}

TEST_CASE("frozen maps refuse every mutation path")
{
    cppstream::HashMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.freeze();

    CHECK(ages.isFrozen());
    CHECK_THROWS_AS(static_cast<void>(ages.put(std::string("alan"), 41)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(ages.putIfAbsent(std::string("alan"), 41)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(ages.remove(std::string("ada"))),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(ages.clear(), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(ages.computeIfAbsent(std::string("alan"),
                      [](const std::string&) { return cppstream::Optional<int>::of(1); })),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(ages.merge(std::string("ada"), 1,
                      [](const int& left, const int& right) { return left + right; })),
        cppstream::UnsupportedOperationException);

    CHECK(ages.size() == 1);
}

TEST_CASE("TreeMap iterates in key order and answers navigable queries")
{
    cppstream::TreeMap<int, std::string> names;
    names.put(30, std::string("thirty"));
    names.put(10, std::string("ten"));
    names.put(20, std::string("twenty"));

    CHECK(names.firstKey() == 10);
    CHECK(names.lastKey() == 30);
    CHECK(names.firstEntry().get().getKey() == 10);
    CHECK(names.lastEntry().get().getKey() == 30);

    CHECK(names.floorKey(25).get() == 20);
    CHECK(names.ceilingKey(25).get() == 30);
    CHECK(names.lowerKey(20).get() == 10);
    CHECK(names.higherKey(20).get() == 30);

    CHECK(names.firstEntry().get().getValue() == "ten");
    CHECK(names.subMap(15, 35).size() == 2);
    CHECK(names.headMap(25).size() == 2);
    CHECK(names.tailMap(20).size() == 2);

    const cppstream::Optional<cppstream::TreeMap<int, std::string>::Entry> polled = names.pollFirstEntry();
    CHECK(polled.get().getKey() == 10);
    CHECK(names.size() == 2);
    CHECK(names.pollLastEntry().get().getKey() == 30);
    CHECK(names.size() == 1);

    CHECK_THROWS_AS(static_cast<void>(cppstream::TreeMap<int, int>{}.firstKey()),
        cppstream::NoSuchElementException);

    // keySet() is a live Set view. Our Set has no SortedSet refinement, so key
    // order is not part of the type -- but it is what the view actually produces,
    // because the snapshot is taken from the tree.
    const auto remaining = names.keySet();
    CHECK(remaining.size() == 1);
    CHECK(remaining.toArray() == std::vector<int>{20});
}

TEST_CASE("maps reject null keys and values for pointer payloads")
{
    cppstream::HashMap<int*, int> pointers;

    CHECK_THROWS_AS(static_cast<void>(pointers.put(static_cast<int*>(nullptr), 1)),
        cppstream::NullPointerException);
    CHECK_FALSE(pointers.containsKey(static_cast<int*>(nullptr)));
}

TEST_CASE("putAll copies every mapping")
{
    cppstream::HashMap<std::string, int> source;
    source.put(std::string("ada"), 36);
    source.put(std::string("alan"), 41);

    cppstream::TreeMap<std::string, int> destination;
    destination.putAll(source);

    CHECK(destination.size() == 2);
    CHECK(*destination.get(std::string("ada")) == 36);
}

TEST_CASE("TreeMap.descendingMap is a live view of the same map")
{
    cppstream::TreeMap<std::string, int> ages{{"amy", 31}, {"bob", 24}, {"cid", 45}};

    auto descending = ages.descendingMap();

    CHECK(descending.size() == 3);
    CHECK(descending.firstKey() == "cid");
    CHECK(descending.lastKey() == "amy");

    std::vector<std::string> keys;
    for (const auto& entry : descending.entrySet()) {
        keys.push_back(entry.getKey());
    }
    CHECK(keys == std::vector<std::string>{"cid", "bob", "amy"});

    // A write through the view lands in the map...
    static_cast<void>(descending.put("dan", 30));
    CHECK(*ages.get(std::string("dan")) == 30);

    // ...and a change to the map shows up through the view, which is also why the
    // inherited keySet()/values()/entrySet() come out in the view's order.
    static_cast<void>(ages.put("eve", 20));
    CHECK(descending.size() == 5);
    CHECK(descending.firstKey() == "eve");
    CHECK(descending.lastKey() == "amy");
    CHECK(descending.modCount() == ages.modCount());

    std::vector<int> values;
    for (const auto& value : descending.values()) {
        values.push_back(value);
    }
    CHECK(values == std::vector<int>{20, 30, 45, 24, 31});

    // Flipping twice gives the ascending walk back, as Java's does.
    CHECK(descending.descendingMap().firstKey() == "amy");
    CHECK(descending.descendingMap().keySet().toArray()
        == std::vector<std::string>{"amy", "bob", "cid", "dan", "eve"});

    // Java: DescendingSubMap.comparator() is reverseOrder(m.comparator()).
    CHECK(descending.comparator().compare(std::string("a"), std::string("b")) > 0);
}

TEST_CASE("TreeMap.Descending is still an independent reversed copy")
{
    const cppstream::TreeMap<std::string, int> ages{{"amy", 31}, {"bob", 24}, {"cid", 45}};

    cppstream::TreeMap<std::string, int>::Descending copy;
    copy.putAll(ages);
    CHECK(copy.size() == 3);
    CHECK(copy.firstKey() == "cid");
    static_cast<void>(copy.put("dan", 30));
    CHECK(copy.size() == 4);
    CHECK(ages.size() == 3);  // the copy is independent storage
}

TEST_CASE("TreeMap descending views navigate and flip their range methods")
{
    cppstream::TreeMap<int, int> ages{{10, 1}, {20, 2}, {30, 3}, {40, 4}, {50, 5}};
    auto descending = ages.descendingMap();

    CHECK(descending.floorKey(30).get() == 30);
    CHECK(descending.lowerKey(30).get() == 40);
    CHECK(descending.ceilingKey(30).get() == 30);
    CHECK(descending.higherKey(30).get() == 20);
    CHECK(descending.floorKey(5).get() == 10);
    CHECK(descending.ceilingKey(100).get() == 50);

    CHECK(descending.firstEntry().get().getKey() == 50);
    CHECK(descending.lastEntry().get().getKey() == 10);
    CHECK(descending.pollFirstEntry().get().getKey() == 50);
    CHECK(descending.pollLastEntry().get().getKey() == 10);
    CHECK(ages.keySet().toArray() == std::vector<int>{20, 30, 40});

    CHECK(descending.headMap(30, true).keySet().toArray() == std::vector<int>{40, 30});
    CHECK(descending.tailMap(30, true).keySet().toArray() == std::vector<int>{30, 20});
    CHECK(descending.subMap(40, true, 20, false).keySet().toArray() == std::vector<int>{40, 30});

    // The backwards window is rejected with Java's message: "from" is the upper end
    // of a descending window, so 20 below 40 is the backwards one.
    CHECK_THROWS_AS(static_cast<void>(descending.subMap(20, 40)),
        cppstream::IllegalArgumentException);

    // On a *bounded* descending window an out-of-window fence is rejected, and the
    // message names the descending view's own parameter rather than the ascending
    // role the bound plays inside. Verified against a JDK.
    auto bounded = ages.descendingMap().subMap(40, true, 20, true);
    CHECK(bounded.keySet().toArray() == std::vector<int>{40, 30, 20});
    CHECK_THROWS_AS(static_cast<void>(bounded.headMap(100)), cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(bounded.tailMap(5)), cppstream::IllegalArgumentException);
}

TEST_CASE("TreeMap.navigableKeySet and descendingKeySet are live NavigableSet views")
{
    cppstream::TreeMap<int, int> ages{{10, 1}, {20, 2}, {30, 3}};

    auto keys = ages.navigableKeySet();
    CHECK(keys.toArray() == std::vector<int>{10, 20, 30});
    CHECK(keys.first() == 10);
    CHECK(keys.last() == 30);
    CHECK(keys.floor(25).get() == 20);
    CHECK(keys.ceiling(25).get() == 30);

    // Removing a key takes the mapping with it, because that is what removing a key
    // from a map means; adding one is not something a key set can do.
    CHECK(keys.remove(20));
    CHECK(ages.size() == 2);
    CHECK_THROWS_AS(static_cast<void>(keys.add(40)), cppstream::UnsupportedOperationException);

    // subSet/headSet/tailSet are windows on the map, so writes through them land.
    CHECK(keys.subSet(30, true, 50, true).toArray() == std::vector<int>{30});
    CHECK(keys.headSet(30, true).toArray() == std::vector<int>{10, 30});
    CHECK(keys.tailSet(30, false).toArray() == std::vector<int>{});

    // descendingKeySet() is the same keys the other way round, and descendingSet()
    // on a key set is the same thing.
    auto descendingKeys = ages.descendingKeySet();
    CHECK(descendingKeys.toArray() == std::vector<int>{30, 10});
    CHECK(descendingKeys.first() == 30);
    CHECK(keys.descendingSet().toArray() == std::vector<int>{30, 10});
    CHECK(descendingKeys.descendingSet().toArray() == std::vector<int>{10, 30});
    CHECK(descendingKeys.comparator().compare(1, 2) > 0);
    // floor() is "greatest key <= under *this* ordering", so on a descending key
    // set it is the ascending ceiling: 30 is the smallest key >= 25.
    CHECK(descendingKeys.floor(25).get() == 30);
    CHECK(descendingKeys.ceiling(25).get() == 10);

    // The iterator removes through the window, fail-fast on the backing map.
    auto iterator = descendingKeys.iterator();
    CHECK(iterator->next() == 30);
    iterator->remove();
    CHECK(ages.size() == 1);
    CHECK(ages.keySet().toArray() == std::vector<int>{10});

    // A key set of a window is bounded, and says so with Java's messages.
    auto window = ages.navigableKeySet().subSet(5, false, 20, true);
    CHECK_THROWS_AS(static_cast<void>(window.headSet(50, true)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.tailSet(1, true)),
        cppstream::IllegalArgumentException);
}

TEST_CASE("a descending map view taken from a const map is read-only")
{
    const cppstream::TreeMap<int, int> ages{{1, 10}, {2, 20}};
    auto descending = ages.descendingMap();

    CHECK(descending.firstKey() == 2);
    CHECK_THROWS_AS(static_cast<void>(descending.put(3, 30)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(ages.navigableKeySet().add(3)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(ages.navigableKeySet().remove(1)),
        cppstream::UnsupportedOperationException);

    // Iterating a read-only window is still fine, and so is a sub-window.
    CHECK(descending.subMap(2, true, 1, false).keySet().toArray() == std::vector<int>{2});
}
