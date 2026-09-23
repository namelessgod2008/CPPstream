#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <ranges>
#include <string>
#include <vector>

TEST_CASE("HashSet rejects duplicates and reports whether it inserted")
{
    cppstream::HashSet<int> values;

    CHECK(values.add(1));
    CHECK(values.add(2));
    CHECK_FALSE(values.add(2));
    CHECK(values.size() == 2);
    CHECK(values.contains(2));
    CHECK_FALSE(values.contains(9));

    CHECK(values.remove(2));
    CHECK_FALSE(values.remove(2));
    CHECK(values.size() == 1);
}

TEST_CASE("HashSet is constructible from a collection, a capacity and a list")
{
    const cppstream::ArrayList<int> source{1, 2, 3, 3};
    const cppstream::HashSet<int> fromCollection(source);
    CHECK(fromCollection.size() == 3);

    const cppstream::HashSet<int> withCapacity(64);
    CHECK(withCapacity.isEmpty());

    const cppstream::HashSet<int> fromList{4, 5, 4};
    CHECK(fromList.size() == 2);

    CHECK_THROWS_AS(cppstream::HashSet<int>(-1), cppstream::IllegalArgumentException);
}

TEST_CASE("Set equality ignores order where List equality does not")
{
    const cppstream::HashSet<int> left{1, 2, 3};
    const cppstream::HashSet<int> reordered{3, 2, 1};
    const cppstream::ArrayList<int> asList{1, 2, 3};

    CHECK(left.equals(reordered));
    CHECK(left.hashCode() == reordered.hashCode());

    // Java's Set.equals requires the other collection to be a Set, so a List with
    // the same elements is not equal. That is the dynamic_cast in AbstractSet.
    CHECK_FALSE(left.equals(asList));

    // Java's AbstractSet.hashCode is the plain sum, not the ordered 31* fold.
    CHECK(left.hashCode() == 6U);

    const cppstream::HashSet<int> smaller{1, 2};
    CHECK_FALSE(left.equals(smaller));
}

TEST_CASE("HashSet iterators are fail-fast and remove() works")
{
    cppstream::HashSet<int> values{1, 2, 3, 4};

    auto iterator = values.iterator();
    int seen = 0;
    while (iterator->hasNext()) {
        iterator->next();
        ++seen;
    }
    CHECK(seen == 4);

    auto remover = values.iterator();
    CHECK_THROWS_AS(remover->remove(), cppstream::IllegalStateException);
    remover->next();
    remover->remove();
    CHECK(values.size() == 3);

    auto doomed = values.iterator();
    values.add(99);
    CHECK_THROWS_AS(doomed->next(), cppstream::ConcurrentModificationException);
}

TEST_CASE("frozen sets refuse every mutation path")
{
    cppstream::HashSet<int> values{1, 2, 3};
    values.freeze();

    CHECK(values.isFrozen());
    CHECK_THROWS_AS(values.add(4), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.remove(1), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.clear(), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.removeIf([](int value) { return value == 1; }),
        cppstream::UnsupportedOperationException);

    CHECK(values.size() == 3);
}

TEST_CASE("range-for and std::ranges see a Set through the read-only path")
{
    const cppstream::HashSet<int> values{1, 2, 3, 4};

    int total = 0;
    for (int value : values) {
        total += value;
    }
    CHECK(total == 10);

    static_assert(std::ranges::input_range<cppstream::HashSet<int>>);
    CHECK(std::ranges::count_if(values, [](int value) { return value > 2; }) == 2);
}

TEST_CASE("TreeSet iterates in sorted order")
{
    const cppstream::TreeSet<int> values{5, 1, 4, 2, 3, 3};

    CHECK(values.size() == 5);
    CHECK(values.toSortedVector() == std::vector<int>{1, 2, 3, 4, 5});

    std::vector<int> walked;
    for (int value : values) {
        walked.push_back(value);
    }
    CHECK(walked == std::vector<int>{1, 2, 3, 4, 5});
}

