#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Descending views: TreeSet.descendingSet / TreeMap.descendingMap /
// navigableKeySet / descendingKeySet, on a container and on a range view.
//
// Every expectation below was produced by running the equivalent program against
// java.util.TreeSet / java.util.TreeMap / java.util.TreeMap.DescendingSubMap on a
// JDK. The two probes are committed at tests/jdk/DescendingProbe.java and
// tests/jdk/DescendingEdgeProbe.java; run one with
// `javac -d /tmp/probe DescendingProbe.java && java -cp /tmp/probe DescendingProbe`.
//
// The bulk of the checking was done by diffing a 629-line set transcript and a
// 502-line map transcript generated from both sides; these tests pin down the
// parts of that transcript that would otherwise be easy to regress in silence --
// the fence acceptance rules, the exact messages, and the direction of every
// member that swaps its two senses.
// ---------------------------------------------------------------------------

namespace {

using IntSet = cppstream::TreeSet<int>;
using IntMap = cppstream::TreeMap<int, std::string>;

[[nodiscard]] bool listIs(const std::vector<int>& actual, const std::vector<int>& expected) {
    return actual == expected;
}

/// Templated so it accepts a TreeMap and any window onto one alike.
template <class MapType>
[[nodiscard]] std::vector<int> keysOf(const MapType& map) {
    std::vector<int> keys;
    for (const auto& entry : map.entrySet()) {
        keys.push_back(entry.getKey());
    }
    return keys;
}

/// Applies a bound to a view and reports whether it was accepted, so a whole
/// acceptance matrix can be written as data rather than as a wall of CHECKs.
template <class Apply>
[[nodiscard]] bool accepted(Apply apply) {
    try {
        static_cast<void>(apply());
        return true;
    } catch (const cppstream::IllegalArgumentException&) {
        return false;
    }
}

/// The message of the IllegalArgumentException a bound provokes, or "".
template <class Apply>
[[nodiscard]] std::string rejection(Apply apply) {
    try {
        static_cast<void>(apply());
        return "";
    } catch (const cppstream::IllegalArgumentException& error) {
        return error.what();
    }
}

}  // namespace

TEST_CASE("a descending set window accepts exactly the fences the JDK accepts")
{
    IntSet values{10, 20, 30, 40, 50};
    // The descending view of the ascending window [20, 40).
    auto window = values.subSet(20, true, 40, false).descendingSet();

    // The accepted ones, and the resulting order. headSet(k) keeps what lies
    // *above* k in ascending terms, so these two are small: on this window only
    // 20 is above 20, and nothing is above 40.
    CHECK(listIs(window.toArray(), {30, 20}));
    CHECK(listIs(window.headSet(40, false).toArray(), {}));
    CHECK(listIs(window.tailSet(20, true).toArray(), {20}));
    CHECK(listIs(window.subSet(35, true, 20, false).toArray(), {30}));

    // "to" may not exceed the window's upper end...
    CHECK_FALSE(accepted([&] { return window.headSet(45, false); }));
    CHECK_FALSE(accepted([&] { return window.headSet(100, false); }));
    // ...nor may it *claim* to include a fence the window excludes.
    CHECK_FALSE(accepted([&] { return window.headSet(40, true); }));
    // "from" may not fall below the window's lower end...
    CHECK_FALSE(accepted([&] { return window.tailSet(5, false); }));
    // ...but a non-inclusive bound only has to lie in the closed range, so one
    // equal to an exclusive fence is accepted even when it yields nothing: here
    // tailSet(20, false) is the ascending window [20, 20).
    CHECK(listIs(window.tailSet(20, false).toArray(), {}));
    // A sub-window's two bounds are checked independently, from first.
    CHECK_FALSE(accepted([&] { return window.subSet(35, true, 45, false); }));
    CHECK_FALSE(accepted([&] { return window.subSet(45, true, 15, false); }));
    CHECK_FALSE(accepted([&] { return window.subSet(15, true, 5, false); }));

    // The same window seen through a container, to prove the view of a view keeps
    // the window rather than widening to the whole tree.
    CHECK(listIs(window.descendingSet().toArray(), {20, 30}));
}

