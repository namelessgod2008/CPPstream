#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Live range views: TreeSet.subSet/headSet/tailSet and TreeMap.subMap/headMap/
// tailMap.
//
// Every expectation below was produced by running the equivalent program against
// java.util.TreeSet / java.util.TreeMap on a JDK: where Java throws, these throw;
// where Java clamps a lookup to the window instead of answering empty, these
// clamp the same way. See DESIGN.md section 9 for the harness.
// ---------------------------------------------------------------------------

TEST_CASE("TreeSet range views are live windows, not copies")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, 40);  // [20, 40)

    CHECK(window.size() == 2);
    CHECK(window.toArray() == std::vector<int>{20, 30});

    // A change to the tree is visible through the window...
    CHECK(values.add(25));
    CHECK(window.toArray() == std::vector<int>{20, 25, 30});

    // ...and a write through the window lands in the tree.
    CHECK(window.add(35));
    CHECK(values.toArray() == std::vector<int>{10, 20, 25, 30, 35, 40, 50});

    CHECK(window.remove(30));
    CHECK_FALSE(window.remove(10));  // outside the window, so simply not there
    CHECK(values.toArray() == std::vector<int>{10, 20, 25, 35, 40, 50});

    CHECK(window.contains(25));
    CHECK_FALSE(window.contains(10));
    CHECK_FALSE(window.contains(40));  // the upper fence is exclusive
    CHECK_FALSE(window.add(25));       // still a Set
    CHECK(window.comparator()(1, 2));  // std::less, as std::set uses
    CHECK(window.modCount() == values.modCount());
}

TEST_CASE("TreeSet range views reject writes outside the window")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, 40);

    CHECK_THROWS_AS(static_cast<void>(window.add(45)), cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.add(15)), cppstream::IllegalArgumentException);
    CHECK(window.add(21));
    CHECK(values.contains(21));

    // addAll stops where Java's stops: the element before the offending one has
    // already been added, because addAll is just add() in a loop.
    const cppstream::ArrayList<int> batch{24, 45};
    CHECK_THROWS_AS(static_cast<void>(window.addAll(batch)), cppstream::IllegalArgumentException);
    CHECK(values.contains(21));
    CHECK(values.contains(24));
    CHECK_FALSE(values.contains(45));
}

TEST_CASE("TreeSet range views clear only their window")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, 40);

    window.clear();
    CHECK(values.toArray() == std::vector<int>{10, 40, 50});
    CHECK(window.isEmpty());
    CHECK(window.size() == 0);
}

TEST_CASE("TreeSet range views navigate their own window")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, 40);

    CHECK(window.first() == 20);
    CHECK(window.last() == 30);

    CHECK(window.pollFirst().get() == 20);
    CHECK(window.pollLast().get() == 30);
    CHECK(values.toArray() == std::vector<int>{10, 40, 50});

    auto empty = values.headSet(10);
    CHECK(empty.isEmpty());
    CHECK(empty.size() == 0);
    CHECK(empty.pollFirst().isEmpty());
    CHECK(empty.pollLast().isEmpty());
    CHECK_THROWS_AS(static_cast<void>(empty.first()), cppstream::NoSuchElementException);
    CHECK_THROWS_AS(static_cast<void>(empty.last()), cppstream::NoSuchElementException);
}

TEST_CASE("TreeSet range view lookups clamp to the window, as Java does")
{
    const cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    const auto window = values.subSet(20, 40);  // [20, 40)

    CHECK(window.floor(10).isEmpty());
    CHECK(window.floor(35).get() == 30);
    CHECK(window.floor(40).get() == 30);   // 40 is the exclusive fence
    CHECK(window.floor(100).get() == 30);  // clamped, not empty

    CHECK(window.lower(10).isEmpty());
    CHECK(window.lower(20).isEmpty());
    CHECK(window.lower(35).get() == 30);
    CHECK(window.lower(100).get() == 30);

    CHECK(window.ceiling(10).get() == 20);  // clamped up
    CHECK(window.ceiling(25).get() == 30);
    CHECK(window.ceiling(100).isEmpty());

    CHECK(window.higher(10).get() == 20);
    CHECK(window.higher(20).get() == 30);
    CHECK(window.higher(35).isEmpty());
    CHECK(window.higher(100).isEmpty());
}