TEST_CASE("TreeSet navigable queries follow NavigableSet")
{
    const cppstream::TreeSet<int> values{10, 20, 30, 40};

    CHECK(values.first() == 10);
    CHECK(values.last() == 40);
    CHECK(values.lower(30).get() == 20);
    CHECK(values.floor(30).get() == 30);
    CHECK(values.higher(30).get() == 40);
    CHECK(values.ceiling(30).get() == 30);

    CHECK(values.lower(10).isEmpty());
    CHECK(values.higher(40).isEmpty());
    CHECK(values.floor(9).isEmpty());
    CHECK(values.ceiling(41).isEmpty());

    // The range methods hand back a live view, not a copy; toArray() walks it.
    CHECK(values.subSet(20, 40).toArray() == std::vector<int>{20, 30});
    CHECK(values.headSet(30).toArray() == std::vector<int>{10, 20});
    CHECK(values.tailSet(30).toArray() == std::vector<int>{30, 40});

    CHECK_THROWS_AS(static_cast<void>(cppstream::TreeSet<int>{}.first()),
        cppstream::NoSuchElementException);
}

TEST_CASE("TreeSet mutation keeps ordering and stays fail-fast")
{
    cppstream::TreeSet<int> values{30, 10, 20};

    CHECK(values.add(5));
    CHECK_FALSE(values.add(10));
    CHECK(values.pollFirst().get() == 5);
    CHECK(values.pollLast().get() == 30);
    CHECK(values.toSortedVector() == std::vector<int>{10, 20});

    CHECK(values.pollFirst().get() == 10);
    CHECK(values.pollFirst().get() == 20);
    CHECK(values.pollFirst().isEmpty());

    auto iterator = values.iterator();
    values.add(7);
    CHECK_THROWS_AS(iterator->next(), cppstream::ConcurrentModificationException);
}

TEST_CASE("TreeSet accepts a custom comparator")
{
    // Reverse order, supplied as a std::set-style comparer rather than a
    // cppstream::Comparator: the strategy parameter lives on the concrete class.
    cppstream::TreeSet<int, std::greater<>> descending{1, 3, 2};
    CHECK(descending.toSortedVector() == std::vector<int>{3, 2, 1});
    CHECK(descending.first() == 3);
}

TEST_CASE("TreeSet.descendingSet is a live view of the same tree")
{
    cppstream::TreeSet<int> values{3, 1, 2};

    auto descending = values.descendingSet();
    CHECK(descending.toArray() == std::vector<int>{3, 2, 1});
    CHECK(descending.first() == 3);
    CHECK(descending.last() == 1);
    CHECK(descending.size() == 3);

    // A write through the view lands in the tree...
    CHECK_FALSE(descending.add(3));  // still a Set, so no duplicate
    CHECK(descending.add(0));
    CHECK(values.toArray() == std::vector<int>{0, 1, 2, 3});

    // ...and a change to the tree shows up through the view.
    CHECK(values.add(4));
    CHECK(descending.toArray() == std::vector<int>{4, 3, 2, 1, 0});
    CHECK(descending.modCount() == values.modCount());

    // It is the same window walked the other way, so flipping twice gives the
    // ascending order back, exactly as Java's descendingSet().descendingSet() does.
    CHECK(descending.descendingSet().toArray() == std::vector<int>{0, 1, 2, 3, 4});

    // Java: DescendingSet.comparator() is reverseOrder(m.comparator()).
    CHECK(descending.comparator().compare(1, 2) > 0);
    CHECK(descending.descendingSet().comparator().compare(1, 2) < 0);

    // Descending is still a Set, which is what makes `instanceof Set` agree with
    // Java's AbstractSet.equals.
    CHECK(descending.isSet());
}

TEST_CASE("TreeSet.Descending is still an independent reversed copy")
{
    const cppstream::TreeSet<int> ascending{3, 1, 2};

    // descendingSet() is a view now; the alias still names the copy you build
    // yourself, and the reversed comparer is what makes it sort itself.
    cppstream::TreeSet<int>::Descending copy{ascending};
    CHECK(copy.toSortedVector() == std::vector<int>{3, 2, 1});
    CHECK(copy.first() == 3);
    CHECK(copy.add(0));
    CHECK(copy.size() == 4);
    CHECK(ascending.size() == 3);  // the copy is independent storage
}