TEST_CASE("a descending window rejects a backwards range the way the JDK does")
{
    IntSet values{10, 20, 30, 40, 50};
    auto descending = values.descendingSet();

    // Unbounded, so only the ordering itself can be wrong: "from" is the *upper*
    // end, so 20 below 40 is the backwards window.
    CHECK_THROWS_AS(
        static_cast<void>(descending.subSet(20, 40)), cppstream::IllegalArgumentException);
    CHECK(listIs(descending.subSet(40, true, 20, false).toArray(), {40, 30}));
    CHECK(listIs(descending.subSet(40, false, 20, false).toArray(), {30}));

    // headSet/tailSet on a descending view flip orientation: headSet keeps the
    // elements *above* the bound in ascending terms.
    CHECK(listIs(descending.headSet(30).toArray(), {50, 40}));
    CHECK(listIs(descending.headSet(30, true).toArray(), {50, 40, 30}));
    CHECK(listIs(descending.tailSet(30).toArray(), {30, 20, 10}));
}

TEST_CASE("the fence messages on a descending view name the view's own parameters")
{
    IntSet values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, true, 40, false).descendingSet();

    // Java's DescendingSubMap checks inRange(from), then inRange(to), and reports
    // whichever it was with the name the *caller* used.
    CHECK(rejection([&] { return window.headSet(45, false); }) == "toKey out of range");
    CHECK(rejection([&] { return window.headSet(40, true); }) == "toKey out of range");
    CHECK(rejection([&] { return window.tailSet(5, false); }) == "fromKey out of range");
    CHECK(rejection([&] { return window.subSet(35, true, 45, false); }) == "toKey out of range");
    CHECK(rejection([&] { return window.subSet(45, true, 15, false); }) == "fromKey out of range");
    CHECK(rejection([&] { return window.subSet(15, true, 5, false); }) == "fromKey out of range");
    CHECK(rejection([&] { return window.subSet(20, true, 40, false); }) == "fromKey > toKey");
}

TEST_CASE("lookups on a descending window swap floor with ceiling and lower with higher")
{
    IntSet values{10, 20, 30, 40, 50};
    auto descending = values.descendingSet();

    CHECK(descending.floor(30).get() == 30);
    CHECK(descending.lower(30).get() == 40);
    CHECK(descending.ceiling(30).get() == 30);
    CHECK(descending.higher(30).get() == 20);

    // Clamping still happens, in the direction the view's own ordering calls for.
    CHECK(descending.floor(5).get() == 10);
    CHECK(descending.ceiling(100).get() == 50);

    // A bounded descending window: {20, 30} walked as 30 then 20.
    auto window = values.subSet(20, true, 40, false).descendingSet();  // {20, 30}
    CHECK(window.floor(25).get() == 30);
    CHECK(window.ceiling(25).get() == 20);
    // A key outside the window clamps to the window's own end, the way the full
    // set clamps to its own: floor() below the window is the window's last, and
    // ceiling() above it is the window's first.
    CHECK(window.floor(5).get() == 20);
    CHECK(window.ceiling(100).get() == 30);
}

TEST_CASE("descendingSet on a container and on a view hand out views, not copies")
{
    IntSet values{10, 20, 30};
    auto descending = values.descendingSet();

    CHECK(descending.add(25));
    CHECK(values.contains(25));
    CHECK(listIs(values.toArray(), {10, 20, 25, 30}));

    CHECK(values.add(5));
    CHECK(listIs(descending.toArray(), {30, 25, 20, 10, 5}));

    // descendingIterator() is the opposite of the view's own order, so on a
    // descending view it walks forwards.
    std::vector<int> forwards;
    auto iterator = descending.descendingIterator();
    while (iterator->hasNext()) {
        forwards.push_back(iterator->next());
    }
    CHECK(listIs(forwards, {5, 10, 20, 25, 30}));

    // Flipping twice recovers the original walk, fences and all.
    CHECK(listIs(descending.descendingSet().toArray(), {5, 10, 20, 25, 30}));
}

