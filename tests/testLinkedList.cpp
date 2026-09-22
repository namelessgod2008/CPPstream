#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

TEST_CASE("LinkedList satisfies the List contract")
{
    cppstream::LinkedList<int> values{1, 2, 3};

    CHECK(values.size() == 3);
    CHECK(values.get(0) == 1);
    CHECK(values.get(2) == 3);

    values.add(1, 9);
    CHECK(values.toArray() == std::vector<int>{1, 9, 2, 3});

    CHECK(values.removeAt(0) == 1);
    CHECK(values.toArray() == std::vector<int>{9, 2, 3});

    CHECK(values.set(0, 7) == 9);
    CHECK(values.get(0) == 7);

    CHECK(values.indexOf(2) == 1);
    CHECK(values.lastIndexOf(2) == 1);
    CHECK(values.indexOf(42) == -1);

    CHECK(values.subList(0, 2)->toArray() == std::vector<int>{7, 2});

    CHECK_THROWS_WITH_AS(values.get(9), "Index 9 out of bounds for length 3",
        cppstream::IndexOutOfBoundsException);
    CHECK_THROWS_AS(values.add(9, 1), cppstream::IndexOutOfBoundsException);
}

TEST_CASE("LinkedList is a List, so the same polymorphic code drives both")
{
    // The interface was reused verbatim: only the storage differs. This is the
    // check that List<T> really is abstract enough.
    const cppstream::LinkedList<int> linked{1, 2, 3, 4};
    const cppstream::ArrayList<int> array{1, 2, 3, 4};

    const cppstream::List<int>& first = linked;
    const cppstream::List<int>& second = array;

    CHECK(first.equals(second));
    CHECK(second.equals(first));
    CHECK(first.hashCode() == second.hashCode());
    CHECK(first.stream().map([](int value) { return value * 2; }).sum() == 20);
}

TEST_CASE("LinkedList addAll with an index preserves order")
{
    cppstream::LinkedList<int> values{1, 4};
    const cppstream::ArrayList<int> middle{2, 3};

    CHECK(values.addAll(1, middle));
    CHECK(values.toArray() == std::vector<int>{1, 2, 3, 4});

    CHECK_FALSE(values.addAll(4, cppstream::ArrayList<int>{}));
}

TEST_CASE("LinkedList sort is stable and honours the comparator")
{
    cppstream::LinkedList<int> values{3, 1, 2};
    values.sort(cppstream::Comparator<int>::naturalOrder());
    CHECK(values.toArray() == std::vector<int>{1, 2, 3});

    values.sort(cppstream::Comparator<int>::reverseOrder());
    CHECK(values.toArray() == std::vector<int>{3, 2, 1});
}

TEST_CASE("LinkedList listIterator edits through a linked cursor")
{
    cppstream::LinkedList<int> values{1, 2, 3, 4};

    auto iterator = values.listIterator();
    CHECK(iterator->nextIndex() == 0);
    CHECK(iterator->next() == 1);

    iterator->set(10);
    iterator->add(15);
    CHECK(values.toArray() == std::vector<int>{10, 15, 2, 3, 4});
    CHECK(iterator->nextIndex() == 2);

    CHECK(iterator->previous() == 15);
    CHECK(iterator->next() == 15);

    iterator->remove();
    CHECK(values.toArray() == std::vector<int>{10, 2, 3, 4});
    CHECK(iterator->nextIndex() == 1);

    // Removing via next() shifts the index back by one.
    CHECK(iterator->next() == 2);
    iterator->remove();
    CHECK(values.toArray() == std::vector<int>{10, 3, 4});
    CHECK(iterator->nextIndex() == 1);

    CHECK_THROWS_AS(iterator->set(1), cppstream::IllegalStateException);
    CHECK_THROWS_AS(iterator->remove(), cppstream::IllegalStateException);
}

TEST_CASE("LinkedList walk backwards to the front")
{
    cppstream::LinkedList<int> values{1, 2, 3};

    auto iterator = values.listIterator(3);
    CHECK_FALSE(iterator->hasNext());
    CHECK(iterator->previous() == 3);
    CHECK(iterator->previous() == 2);
    CHECK(iterator->previous() == 1);
    CHECK_THROWS_AS(iterator->previous(), cppstream::NoSuchElementException);
}

TEST_CASE("LinkedList fails fast during iteration")
{
    cppstream::LinkedList<int> values{1, 2, 3};

    auto iterator = values.iterator();
    values.add(4);
    CHECK_THROWS_AS(iterator->next(), cppstream::ConcurrentModificationException);
}

TEST_CASE("LinkedList deque-shaped helpers")
{
    cppstream::LinkedList<int> values;

    values.addFirst(2);
    values.addFirst(1);
    values.addLast(3);
    CHECK(values.toArray() == std::vector<int>{1, 2, 3});

    CHECK(values.getFirst() == 1);
    CHECK(values.getLast() == 3);
    CHECK(values.peekFirst().get() == 1);
    CHECK(values.peekLast().get() == 3);

    CHECK(values.removeFirst() == 1);
    CHECK(values.removeLast() == 3);
    CHECK(values.toArray() == std::vector<int>{2});

    CHECK(values.pollFirst().get() == 2);
    CHECK(values.pollLast().isEmpty());
    CHECK(values.isEmpty());
    CHECK_THROWS_AS(static_cast<void>(values.getFirst()), cppstream::NoSuchElementException);
}

TEST_CASE("LinkedList supports range-for and the collector vocabulary")
{
    const cppstream::LinkedList<std::string> words{"a", "bb", "ccc"};

    int characters = 0;
    for (const std::string& word : words) {
        characters += static_cast<int>(word.size());
    }
    CHECK(characters == 6);

    static_assert(std::ranges::input_range<cppstream::LinkedList<std::string>>);

    const std::string joined = words.stream().collect(cppstream::Collectors::joining<std::string>("+"));
    CHECK(joined == "a+bb+ccc");

    const cppstream::HashSet<std::string> unique =
        words.stream().collect(cppstream::Collectors::toSet<std::string>());
    CHECK(unique.size() == 3);
}

TEST_CASE("frozen LinkedList refuses mutation")
{
    cppstream::LinkedList<int> values{1, 2, 3};
    values.freeze();

    CHECK_THROWS_AS(values.add(4), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.removeAt(0), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.addFirst(0), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.clear(), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.sort(cppstream::Comparator<int>::naturalOrder()),
        cppstream::UnsupportedOperationException);
    CHECK(values.size() == 3);
}