TEST_CASE("TreeSet range view iterators walk the window and fail fast")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, 40);

    std::vector<int> walked;
    for (int value : window) {
        walked.push_back(value);
    }
    CHECK(walked == std::vector<int>{20, 30});

    auto iterator = window.iterator();
    CHECK(iterator->next() == 20);
    iterator->remove();
    CHECK(values.toArray() == std::vector<int>{10, 30, 40, 50});
    CHECK(iterator->next() == 30);  // the cursor survived the removal
    CHECK_FALSE(iterator->hasNext());

    auto stale = window.iterator();
    CHECK(stale->next() == 30);
    values.add(45);
    CHECK_THROWS_AS(stale->next(), cppstream::ConcurrentModificationException);

    // The read-only path yields const references and refuses to remove.
    const cppstream::TreeSet<int>& readOnly = values;
    const auto constWindow = readOnly.subSet(20, 40);
    auto constIterator = constWindow.constIterator();
    CHECK(constIterator->next() == 30);
    CHECK_THROWS_AS(constIterator->remove(), cppstream::UnsupportedOperationException);
}

TEST_CASE("TreeSet range views inherit Java's fence rules")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, 40);  // [20, 40)

    // A window inside a window keeps the fence it does not replace.
    CHECK(window.subSet(20, true, 30, false).toArray() == std::vector<int>{20});
    CHECK(window.subSet(20, false, 30, false).isEmpty());
    CHECK(window.subSet(21, true, 30, false).isEmpty());
    CHECK(window.subSet(21, true, 39, true).toArray() == std::vector<int>{30});
    CHECK(window.headSet(40, false).toArray() == std::vector<int>{20, 30});
    CHECK(window.headSet(39, true).toArray() == std::vector<int>{20, 30});
    CHECK(window.headSet(20, false).isEmpty());
    CHECK(window.tailSet(20, true).toArray() == std::vector<int>{20, 30});
    CHECK(window.tailSet(20, false).toArray() == std::vector<int>{30});

    // Widening is rejected: beyond a fence, or equal to an exclusive one while
    // claiming to include it.
    CHECK_THROWS_AS(static_cast<void>(window.headSet(40, true)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.headSet(10, false)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.tailSet(45, true)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.subSet(20, false, 40, true)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.subSet(35, true, 25, true)),
        cppstream::IllegalArgumentException);

    // An inclusive upper fence accepts its own bound; an exclusive lower one
    // rejects a bound that would include it.
    const auto inclusive = values.subSet(20, true, 40, true);
    CHECK(inclusive.headSet(40, true).toArray() == std::vector<int>{20, 30, 40});
    CHECK(inclusive.headSet(40, false).toArray() == std::vector<int>{20, 30});
    CHECK(inclusive.tailSet(40, true).toArray() == std::vector<int>{40});

    const auto exclusive = values.subSet(20, false, 40, false);
    CHECK_THROWS_AS(static_cast<void>(exclusive.tailSet(20, true)),
        cppstream::IllegalArgumentException);
    CHECK(exclusive.tailSet(20, false).toArray() == std::vector<int>{30});

    // The container itself rejects a window that runs backwards.
    CHECK_THROWS_AS(static_cast<void>(values.subSet(40, 20)),
        cppstream::IllegalArgumentException);
}

