#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

/// A copy of the window, so that assertions read as values rather than as a
/// moving target while the view is being mutated.
std::vector<int> windowOf(const cppstream::List<int>& view)
{
    return view.toArray();
}

}  // namespace

TEST_CASE("ListView is a live window, not a copy")
{
    cppstream::ArrayList<int> values{5, 3, 1, 4, 2};
    const auto middle = values.subList(1, 4);

    CHECK(middle->size() == 3);
    CHECK(windowOf(*middle) == std::vector<int>{3, 1, 4});

    // Writes through the view land in the list...
    middle->set(0, 30);
    CHECK(values.toArray() == std::vector<int>{5, 30, 1, 4, 2});

    // ...and writes to the list are visible through the view.
    values.set(3, 40);
    CHECK(windowOf(*middle) == std::vector<int>{30, 1, 40});

    // Sub-list indices are window-relative, exactly as in Java.
    CHECK(middle->indexOf(1) == 1);
    CHECK(middle->indexOf(5) == -1);
    CHECK(middle->lastIndexOf(40) == 2);
}

TEST_CASE("ListView resizes the window when the window is structurally changed")
{
    cppstream::ArrayList<int> values{1, 2, 3, 4, 5};
    const auto window = values.subList(1, 4);

    CHECK(window->add(99));
    CHECK(window->size() == 4);
    CHECK(values.toArray() == std::vector<int>{1, 2, 3, 4, 99, 5});

    window->add(0, 0);
    CHECK(values.toArray() == std::vector<int>{1, 0, 2, 3, 4, 99, 5});

    CHECK(window->removeAt(0) == 0);
    CHECK(window->remove(99));
    CHECK_FALSE(window->remove(12345));
    CHECK(window->size() == 3);
    CHECK(values.toArray() == std::vector<int>{1, 2, 3, 4, 5});

    CHECK(window->addAll(1, cppstream::ArrayList<int>{7, 8}));
    CHECK(window->size() == 5);
    CHECK(values.toArray() == std::vector<int>{1, 2, 7, 8, 3, 4, 5});

    window->clear();
    CHECK(window->isEmpty());
    CHECK(values.toArray() == std::vector<int>{1, 5});
}

TEST_CASE("a sub-list of a sub-list shares the same backing storage")
{
    cppstream::ArrayList<int> values{0, 1, 2, 3, 4, 5};
    const auto outer = values.subList(1, 5);
    const auto inner = outer->subList(1, 3);

    CHECK(windowOf(*inner) == std::vector<int>{2, 3});

    inner->set(0, 20);
    CHECK(values.toArray() == std::vector<int>{0, 1, 20, 3, 4, 5});

    // The inner view's index space is the outer view's, not the list's.
    CHECK(inner->get(0) == 20);
    CHECK(outer->get(1) == 20);
    CHECK(values.get(2) == 20);

    CHECK_THROWS_AS(static_cast<void>(outer->subList(0, 9)), cppstream::IndexOutOfBoundsException);
}

TEST_CASE("ListView iterators walk the window and write through it")
{
    cppstream::ArrayList<int> values{1, 2, 3, 4, 5};
    const auto window = values.subList(1, 4);

    std::vector<int> seen;
    for (const int value : *window) {
        seen.push_back(value);
    }
    CHECK(seen == std::vector<int>{2, 3, 4});

    auto iterator = window->iterator();
    CHECK(iterator->next() == 2);
    iterator->remove();
    CHECK(values.toArray() == std::vector<int>{1, 3, 4, 5});

    auto listIterator = window->listIterator();
    CHECK(listIterator->nextIndex() == 0);
    CHECK(listIterator->next() == 3);
    CHECK(listIterator->previous() == 3);
    CHECK(listIterator->previousIndex() == -1);
    listIterator->add(33);
    CHECK(values.toArray() == std::vector<int>{1, 33, 3, 4, 5});
    CHECK(window->size() == 3);

    auto backwards = window->listIterator(window->size());
    std::vector<int> reversed;
    while (backwards->hasPrevious()) {
        reversed.push_back(backwards->previous());
    }
    CHECK(reversed == std::vector<int>{4, 3, 33});
}

TEST_CASE("a view iterator fails fast when the backing list changes underneath it")
{
    cppstream::ArrayList<int> values{1, 2, 3, 4, 5};
    const auto window = values.subList(1, 4);

    auto iterator = window->iterator();
    CHECK(iterator->next() == 2);

    values.add(6);
    CHECK_THROWS_AS(iterator->next(), cppstream::ConcurrentModificationException);

    auto another = window->listIterator();
    CHECK(another->next() == 2);
    values.removeAt(0);
    CHECK_THROWS_AS(another->next(), cppstream::ConcurrentModificationException);
}

