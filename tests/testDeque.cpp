#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

TEST_CASE("ArrayDeque is a queue: first in, first out")
{
    cppstream::ArrayDeque<int> queue;

    CHECK(queue.isEmpty());
    CHECK(queue.offer(1));
    CHECK(queue.offer(2));
    queue.add(3);

    CHECK(queue.size() == 3);
    CHECK(queue.peek().get() == 1);
    CHECK(queue.element() == 1);

    CHECK(queue.poll().get() == 1);
    CHECK(queue.remove() == 2);
    CHECK(queue.pop() == 3);
    CHECK(queue.isEmpty());

    CHECK(queue.poll().isEmpty());
    CHECK(queue.peek().isEmpty());
    CHECK_THROWS_AS(static_cast<void>(queue.remove()), cppstream::NoSuchElementException);
    CHECK_THROWS_AS(static_cast<void>(queue.element()), cppstream::NoSuchElementException);
}

TEST_CASE("ArrayDeque is a stack: push and pop work on the head")
{
    cppstream::ArrayDeque<std::string> stack;

    stack.push("a");
    stack.push("b");
    stack.push("c");

    // push() is addFirst, so the head of the deque is the top of the stack.
    CHECK(stack.getFirst() == "c");
    CHECK(stack.getLast() == "a");

    CHECK(stack.pop() == "c");
    CHECK(stack.pop() == "b");
    CHECK(stack.pop() == "a");
    CHECK_THROWS_AS(static_cast<void>(stack.pop()), cppstream::NoSuchElementException);
}

TEST_CASE("ArrayDeque is a deque: both ends stay addressable")
{
    cppstream::ArrayDeque<int> values;

    values.addFirst(2);
    values.addFirst(1);
    values.addLast(3);
    values.addLast(4);

    CHECK(values.toArray() == std::vector<int>{1, 2, 3, 4});
    CHECK(values.getFirst() == 1);
    CHECK(values.getLast() == 4);
    CHECK(values.peekFirst().get() == 1);
    CHECK(values.peekLast().get() == 4);

    CHECK(values.offerFirst(0));
    CHECK(values.offerLast(5));
    CHECK(values.toArray() == std::vector<int>{0, 1, 2, 3, 4, 5});

    CHECK(values.removeFirst() == 0);
    CHECK(values.removeLast() == 5);
    CHECK(values.pollFirst().get() == 1);
    CHECK(values.pollLast().get() == 4);
    CHECK(values.toArray() == std::vector<int>{2, 3});
}

TEST_CASE("ArrayDeque removes the first or the last matching occurrence")
{
    cppstream::ArrayDeque<int> values{1, 2, 3, 2, 1};

    CHECK(values.removeFirstOccurrence(2));
    CHECK(values.toArray() == std::vector<int>{1, 3, 2, 1});

    CHECK(values.removeLastOccurrence(1));
    CHECK(values.toArray() == std::vector<int>{1, 3, 2});

    CHECK_FALSE(values.removeFirstOccurrence(42));
    CHECK_FALSE(values.removeLastOccurrence(42));

    // removeFirstOccurrence is Collection.remove(element) spelled out.
    CHECK(values.remove(3));
    CHECK(values.toArray() == std::vector<int>{1, 2});
}

TEST_CASE("ArrayDeque iterates forwards and backwards")
{
    cppstream::ArrayDeque<int> values{1, 2, 3};

    std::vector<int> forwards;
    for (const int value : values) {
        forwards.push_back(value);
    }
    CHECK(forwards == std::vector<int>{1, 2, 3});

    std::vector<int> backwards;
    auto descending = values.descendingIterator();
    while (descending->hasNext()) {
        backwards.push_back(descending->next());
    }
    CHECK(backwards == std::vector<int>{3, 2, 1});

    // reverse() is the stream-level spelling of the same walk.
    CHECK(values.stream().reversed().toList().toArray() == std::vector<int>{3, 2, 1});
}

TEST_CASE("ArrayDeque iterators are fail-fast and can remove")
{
    cppstream::ArrayDeque<int> values{1, 2, 3, 4};

    auto iterator = values.iterator();
    CHECK(iterator->next() == 1);
    iterator->remove();
    CHECK(values.toArray() == std::vector<int>{2, 3, 4});
    CHECK(iterator->next() == 2);

    // remove() before next() is the standard IllegalStateException.
    auto fresh = values.iterator();
    CHECK_THROWS_AS(fresh->remove(), cppstream::IllegalStateException);

    auto stale = values.iterator();
    values.add(9);
    CHECK_THROWS_AS(stale->next(), cppstream::ConcurrentModificationException);
}