TEST_CASE("range views taken from a const container are read-only")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    const cppstream::TreeSet<int>& readOnly = values;
    auto window = readOnly.subSet(20, 40);

    CHECK(window.size() == 2);
    CHECK(window.contains(20));
    CHECK(window.first() == 20);
    CHECK(window.last() == 30);
    CHECK(window.floor(35).get() == 30);
    CHECK(window.toArray() == std::vector<int>{20, 30});

    CHECK_THROWS_AS(static_cast<void>(window.add(25)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window.remove(20)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(window.clear(), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window.iterator()), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window.pollFirst()),
        cppstream::UnsupportedOperationException);

    // A window onto a read-only window is read-only rather than an error, which is
    // what Collections.unmodifiableNavigableSet(set).subSet(...) does.
    auto narrow = window.subSet(21, true, 31, false);
    CHECK(narrow.toArray() == std::vector<int>{30});
    CHECK_THROWS_AS(static_cast<void>(narrow.add(25)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(narrow.iterator()), cppstream::UnsupportedOperationException);

    // Through a const reference the same call is const, and read-only too.
    const cppstream::TreeSetRangeView<int, std::less<int>>& readView = window;
    auto constNarrow = readView.tailSet(30, true);  // the [20, 40) fence is kept
    CHECK(constNarrow.toArray() == std::vector<int>{30});
    CHECK_THROWS_AS(static_cast<void>(constNarrow.add(35)),
        cppstream::UnsupportedOperationException);
    CHECK(values.size() == 5);
}

TEST_CASE("TreeSet range views compare as sets and stream")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    const auto window = values.subSet(20, 40);

    const cppstream::TreeSet<int> sameWindow{20, 30};
    CHECK(window.isSet());
    CHECK(window.equals(sameWindow));
    CHECK(sameWindow.equals(window));
    CHECK(window.hashCode() == sameWindow.hashCode());
    CHECK(window.containsAll(sameWindow));

    CHECK(window.stream().count() == 2);
    CHECK(window.stream().map([](int value) { return value * 2; }).toList().toArray() ==
        std::vector<int>{40, 60});
}

// ---------------------------------------------------------------------------
// TreeMap
// ---------------------------------------------------------------------------

TEST_CASE("TreeMap range views are live windows, not copies")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }

    auto window = values.subMap(20, 40);  // [20, 40)
    CHECK(window.size() == 2);
    CHECK(window.keySet().toArray() == std::vector<int>{20, 30});

    // A change to the map is visible through the window...
    static_cast<void>(values.put(25, "v25"));
    CHECK(window.size() == 3);
    CHECK(window.containsKey(25));

    // ...and a write through the window lands in the map.
    static_cast<void>(window.put(35, "v35"));
    CHECK(*values.get(35) == "v35");

    // values() and entrySet() are windows onto the window.
    CHECK(window.values().toArray() == std::vector<std::string>{"v20", "v25", "v30", "v35"});
    CHECK(window.keySet().toArray() == std::vector<int>{20, 25, 30, 35});
    CHECK(window.entrySet().size() == 4);

    CHECK(window.remove(30).get() == "v30");
    CHECK_FALSE(window.remove(10).isPresent());   // outside the window
    CHECK(values.containsKey(30) == false);
    CHECK(values.containsKey(10));

    CHECK(window.get(25) != nullptr);
    CHECK(window.get(10) == nullptr);
    CHECK_FALSE(window.containsKey(40));          // the upper fence is exclusive
    CHECK_FALSE(window.containsValue("v10"));
    CHECK(window.containsValue("v25"));

    // forEach goes through the filtered visitEntries, so the window is what is
    // visited -- and a write to the value lands in the map.
    window.forEach([](const int&, std::string& value) { value += "!"; });
    CHECK(*values.get(20) == "v20!");
    CHECK(*values.get(10) == "v10");
    CHECK(window.comparator()(1, 2));
}

