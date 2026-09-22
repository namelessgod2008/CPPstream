#include <doctest/doctest.h>

#include <cppstream/cppstream.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

namespace {

[[nodiscard]] bool sameValues(const std::vector<int>& actual, const std::vector<int>& expected) {
    return actual == expected;
}

}  // namespace

TEST_CASE("ArrayList provides the Java List surface")
{
    cppstream::ArrayList<std::string> names;

    CHECK(names.isEmpty());
    CHECK(names.add(std::string("bob")));
    CHECK(names.add(std::string("alice")));
    CHECK(names.add(std::string("carol")));
    CHECK(names.size() == 3);
    CHECK_FALSE(names.isEmpty());

    CHECK(names.contains("alice"));
    CHECK_FALSE(names.contains("dave"));

    CHECK(names.get(1) == "alice");
    CHECK(names.indexOf("carol") == 2);
    CHECK(names.indexOf("dave") == -1);
    CHECK(names.lastIndexOf("bob") == 0);

    CHECK(names.set(1, std::string("alicia")) == "alice");
    CHECK(names.get(1) == "alicia");

    names.add(0, std::string("zed"));
    CHECK(names.get(0) == "zed");
    CHECK(names.removeAt(0) == "zed");

    CHECK(names.remove("alicia"));
    CHECK_FALSE(names.remove("alicia"));
}

TEST_CASE("braced initialisation is a list, parenthesised is a capacity")
{
    const cppstream::ArrayList<int> oneElement{5};
    CHECK(oneElement.size() == 1);
    CHECK(oneElement.get(0) == 5);

    const cppstream::ArrayList<int> reserved(5);
    CHECK(reserved.isEmpty());

    const cppstream::ArrayList<int> source{1, 2, 3};
    const cppstream::ArrayList<int> copied(source);
    CHECK(sameValues(copied.toArray(), {1, 2, 3}));

    // Going through the Collection constructor explicitly, rather than the copy
    // constructor, exercises the generic path.
    const cppstream::List<int>& asList = source;
    const cppstream::ArrayList<int> fromCollection(asList);
    CHECK(sameValues(fromCollection.toArray(), {1, 2, 3}));

    CHECK_THROWS_AS(cppstream::ArrayList<int>(-1), cppstream::IllegalArgumentException);
}

TEST_CASE("AbstractCollection algorithms are built from the iterator alone")
{
    cppstream::ArrayList<int> values{1, 2, 3, 4, 5};

    CHECK(values.removeIf([](int value) { return value % 2 == 0; }));
    CHECK(sameValues(values.toArray(), {1, 3, 5}));

    const cppstream::ArrayList<int> other{3, 5, 7};
    CHECK_FALSE(values.containsAll(other));
    CHECK(values.containsAll(cppstream::ArrayList<int>{1, 3}));

    CHECK(values.retainAll(other));
    CHECK(sameValues(values.toArray(), {3, 5}));

    CHECK(values.addAll(other));
    CHECK(sameValues(values.toArray(), {3, 5, 3, 5, 7}));

    CHECK(values.removeAll(other));
    CHECK(values.isEmpty());

    values.addAll(other);
    values.clear();
    CHECK(values.isEmpty());
    CHECK(values.size() == 0);
}

TEST_CASE("equals is element-wise in order and hashCode is the ordered 31* fold")
{
    const cppstream::ArrayList<int> left{1, 2, 3};
    const cppstream::ArrayList<int> same{1, 2, 3};
    const cppstream::ArrayList<int> reordered{3, 2, 1};
    const cppstream::ArrayList<int> shorter{1, 2};

    CHECK(left.equals(same));
    CHECK_FALSE(left.equals(reordered));
    CHECK_FALSE(left.equals(shorter));
    CHECK(left.hashCode() == same.hashCode());

    // Java's AbstractList.hashCode(): h = 31*h + e, starting from 1.
    CHECK(left.hashCode() == 30817u);

    // Order matters for a List, which is exactly why AbstractSet overrides this
    // with the order-insensitive sum.
    CHECK(left.hashCode() != reordered.hashCode());
}

TEST_CASE("element types need neither operator== nor std::hash")
{
    struct Plain {
        int value;
    };

    // Java's Object gives every type an equals and a hashCode; C++ does not, so
    // the container falls back to identity. Without that fallback this test case
    // would not even compile, because contains/hashCode are virtual overrides.
    cppstream::ArrayList<Plain> values;
    values.add(Plain{1});
    values.add(Plain{2});

    CHECK(values.size() == 2);
    CHECK_FALSE(values.contains(Plain{1}));

    const Plain& first = values.get(0);
    CHECK(values.contains(first));
    CHECK(values.hashCode() != 0u);
}