TEST_CASE("a frozen ArrayDeque refuses every mutator")
{
    cppstream::ArrayDeque<int> values{1, 2, 3};
    values.freeze();

    CHECK(values.isFrozen());
    CHECK_THROWS_AS(values.add(4), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.addFirst(0), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.addLast(4), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(values.offer(4)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.push(0), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(values.removeFirst()), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(values.removeLast()), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(values.removeFirstOccurrence(2)), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(values.clear(), cppstream::UnsupportedOperationException);

    // Reading and draining still work: polling from an empty deque is not a
    // structural modification, so it stays legal on a frozen deque.
    CHECK(values.peek().get() == 1);
    CHECK(values.getLast() == 3);
    CHECK(values.size() == 3);
}

TEST_CASE("ArrayDeque is usable through Queue, Deque and Collection references")
{
    cppstream::ArrayDeque<int> storage{1, 2, 3};

    cppstream::Queue<int>& queue = storage;
    CHECK(queue.peek().get() == 1);
    CHECK(queue.poll().get() == 1);

    cppstream::Deque<int>& deque = storage;
    deque.addFirst(0);
    CHECK(deque.getFirst() == 0);
    CHECK(deque.removeLast() == 3);

    const cppstream::Collection<int>& collection = storage;
    CHECK(collection.size() == 2);
    CHECK(collection.contains(0));
    CHECK(collection.stream().sum() == 2);
    CHECK(collection.equals(storage));
}

TEST_CASE("ArrayDeque accepts a backing std::deque, a collection and an initializer list")
{
    cppstream::ArrayDeque<int> fromNodes(std::deque<int>{1, 2, 2, 3});
    CHECK(fromNodes.size() == 4);
    CHECK_FALSE(fromNodes.equals(cppstream::ArrayDeque<int>{1, 2, 3}));

    const cppstream::ArrayList<int> source{4, 5, 6};
    const cppstream::ArrayDeque<int> fromCollection(source);
    CHECK(fromCollection.toArray() == std::vector<int>{4, 5, 6});

    // toArray() keeps the head-to-tail order, so a deque round-trips through
    // the std::deque constructor.
    const std::vector<int> drained = cppstream::ArrayDeque<int>{7, 8}.toArray();
    const cppstream::ArrayDeque<int> copy(std::deque<int>(drained.begin(), drained.end()));
    CHECK(copy.toArray() == std::vector<int>{7, 8});
}

TEST_CASE("ArrayDeque rejects duplicate-free bulk operations correctly")
{
    cppstream::ArrayDeque<int> values{1, 2, 3, 4, 5};
    const cppstream::ArrayList<int> evens{2, 4};

    CHECK(values.containsAll(evens));
    CHECK(values.removeAll(evens));
    CHECK(values.toArray() == std::vector<int>{1, 3, 5});

    CHECK(values.retainAll(evens));
    CHECK(values.isEmpty());

    CHECK(values.addAll(cppstream::ArrayList<int>{1, 2}));
    CHECK(values.toArray() == std::vector<int>{1, 2});
}

TEST_CASE("LinkedList keeps deque-shaped methods without implementing Deque")
{
    // Divergence 29: List and Deque are both abstract classes here, so deriving
    // from both would need virtual inheritance through the whole hierarchy.
    cppstream::LinkedList<int> values{2, 3};

    values.addFirst(1);
    values.addLast(4);
    CHECK(values.toArray() == std::vector<int>{1, 2, 3, 4});
    CHECK(values.getFirst() == 1);
    CHECK(values.getLast() == 4);
    CHECK(values.pollFirst().get() == 1);
    CHECK(values.pollLast().get() == 4);
    CHECK(values.toArray() == std::vector<int>{2, 3});

    static_assert(!std::is_base_of_v<cppstream::Queue<int>, cppstream::LinkedList<int>>);
    static_assert(std::is_base_of_v<cppstream::Deque<int>, cppstream::ArrayDeque<int>>);
}