TEST_CASE("a view of a const list is read-only")
{
    const cppstream::ArrayList<int> values{1, 2, 3, 4, 5};
    const auto window = values.subList(1, 4);

    CHECK(windowOf(*window) == std::vector<int>{2, 3, 4});
    CHECK(window->indexOf(4) == 2);
    CHECK(window->stream().map([](int value) { return value * 2; }).sum() == 18);

    auto reader = window->constIterator();
    std::vector<int> seen;
    while (reader->hasNext()) {
        seen.push_back(reader->next());
    }
    CHECK(seen == std::vector<int>{2, 3, 4});

    CHECK_THROWS_AS(static_cast<void>(window->iterator()), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window->set(0, 9)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window->add(9)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window->removeAt(0)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(window->clear()), cppstream::UnsupportedOperationException);
}

TEST_CASE("freeze() on a view leaves the backing list alone")
{
    cppstream::ArrayList<int> values{1, 2, 3, 4, 5};
    const auto window = values.subList(1, 4);
    window->freeze();

    CHECK_THROWS_AS(static_cast<void>(window->add(9)), cppstream::UnsupportedOperationException);
    CHECK(window->isFrozen());
    CHECK_FALSE(values.isFrozen());

    // The list itself is untouched, so the view is still readable.
    values.add(6);
    CHECK(values.size() == 6);
}

TEST_CASE("ListView validates its range against the parent's index space")
{
    cppstream::ArrayList<int> values{1, 2, 3};
    const auto window = values.subList(1, 3);

    CHECK_THROWS_AS(static_cast<void>(values.subList(-1, 2)), cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_AS(static_cast<void>(values.subList(2, 1)), cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_AS(static_cast<void>(values.subList(0, 4)), cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_AS(static_cast<void>(window->get(2)), cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_AS(static_cast<void>(window->add(3, 9)), cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_AS(static_cast<void>(window->listIterator(3)), cppstream::IndexOutOfBoundsException);

    // The window stops where it was asked to stop, not where the list does.
    CHECK(window->get(1) == 3);
}

TEST_CASE("ListView sorts only its own window")
{
    cppstream::ArrayList<int> values{9, 5, 1, 3, 7, 8};
    const auto window = values.subList(1, 5);

    window->sort(cppstream::Comparator<int>::naturalOrder());
    CHECK(values.toArray() == std::vector<int>{9, 1, 3, 5, 7, 8});
}

TEST_CASE("ListView behaves like the equivalent ArrayList")
{
    cppstream::LinkedList<int> values{1, 2, 3, 4, 5};

    // The same view type serves any List, exactly as Java's SubList does.
    const auto window = values.subList(1, 4);

    CHECK(windowOf(*window) == std::vector<int>{2, 3, 4});
    CHECK(window->equals(*cppstream::ArrayList<int>{2, 3, 4}.subList(0, 3)));
    CHECK(window->hashCode() == cppstream::ArrayList<int>{2, 3, 4}.hashCode());

    window->set(0, 20);
    CHECK(values.get(1) == 20);

    // Through a const reference the very same call yields a read-only view.
    const cppstream::List<int>& asList = values;
    const auto readOnly = asList.subList(1, 4);
    CHECK(windowOf(*readOnly) == std::vector<int>{20, 3, 4});
    CHECK_THROWS_AS(static_cast<void>(readOnly->set(0, 0)),
        cppstream::UnsupportedOperationException);

    // A sub-list of a read-only view is read-only rather than an error, which is
    // what Collections.unmodifiableList(list).subList(a, b) does -- and it is still
    // a window onto the same list.
    const auto inner = readOnly->subList(0, 2);
    CHECK(windowOf(*inner) == std::vector<int>{20, 3});
    values.set(1, 21);
    CHECK(windowOf(*inner) == std::vector<int>{21, 3});
    CHECK_THROWS_AS(static_cast<void>(inner->set(0, 0)),
        cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(inner->subList(0, 1)->add(1)),
        cppstream::UnsupportedOperationException);
}

TEST_CASE("a view answers with its backing list's modification count")
{
    cppstream::ArrayList<int> values{1, 2, 3, 4, 5};
    const auto window = values.subList(1, 4);

    // Not the view's own count: it is a window, so the iterator's fail-fast signal
    // has to be the list the cursor actually points into.
    CHECK(window->modCount() == values.modCount());
    values.add(6);
    CHECK(window->modCount() == values.modCount());
    window->add(9);
    CHECK(window->modCount() == values.modCount());
    CHECK(values.size() == 7);
}