TEST_CASE("TreeMap range views reject writes outside the window")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }
    auto window = values.subMap(20, 40);

    CHECK_THROWS_AS(static_cast<void>(window.put(50, "x")), cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.putIfAbsent(50, "x")),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.computeIfAbsent(50, [](const int&) {
        return cppstream::Optional<std::string>::of("x");
    })), cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.merge(50, "x",
        [](const std::string& left, const std::string& right) {
            return cppstream::Optional<std::string>::of(left + right);
        })), cppstream::IllegalArgumentException);

    // computeIfPresent answers empty for a key outside the window rather than
    // throwing -- and, as in Java, the mapping outside the window is untouched.
    CHECK(window.computeIfPresent(50, [](const int&, const std::string& value) {
        return cppstream::Optional<std::string>::of(value + "?");
    }).isEmpty());
    CHECK(*values.get(50) == "v50");
    CHECK(*values.get(10) == "v10");
    CHECK(window.computeIfPresent(30, [](const int&, const std::string& value) {
        return cppstream::Optional<std::string>::of(value + "?");
    }).get() == "v30?");
    CHECK(*values.get(30) == "v30?");

    // putIfAbsent inside the window still reports the existing value.
    CHECK(window.putIfAbsent(30, "x").get() == "v30?");
    CHECK(*values.get(30) == "v30?");
}

TEST_CASE("TreeMap range views clear only their window")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }
    auto window = values.subMap(20, 40);

    window.clear();
    CHECK(values.size() == 3);
    CHECK(values.keySet().toArray() == std::vector<int>{10, 40, 50});
    CHECK(window.isEmpty());
}

TEST_CASE("TreeMap range views navigate their own window")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }
    auto window = values.subMap(20, 40);

    CHECK(window.firstKey() == 20);
    CHECK(window.lastKey() == 30);
    CHECK(window.firstEntry().get().getKey() == 20);
    CHECK(window.lastEntry().get().getValue() == "v30");

    CHECK(window.pollFirstEntry().get().getKey() == 20);
    CHECK(window.pollLastEntry().get().getKey() == 30);
    CHECK(values.keySet().toArray() == std::vector<int>{10, 40, 50});

    auto empty = values.headMap(10);
    CHECK(empty.isEmpty());
    CHECK(empty.pollFirstEntry().isEmpty());
    CHECK(empty.firstEntry().isEmpty());
    CHECK_THROWS_AS(static_cast<void>(empty.firstKey()), cppstream::NoSuchElementException);
    CHECK_THROWS_AS(static_cast<void>(empty.lastKey()), cppstream::NoSuchElementException);
}

TEST_CASE("TreeMap range view lookups clamp to the window, as Java does")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }
    const auto window = values.subMap(20, 40);  // [20, 40)

    CHECK(window.floorKey(10).isEmpty());
    CHECK(window.floorKey(35).get() == 30);
    CHECK(window.floorKey(40).get() == 30);
    CHECK(window.floorKey(100).get() == 30);

    CHECK(window.lowerKey(10).isEmpty());
    CHECK(window.lowerKey(20).isEmpty());
    CHECK(window.lowerKey(35).get() == 30);
    CHECK(window.lowerKey(100).get() == 30);

    CHECK(window.ceilingKey(10).get() == 20);
    CHECK(window.ceilingKey(25).get() == 30);
    CHECK(window.ceilingKey(100).isEmpty());

    CHECK(window.higherKey(10).get() == 20);
    CHECK(window.higherKey(20).get() == 30);
    CHECK(window.higherKey(35).isEmpty());
    CHECK(window.higherKey(100).isEmpty());
}

TEST_CASE("map views built on a range view fail fast against the backing map")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }
    auto window = values.subMap(20, 40);

    // The view answers with the backing map's counter, which is what makes these
    // iterators fail fast on a change to the map rather than to the view.
    CHECK(window.modCount() == values.modCount());

    auto keys = window.keySet();
    auto iterator = keys.iterator();
    CHECK(iterator->next() == 20);
    static_cast<void>(values.put(15, "v15"));  // outside the window, still structural
    CHECK_THROWS_AS(iterator->next(), cppstream::ConcurrentModificationException);

    // entrySet() on the view is filtered too, so it starts at 20 -- 15 is outside
    // the window even though it is in the map.
    auto entries = window.entrySet();
    auto entryIterator = entries.iterator();
    CHECK(entryIterator->next().getKey() == 20);
    static_cast<void>(values.put(14, "v14"));
    CHECK_THROWS_AS(entryIterator->next(), cppstream::ConcurrentModificationException);
}