TEST_CASE("a descending set view fails fast when the tree changes underneath it")
{
    IntSet values{10, 20, 30};
    auto descending = values.descendingSet();

    auto iterator = descending.iterator();
    CHECK(iterator->next() == 30);
    values.add(40);
    CHECK_THROWS_AS(iterator->next(), cppstream::ConcurrentModificationException);

    auto again = descending.iterator();
    CHECK(again->next() == 40);
    again->remove();
    CHECK_FALSE(values.contains(40));
    CHECK(listIs(descending.toArray(), {30, 20, 10}));
}

TEST_CASE("a descending set view removes through the window and no wider")
{
    IntSet values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, true, 40, false).descendingSet();  // {20, 30}

    CHECK(window.pollFirst().get() == 30);
    CHECK(window.pollLast().get() == 20);
    CHECK(window.isEmpty());
    CHECK(listIs(values.toArray(), {10, 40, 50}));

    // The window is still the window: a value outside it is simply not there, but
    // adding one is refused, exactly as on an ascending view.
    CHECK_FALSE(window.remove(10));
    CHECK_THROWS_AS(static_cast<void>(window.add(45)), cppstream::IllegalArgumentException);
    CHECK(window.add(25));
    CHECK(values.contains(25));
}

TEST_CASE("an accepted-but-empty descending window is empty, not a runaway walk")
{
    IntSet values{10, 20, 30, 40, 50};
    auto window = values.subSet(20, true, 40, false).descendingSet();

    // This window's ascending fences are [40, 40): accepted, and empty. Getting
    // this wrong used to make firstInRange() and pastLastInRange() straddle the
    // tree (upper_bound(40) past lower_bound(40)), so size() walked past end().
    auto empty = window.headSet(40, false);
    CHECK(empty.isEmpty());
    CHECK(empty.size() == 0);
    CHECK(listIs(empty.toArray(), {}));
    CHECK(empty.floor(100).isEmpty());
    CHECK(empty.ceiling(5).isEmpty());
    CHECK(empty.pollFirst().isEmpty());
    CHECK(empty.pollLast().isEmpty());

    // Both walks agree, in either direction, and the iterators terminate.
    CHECK_FALSE(empty.iterator()->hasNext());
    CHECK_FALSE(empty.descendingIterator()->hasNext());
    CHECK(listIs(empty.descendingSet().toArray(), {}));

    // An empty window on an empty tree is the same story, as are the container
    // level spellings of a window that vanished.
    IntSet none;
    CHECK(none.descendingSet().isEmpty());
    CHECK(none.descendingSet().headSet(40, false).isEmpty());
    CHECK(listIs(values.headSet(5, false).descendingSet().toArray(), {}));
    CHECK(listIs(values.tailSet(100, true).descendingSet().toArray(), {}));
    CHECK(values.subSet(20, true, 40, false).descendingSet().headSet(40, false).isEmpty());

    // A map spelling of the same window.
    IntMap prices{{10, "a"}, {20, "b"}, {30, "c"}, {40, "d"}, {50, "e"}};
    auto emptyMap = prices.subMap(20, true, 40, false).descendingMap().headMap(40, false);
    CHECK(emptyMap.isEmpty());
    CHECK(emptyMap.size() == 0);
    CHECK(keysOf(emptyMap).empty());
    CHECK(emptyMap.firstEntry().isEmpty());
    CHECK(emptyMap.pollLastEntry().isEmpty());
    CHECK_FALSE(emptyMap.entrySet().iterator()->hasNext());

    // ...and the same window seen through a key set, which is the third shape a
    // window can take.
    auto emptyKeys = emptyMap.navigableKeySet();
    CHECK(emptyKeys.isEmpty());
    CHECK(emptyKeys.size() == 0);
    CHECK(emptyKeys.toArray().empty());
    CHECK(emptyKeys.floor(100).isEmpty());
    CHECK(emptyKeys.ceiling(5).isEmpty());
    CHECK(emptyKeys.pollFirst().isEmpty());
    CHECK_FALSE(emptyKeys.iterator()->hasNext());
    CHECK_FALSE(emptyKeys.descendingIterator()->hasNext());
    CHECK(emptyKeys.descendingSet().isEmpty());
    CHECK_THROWS_AS(static_cast<void>(emptyKeys.first()), cppstream::NoSuchElementException);
}