TEST_CASE("index problems raise Java's exception type and message")
{
    cppstream::ArrayList<int> values{1, 2, 3};
    const std::string expected = "Index 5 out of bounds for length 3";

    CHECK_THROWS_WITH_AS(values.get(5), expected.c_str(), cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_WITH_AS(values.removeAt(5), expected.c_str(), cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_WITH_AS(values.set(5, 1), expected.c_str(), cppstream::IndexOutOfBoundsException);

    CHECK_THROWS_AS(values.get(-1), cppstream::IndexOutOfBoundsException);
    // IndexOutOfBoundsException derives from IllegalArgumentException, so either
    // catch works.
    CHECK_THROWS_AS(values.get(5), cppstream::IllegalArgumentException);
}

TEST_CASE("collections reject null for pointer element types")
{
    cppstream::ArrayList<int*> pointers;
    int value = 1;
    CHECK(pointers.add(&value));
    CHECK(pointers.size() == 1);

    int* const nullPointer = nullptr;
    CHECK_THROWS_AS(pointers.add(nullPointer), cppstream::NullPointerException);
}

TEST_CASE("subList is a live view, addAll inserts, sort reorders")
{
    cppstream::ArrayList<int> values{5, 3, 1, 4, 2};

    const auto middle = values.subList(1, 3);
    CHECK(sameValues(middle->toArray(), {3, 1}));

    // A view, not a copy: changing the original is visible through the window.
    values.set(1, 30);
    CHECK(sameValues(middle->toArray(), {30, 1}));

    const cppstream::ArrayList<int> extra{7, 8};
    CHECK(values.addAll(1, extra));
    CHECK(sameValues(values.toArray(), {5, 7, 8, 30, 1, 4, 2}));

    values.sort(cppstream::Comparator<int>::naturalOrder());
    CHECK(sameValues(values.toArray(), {1, 2, 4, 5, 7, 8, 30}));

    CHECK_THROWS_AS(static_cast<void>(values.subList(-1, 2)),
        cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_AS(static_cast<void>(values.subList(3, 1)),
        cppstream::IndexOutOfBoundsException);
}

TEST_CASE("iterators walk the list and fail fast on structural modification")
{
    cppstream::ArrayList<int> values{1, 2, 3};

    auto iter = values.iterator();
    CHECK(iter->hasNext());
    CHECK(iter->next() == 1);
    CHECK(iter->next() == 2);

    values.add(4);
    CHECK_THROWS_AS(iter->next(), cppstream::ConcurrentModificationException);
}

TEST_CASE("the read-only iterator refuses to remove")
{
    cppstream::ArrayList<int> values{1, 2, 3};

    auto iter = values.constIterator();
    CHECK(iter->next() == 1);
    CHECK_THROWS_AS(iter->remove(), cppstream::UnsupportedOperationException);
}

TEST_CASE("forEachRemaining drains the iterator")
{
    cppstream::ArrayList<int> values{1, 2, 3};

    std::vector<int> seen;
    auto iter = values.iterator();
    iter->forEachRemaining([&seen](int& value) { seen.push_back(value); });

    CHECK(sameValues(seen, {1, 2, 3}));
    CHECK_FALSE(iter->hasNext());
}

TEST_CASE("listIterator walks both ways and edits in place")
{
    cppstream::ArrayList<int> values{1, 2, 3};

    auto iter = values.listIterator();
    CHECK(iter->nextIndex() == 0);
    CHECK(iter->previousIndex() == -1);
    CHECK(iter->next() == 1);
    CHECK(iter->previousIndex() == 0);

    iter->set(10);
    CHECK(values.get(0) == 10);

    iter->add(15);
    CHECK(sameValues(values.toArray(), {10, 15, 2, 3}));
    CHECK(iter->previousIndex() == 1);

    CHECK(iter->hasPrevious());
    CHECK(iter->previous() == 15);
    CHECK(iter->next() == 15);

    iter->remove();
    CHECK(sameValues(values.toArray(), {10, 2, 3}));

    // set() and remove() require a preceding next()/previous().
    CHECK_THROWS_AS(iter->set(99), cppstream::IllegalStateException);
    CHECK_THROWS_AS(iter->remove(), cppstream::IllegalStateException);

    // previous() is still legal while the cursor sits inside the list; it throws
    // only once the cursor has been walked back to the front. This is Java's
    // behaviour: at cursor 1, previous() yields element 0.
    CHECK(iter->previousIndex() == 0);
    CHECK(iter->previous() == 10);
    CHECK_THROWS_AS(iter->previous(), cppstream::NoSuchElementException);
}

TEST_CASE("range-for and std::ranges work through the read-only path")
{
    cppstream::ArrayList<int> values{1, 2, 3, 4};

    int total = 0;
    for (int value : values) {
        total += value;
    }
    CHECK(total == 10);

    const cppstream::ArrayList<int>& view = values;
    int viaConstReference = 0;
    for (int value : view) {
        viaConstReference += value;
    }
    CHECK(viaConstReference == 10);

    static_assert(std::input_iterator<cppstream::ReadOnlyIterator<int>>);
    static_assert(!std::copyable<cppstream::ReadOnlyIterator<int>>);
    static_assert(std::ranges::input_range<cppstream::ArrayList<int>>);

    CHECK(std::ranges::count_if(values, [](int value) { return value % 2 == 0; }) == 2);
    CHECK(std::ranges::find(values, 3) != std::ranges::end(values));
}

TEST_CASE("forEach hands out mutable references")
{
    cppstream::ArrayList<int> values{1, 2, 3};

    values.forEach([](int& value) { value *= 10; });

    CHECK(sameValues(values.toArray(), {10, 20, 30}));
}

TEST_CASE("a container stream yields copies, not aliases into the container")
{
    cppstream::ArrayList<int> values{1, 2, 3};

    const auto squares = values.stream().peek([](int& value) { value *= value; }).toArray();

    CHECK(sameValues(squares, {1, 4, 9}));

    // The source is untouched: the pipeline materialises values, so peek mutates
    // the copies rather than the backing vector. This is a documented divergence
    // from Java, where the elements are references.
    CHECK(values.get(0) == 1);
    CHECK(values.get(1) == 2);
    CHECK(values.get(2) == 3);
}

TEST_CASE("a container stream is lazy and fails fast if the container changes")
{
    cppstream::ArrayList<int> values{1, 2, 3};

    auto stream = values.stream();
    values.add(4);

    CHECK_THROWS_AS(
        static_cast<void>(std::move(stream).count()), cppstream::ConcurrentModificationException);

    auto fresh = values.stream();
    CHECK(std::move(fresh).count() == 4);
}