TEST_CASE("TreeMap range views inherit Java's fence rules")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }
    auto window = values.subMap(20, 40);  // [20, 40)

    CHECK(window.subMap(25, true, 35, true).keySet().toArray() == std::vector<int>{30});
    CHECK(window.headMap(30, true).keySet().toArray() == std::vector<int>{20, 30});
    CHECK(window.tailMap(30, true).keySet().toArray() == std::vector<int>{30});
    CHECK(window.tailMap(20, false).keySet().toArray() == std::vector<int>{30});

    CHECK_THROWS_AS(static_cast<void>(window.subMap(10, true, 30, false)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.headMap(50, false)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.headMap(40, true)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(window.tailMap(45, true)),
        cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(static_cast<void>(values.subMap(40, 20)),
        cppstream::IllegalArgumentException);
}

TEST_CASE("range views taken from a const map are read-only")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }
    const cppstream::TreeMap<int, std::string>& readOnly = values;
    auto window = readOnly.subMap(20, 40);

    CHECK(window.size() == 2);
    CHECK(window.containsKey(20));
    CHECK(window.get(10) == nullptr);
    CHECK(window.firstKey() == 20);

    // Reads that hand out a V* come from the const overload: the mutable one is
    // for writing, and this view has nothing writable to hand out.
    const cppstream::TreeMapRangeView<int, std::string, std::less<int>>& readView = window;
    CHECK(readView.get(20) != nullptr);
    CHECK(readView.get(10) == nullptr);
    CHECK_THROWS_AS(static_cast<void>(window.get(20)),
        cppstream::UnsupportedOperationException);
    CHECK(window.keySet().toArray() == std::vector<int>{20, 30});

    CHECK_THROWS_AS(static_cast<void>(window.put(25, "x")),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window.remove(20)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(window.clear(), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window.computeIfPresent(20,
        [](const int&, const std::string& value) {
            return cppstream::Optional<std::string>::of(value);
        })), cppstream::UnsupportedOperationException);
    // A window onto a read-only window is read-only rather than an error, which is
    // what Collections.unmodifiableNavigableMap(map).subMap(...) does.
    auto narrow = window.subMap(21, true, 31, false);
    CHECK(narrow.keySet().toArray() == std::vector<int>{30});
    CHECK_THROWS_AS(static_cast<void>(narrow.put(25, "x")),
        cppstream::UnsupportedOperationException);

    auto constNarrow = readView.tailMap(30, true);  // the [20, 40) fence is kept
    CHECK(constNarrow.keySet().toArray() == std::vector<int>{30});
    CHECK_THROWS_AS(static_cast<void>(constNarrow.remove(30)),
        cppstream::UnsupportedOperationException);
    CHECK(values.size() == 5);
}

TEST_CASE("TreeMap range views compare as maps")
{
    cppstream::TreeMap<int, std::string> values;
    for (int key = 10; key <= 50; key += 10) {
        static_cast<void>(values.put(key, "v" + std::to_string(key)));
    }
    const auto window = values.subMap(20, 40);

    cppstream::TreeMap<int, std::string> sameWindow;
    static_cast<void>(sameWindow.put(20, "v20"));
    static_cast<void>(sameWindow.put(30, "v30"));

    CHECK(window.equals(sameWindow));
    CHECK(sameWindow.equals(window));
    CHECK(window.hashCode() == sameWindow.hashCode());
    CHECK_FALSE(window.equals(values));
}