TEST_CASE("a descending view of a const container is read-only, and so is its window")
{
    const IntSet values{10, 20, 30};
    auto descending = values.descendingSet();

    CHECK(listIs(descending.toArray(), {30, 20, 10}));
    CHECK_THROWS_AS(static_cast<void>(descending.add(40)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(
        static_cast<void>(descending.remove(10)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(
        static_cast<void>(descending.pollFirst()), cppstream::UnsupportedOperationException);

    // A window of a read-only view is a read-only view, not an exception -- the
    // Collections.unmodifiableNavigableSet(set).descendingSet().subSet(...) chain.
    auto window = descending.subSet(30, true, 10, false);  // {30, 20}
    CHECK(listIs(window.toArray(), {30, 20}));
    CHECK(listIs(window.headSet(20, true).toArray(), {30, 20}));
    CHECK_THROWS_AS(static_cast<void>(window.add(25)), cppstream::UnsupportedOperationException);

    // Its iterator still reads; only the write throws.
    std::vector<int> seen;
    auto iterator = descending.constIterator();
    while (iterator->hasNext()) {
        seen.push_back(iterator->next());
    }
    CHECK(listIs(seen, {30, 20, 10}));
}

// ---------------------------------------------------------------------------
// TreeMap
// ---------------------------------------------------------------------------

TEST_CASE("descendingMap is a view, and its fence rules match the JDK")
{
    IntMap prices{{10, "a"}, {20, "b"}, {30, "c"}, {40, "d"}, {50, "e"}};
    auto descending = prices.descendingMap();

    CHECK(listIs(keysOf(descending), {50, 40, 30, 20, 10}));
    CHECK(descending.firstKey() == 50);
    CHECK(descending.lastKey() == 10);
    CHECK(descending.floorKey(30).get() == 30);
    CHECK(descending.ceilingKey(30).get() == 30);
    CHECK(descending.lowerKey(30).get() == 40);
    CHECK(descending.higherKey(30).get() == 20);

    // Writes land in the map; map writes show up through the view.
    static_cast<void>(descending.put(25, "z"));
    CHECK(*prices.get(25) == "z");
    static_cast<void>(prices.put(5, "f"));
    CHECK(descending.lastKey() == 5);
    CHECK(descending.firstKey() == 50);

    // A bounded descending window with the JDK's messages.
    auto window = prices.subMap(20, true, 40, false).descendingMap();  // {20, 25, 30}
    CHECK(listIs(keysOf(window), {30, 25, 20}));
    CHECK(rejection([&] { return window.headMap(45, false); }) == "toKey out of range");
    CHECK(rejection([&] { return window.tailMap(5, false); }) == "fromKey out of range");
    CHECK(rejection([&] { return window.subMap(20, true, 40, false); }) == "fromKey > toKey");
    CHECK(listIs(keysOf(window.headMap(30, true)), {30}));
    CHECK(listIs(keysOf(window.tailMap(30, true)), {30, 25, 20}));
}

TEST_CASE("descendingMap flips the inherited key, value and entry views")
{
    IntMap prices{{10, "a"}, {20, "b"}, {30, "c"}};
    auto descending = prices.descendingMap();

    std::vector<std::string> values;
    for (const std::string& value : descending.values()) {
        values.push_back(value);
    }
    CHECK(values == std::vector<std::string>{"c", "b", "a"});

    std::vector<int> keys;
    for (const int key : descending.keySet()) {
        keys.push_back(key);
    }
    CHECK(listIs(keys, {30, 20, 10}));

    std::vector<std::string> entries;
    for (const auto& entry : descending.entrySet()) {
        entries.push_back(std::to_string(entry.getKey()) + "=" + entry.getValue());
    }
    CHECK(entries == std::vector<std::string>{"30=c", "20=b", "10=a"});

    CHECK(descending.firstEntry().get().getKey() == 30);
    CHECK(descending.lastEntry().get().getKey() == 10);
    CHECK(descending.pollFirstEntry().get().getKey() == 30);
    CHECK(descending.pollLastEntry().get().getKey() == 10);
    CHECK(listIs(keysOf(prices), {20}));
}

TEST_CASE("navigableKeySet is a live NavigableSet, bounded windows included")
{
    IntMap prices{{10, "a"}, {20, "b"}, {30, "c"}, {40, "d"}, {50, "e"}};

    auto keys = prices.navigableKeySet();
    CHECK(listIs(keys.toArray(), {10, 20, 30, 40, 50}));
    CHECK(keys.floor(25).get() == 20);
    CHECK(keys.ceiling(25).get() == 30);
    CHECK(keys.comparator().compare(1, 2) < 0);

    // subSet/headSet/tailSet are windows onto the same map.
    CHECK(listIs(keys.subSet(30, true, 50, true).toArray(), {30, 40, 50}));
    CHECK_THROWS_AS(
        static_cast<void>(keys.subSet(50, true, 30, true)), cppstream::IllegalArgumentException);

    // A bounded key set answers its own fences, with the JDK's messages.
    auto window = prices.subMap(20, true, 40, false).navigableKeySet();  // {20, 30}
    CHECK(listIs(window.toArray(), {20, 30}));
    CHECK(window.floor(5).isEmpty());          // below the window's lower fence
    CHECK(window.floor(100).get() == 30);      // clamped to the window
    CHECK(window.ceiling(100).isEmpty());
    CHECK(window.ceiling(5).get() == 20);      // clamped up
    CHECK(rejection([&] { return window.headSet(100, true); }) == "toKey out of range");
    CHECK(rejection([&] { return window.tailSet(5, false); }) == "fromKey out of range");

    // Removing a key takes its mapping; adding one is refused.
    CHECK(keys.remove(20));
    CHECK_FALSE(prices.containsKey(20));
    CHECK_THROWS_AS(static_cast<void>(keys.add(60)), cppstream::UnsupportedOperationException);
}

TEST_CASE("descendingKeySet is a bounded navigable view of the same keys")
{
    IntMap prices{{10, "a"}, {20, "b"}, {30, "c"}, {40, "d"}, {50, "e"}};
    auto keys = prices.subMap(20, false, 50, true).descendingKeySet();  // (20, 50], descending

    CHECK(listIs(keys.toArray(), {50, 40, 30}));
    CHECK(keys.first() == 50);
    CHECK(keys.last() == 30);
    CHECK(keys.comparator().compare(1, 2) > 0);

    // floor() is "greatest <= under this ordering", which descending is the
    // ascending ceiling.
    CHECK(keys.floor(15).get() == 30);
    CHECK(keys.ceiling(15).isEmpty());
    CHECK(keys.floor(100).isEmpty());
    CHECK(keys.ceiling(100).get() == 50);

    CHECK(listIs(keys.subSet(40, true, 30, false).toArray(), {40}));
    CHECK(rejection([&] { return keys.headSet(100, false); }) == "toKey out of range");
    CHECK(rejection([&] { return keys.tailSet(10, false); }) == "fromKey out of range");

    // descendingSet() on a descending key set is ascendingKeySet(), and both write
    // through to the map.
    CHECK(listIs(keys.descendingSet().toArray(), {30, 40, 50}));
    CHECK(keys.remove(40));
    CHECK_FALSE(prices.containsKey(40));

    // The iterator walks this key set's order and removes through the window.
    std::vector<int> seen;
    auto iterator = keys.iterator();
    while (iterator->hasNext()) {
        const int key = iterator->next();
        seen.push_back(key);
        if (key == 50) {
            iterator->remove();
        }
    }
    CHECK(listIs(seen, {50, 30}));
    CHECK_FALSE(prices.containsKey(50));
    CHECK(prices.containsKey(30));
}

TEST_CASE("a descending map view of a const map is read-only")
{
    const IntMap prices{{10, "a"}, {20, "b"}};

    auto descending = prices.descendingMap();
    CHECK(listIs(keysOf(descending), {20, 10}));
    CHECK_THROWS_AS(
        static_cast<void>(descending.put(30, "c")), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(descending.pollFirstEntry()),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(prices.navigableKeySet().add(30)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(prices.navigableKeySet().remove(10)),
        cppstream::UnsupportedOperationException);

    // Reading is fine, and a window of a read-only view stays read-only rather than
    // throwing.
    CHECK(listIs(keysOf(descending.subMap(20, true, 10, false)), {20}));
    CHECK(listIs(prices.navigableKeySet().toArray(), {10, 20}));
    CHECK(listIs(prices.descendingKeySet().toArray(), {20, 10}));
}