TEST_CASE("TreeSet descending views navigate and iterate in their own order")
{
    cppstream::TreeSet<int> values{10, 20, 30, 40, 50};
    auto descending = values.descendingSet();

    CHECK(descending.floor(30).get() == 30);
    CHECK(descending.lower(30).get() == 40);
    CHECK(descending.ceiling(30).get() == 30);
    CHECK(descending.higher(30).get() == 20);
    CHECK(descending.floor(5).get() == 10);      // clamped to the window
    CHECK(descending.ceiling(100).get() == 50);  // clamped to the window

    CHECK(descending.pollFirst().get() == 50);
    CHECK(descending.pollLast().get() == 10);
    CHECK(values.toArray() == std::vector<int>{20, 30, 40});

    // headSet keeps the far fence and replaces the one at this view's start, which
    // descending means the *upper* end moves -- Java's DescendingSet.headSet.
    CHECK(descending.headSet(30).toArray() == std::vector<int>{40});
    CHECK(descending.headSet(30, true).toArray() == std::vector<int>{40, 30});
    CHECK(descending.tailSet(30).toArray() == std::vector<int>{30, 20});
    CHECK(descending.subSet(40, true, 20, false).toArray() == std::vector<int>{40, 30});

    // Rejected the same way, and with Java's message: "from" is the upper end here,
    // so 20 < 40 in ascending terms is the backwards window.
    CHECK_THROWS_AS(static_cast<void>(descending.subSet(20, 40)),
        cppstream::IllegalArgumentException);

    // descendingIterator() is the opposite of the view's own order.
    std::vector<int> forwards;
    auto iterator = descending.descendingIterator();
    while (iterator->hasNext()) {
        forwards.push_back(iterator->next());
    }
    CHECK(forwards == std::vector<int>{20, 30, 40});
}

TEST_CASE("a descending view taken from a const set is read-only")
{
    const cppstream::TreeSet<int> values{1, 2, 3};
    auto descending = values.descendingSet();

    CHECK(descending.toArray() == std::vector<int>{3, 2, 1});
    CHECK_THROWS_AS(static_cast<void>(descending.add(0)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(descending.remove(1)), cppstream::UnsupportedOperationException);

    // A window of a read-only view is a read-only view, not an exception -- the
    // unmodifiableNavigableSet(set).descendingSet().subSet(...) chain.
    CHECK(descending.subSet(3, true, 1, false).toArray() == std::vector<int>{3, 2});
    CHECK(descending.descendingSet().toArray() == std::vector<int>{1, 2, 3});
}

TEST_CASE("TreeSet.descendingIterator walks tail to head and fails fast")
{
    cppstream::TreeSet<int> values{1, 2, 3};

    std::vector<int> seen;
    auto iterator = values.descendingIterator();
    while (iterator->hasNext()) {
        seen.push_back(iterator->next());
    }
    CHECK(seen == std::vector<int>{3, 2, 1});

    auto stale = values.descendingIterator();
    values.add(4);
    CHECK_THROWS_AS(stale->next(), cppstream::ConcurrentModificationException);

    const cppstream::TreeSet<int>& readOnly = values;
    auto constIterator = readOnly.descendingConstIterator();
    seen.clear();
    while (constIterator->hasNext()) {
        seen.push_back(constIterator->next());
    }
    CHECK(seen == std::vector<int>{4, 3, 2, 1});
}

TEST_CASE("HashSet accepts a custom hash strategy")
{
    struct Point {
        int x;
        int y;
    };
    struct PointHash {
        std::size_t operator()(const Point& point) const noexcept {
            return (static_cast<std::size_t>(point.x) * 31U) + static_cast<std::size_t>(point.y);
        }
    };
    struct PointEqual {
        bool operator()(const Point& left, const Point& right) const noexcept {
            return left.x == right.x && left.y == right.y;
        }
    };

    // No std::hash<Point> specialisation anywhere, which is more than Java allows.
    cppstream::HashSet<Point, PointHash, PointEqual> points;
    CHECK(points.add(Point{1, 2}));
    CHECK_FALSE(points.add(Point{1, 2}));
    CHECK(points.add(Point{2, 1}));
    CHECK(points.size() == 2);
}

TEST_CASE("set operations inherited from AbstractCollection still work")
{
    cppstream::HashSet<int> values{1, 2, 3, 4};
    const cppstream::HashSet<int> odds{1, 3};

    CHECK(values.containsAll(odds));
    CHECK(values.removeAll(odds));
    CHECK(values.size() == 2);
    CHECK(values.retainAll(cppstream::HashSet<int>{2}));
    CHECK(values.size() == 1);
    CHECK(values.contains(2));

    values.clear();
    CHECK(values.isEmpty());
}
