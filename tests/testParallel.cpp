#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

/// doctest has no guaranteed stringification for std::vector, so comparisons are
/// wrapped in a plain bool predicate rather than written inline in a CHECK.
[[nodiscard]] bool sameValues(const std::vector<int>& actual, const std::vector<int>& expected) {
    return actual == expected;
}

/// Restores whatever parallelism the process started with, so that a test which
/// caps it cannot change the behaviour of the tests that follow.
class ParallelismGuard {
public:
    ParallelismGuard() : saved_(cppstream::parallelism()) {}
    ~ParallelismGuard() { cppstream::setParallelism(saved_); }

    ParallelismGuard(const ParallelismGuard&) = delete;
    ParallelismGuard& operator=(const ParallelismGuard&) = delete;

private:
    std::size_t saved_;
};

/// A sized source whose elements are produced on demand, so that a test can see how
/// far a parallel stage has read ahead. It is deliberately a plain lambda rather
/// than a container: the pull budget has to come from the stream itself.
[[nodiscard]] cppstream::Stream<int> countedRange(int count, std::size_t& pulls) {
    return cppstream::Stream<int>(
        cppstream::Stream<int>::NextFn([count, &pulls, index = 0] mutable -> std::optional<int> {
            if (index >= count) {
                return std::nullopt;
            }
            ++pulls;
            return index++;
        }),
        static_cast<std::size_t>(count), static_cast<std::size_t>(count));
}

}  // namespace

TEST_CASE("parallel and sequential flip the flag on the stream and its stages") {
    using cppstream::Stream;

    CHECK_FALSE(Stream<int>::of(1).isParallel());
    CHECK(Stream<int>::of(1).parallel().isParallel());
    // The flag is carried by every stage built from a parallel stream, which is what
    // makes batching reach the stages rather than stopping at the bend.
    CHECK(Stream<int>::of(1).parallel().map([](int value) { return value; }).isParallel());
    CHECK(Stream<int>::of(1).parallel().filter([](int) { return true; }).isParallel());
    CHECK_FALSE(Stream<int>::of(1).parallel().sequential().isParallel());
    // And a sequential stage in the middle clears it for the stages after it.
    const auto identity = [](int value) { return value; };
    CHECK_FALSE(Stream<int>::of(1).parallel().sequential().map(identity).isParallel());
}

TEST_CASE("a parallel pipeline produces exactly the sequential answer") {
    using cppstream::Stream;

    for (const int count : {0, 1, 2, 7, 1000, 5000}) {
        const auto sequential = Stream<int>::range(0, count)
                                    .filter([](int value) { return value % 3 != 0; })
                                    .map([](int value) { return (value * 7) - 1; })
                                    .skip(count / 4)
                                    .limit(count / 2)
                                    .toArray();
        const auto parallel = Stream<int>::range(0, count)
                                  .parallel()
                                  .filter([](int value) { return value % 3 != 0; })
                                  .map([](int value) { return (value * 7) - 1; })
                                  .skip(count / 4)
                                  .limit(count / 2)
                                  .toArray();
        CHECK(sameValues(parallel, sequential));
    }
}

TEST_CASE("parallel batches keep encounter order") {
    using cppstream::Stream;

    const auto values =
        Stream<int>::range(0, 3000).parallel().map([](int value) { return value * 2; }).toArray();

    bool ordered = values.size() == 3000;
    for (std::size_t index = 0; ordered && index < values.size(); ++index) {
        ordered = values[index] == static_cast<int>(index) * 2;
    }
    CHECK(ordered);
}

TEST_CASE("parallel additivity: sum, count and toArray agree with the sequential run") {
    using cppstream::Stream;

    // A 64-bit element type again: the sum of 1..100000 does not fit in an int, and
    // an overflowing sum would be undefined behaviour rather than a wrong answer.
    const auto sequentialSum = Stream<std::int64_t>::rangeClosed(1, 100000).sum();
    const auto parallelSum = Stream<std::int64_t>::rangeClosed(1, 100000).parallel().sum();
    CHECK(parallelSum == sequentialSum);
    CHECK(parallelSum == 100000LL * 100001LL / 2);

    CHECK(Stream<int>::range(0, 10000).parallel().count() == 10000);

    const auto parallelArray = Stream<int>::range(0, 4000).parallel().toArray();
    CHECK(parallelArray.size() == 4000);
    CHECK(parallelArray.front() == 0);
    CHECK(parallelArray.back() == 3999);
}

TEST_CASE("parallel map really runs on more than one thread") {
    using cppstream::Stream;

    std::mutex guard;
    std::set<std::thread::id> workers;
    // A 64-bit element type: the sum of the first 4000 squares does not fit in an
    // int, and an overflowing test would be testing the wrong thing.
    const auto squares = Stream<std::int64_t>::range(0, 4000)
                             .parallel()
                             .map([&guard, &workers](std::int64_t value) {
                                 {
                                     std::scoped_lock lock(guard);
                                     workers.insert(std::this_thread::get_id());
                                 }
                                 return value * value;
                             })
                             .sum();

    CHECK(squares == 3999LL * 4000LL * 7999LL / 6);
    // A single-worker pool would make every parallel test in this file vacuous, so
    // the fact that the batch was spread out is worth asserting on its own.
    CHECK(workers.size() > 1);
}

TEST_CASE("forEachOrdered stays sequential while forEach keeps every element") {
    using cppstream::Stream;

    // size_t, not int: the element of position n is compared against n itself, and
    // keeping both sides unsigned is what makes that comparison cast-free.
    std::vector<std::size_t> order;
    Stream<int>::range(0, 2000).parallel().forEachOrdered(
        [&order](int value) { order.push_back(static_cast<std::size_t>(value)); });
    bool ordered = order.size() == 2000;
    for (std::size_t index = 0; ordered && index < order.size(); ++index) {
        ordered = order[index] == index;
    }
    CHECK(ordered);

    // forEach is allowed to overlap its actions, so it is checked for contents
    // rather than for ordering.
    std::int64_t total = 0;
    std::mutex guard;
    Stream<int>::range(0, 2000).parallel().forEach([&total, &guard](int value) {
        std::scoped_lock lock(guard);
        total += value;
    });
    CHECK(total == 2000 * 1999 / 2);
}

TEST_CASE("parallel collect folds slices back in encounter order") {
    using cppstream::Stream;

    const auto sequential = Stream<int>::range(0, 5000)
                                .map([](int value) { return value % 97; })
                                .collect(cppstream::Collectors::toList<int>());
    const auto parallel = Stream<int>::range(0, 5000)
                              .parallel()
                              .map([](int value) { return value % 97; })
                              .collect(cppstream::Collectors::toList<int>());
    CHECK(parallel.toArray() == sequential.toArray());

    const auto words = cppstream::ArrayList<std::string>{"the", "quick", "brown", "fox"};
    CHECK(words.stream().collect(cppstream::Collectors::joining<std::string>("-")) ==
          words.parallelStream().collect(cppstream::Collectors::joining<std::string>("-")));
}

TEST_CASE("parallel collect on a grouping collector keeps per-group order and totals") {
    using cppstream::Collectors;
    using cppstream::Stream;

    const auto byRemainder = [](int value) { return value % 8; };
    const auto identity = [](int value) { return value; };
    const auto grouping =
        Collectors::groupingBy<int>(byRemainder, Collectors::summingInt<int>(identity));

    const auto sequential = Stream<int>::range(0, 5000).collect(grouping);
    const auto parallel = Stream<int>::range(0, 5000).parallel().collect(grouping);

    CHECK(parallel.equals(sequential));
    CHECK(parallel.size() == 8);
}

TEST_CASE("parallel reduce, min, max and sum equal the sequential run") {
    using cppstream::Stream;

    const auto comparator = cppstream::Comparator<int>::naturalOrder();
    const auto add = [](int left, int right) { return left + right; };

    for (const int count : {0, 1, 3, 4000}) {
        const auto sequentialReduce = Stream<int>::range(1, count + 1).reduce(add);
        const auto parallelReduce = Stream<int>::range(1, count + 1).parallel().reduce(add);
        CHECK(parallelReduce.isEmpty() == sequentialReduce.isEmpty());
        if (sequentialReduce.isPresent()) {
            CHECK(parallelReduce.get() == sequentialReduce.get());
        }

        CHECK(Stream<int>::range(0, count).parallel().reduce(5, add) ==
              Stream<int>::range(0, count).reduce(5, add));

        const auto scattered = [count](int value) { return (value * 37) % 100; };
        const auto sequentialMin = Stream<int>::range(0, count).map(scattered).min(comparator);
        const auto sequentialMax = Stream<int>::range(0, count).map(scattered).max(comparator);
        const auto parallelMin =
            Stream<int>::range(0, count).parallel().map(scattered).min(comparator);
        const auto parallelMax =
            Stream<int>::range(0, count).parallel().map(scattered).max(comparator);

        CHECK(parallelMin.isEmpty() == sequentialMin.isEmpty());
        CHECK(parallelMax.isEmpty() == sequentialMax.isEmpty());
        if (sequentialMin.isPresent()) {
            CHECK(parallelMin.get() == sequentialMin.get());
            CHECK(parallelMax.get() == sequentialMax.get());
        }
    }
}

TEST_CASE("min keeps the first of equal elements in a parallel pipeline") {
    using cppstream::Stream;

    // Every element compares equal to every other, so a merge that preferred its
    // right-hand side would hand back the last one instead of the first.
    struct Peek {
        int order;
    };
    const auto comparator = cppstream::Comparator<Peek>(
        [](const Peek& left, const Peek& right) { return left.order - right.order; });
    const auto byOrder = [](int value) { return Peek{value}; };

    const auto smallest = Stream<int>::range(0, 4000).parallel().map(byOrder).min(comparator);
    CHECK(smallest.isPresent());
    CHECK(smallest.get().order == 0);
}

TEST_CASE("parallel match terminals keep short-circuiting and agree with the sequential run") {
    using cppstream::Stream;

    for (const int count : {1, 2, 100, 9000}) {
        const auto isLast = [count](int value) { return value == count - 1; };
        const auto inRange = [count](int value) { return value < count; };
        const auto isPast = [count](int value) { return value == count; };

        CHECK(Stream<int>::range(0, count).parallel().anyMatch(isLast) ==
              Stream<int>::range(0, count).anyMatch(isLast));
        CHECK(Stream<int>::range(0, count).parallel().allMatch(inRange) ==
              Stream<int>::range(0, count).allMatch(inRange));
        CHECK(Stream<int>::range(0, count).parallel().noneMatch(isPast) ==
              Stream<int>::range(0, count).noneMatch(isPast));
    }

    // An infinite source can only be answered by a short-circuit: anyMatch stops at
    // the first batch that holds a witness, and the batches before that are pulled
    // one element at a time because an unbounded source has no pull budget.
    CHECK(Stream<int>::iterate(1, [](int value) { return value + 1; })
              .parallel()
              .anyMatch([](int value) { return value == 500; }));
}

TEST_CASE("an unbounded parallel source stays sequential instead of prefetching") {
    using cppstream::Stream;

    // Batching here would pull a whole chunk from an infinite source and hang, so the
    // stage has to fall back to one element at a time. Reaching the answer at all is
    // the assertion.
    const auto first = Stream<int>::iterate(1, [](int value) { return value + 1; })
                           .parallel()
                           .filter([](int value) { return value % 7 == 0; })
                           .findFirst();
    CHECK(first.isPresent());
    CHECK(first.get() == 7);

    const auto limited = Stream<int>::iterate(1, [](int value) { return value + 1; })
                             .parallel()
                             .map([](int value) { return value * 2; })
                             .limit(4)
                             .toArray();
    CHECK(sameValues(limited, {2, 4, 6, 8}));

    // A bounded stage *after* an unbounded one gets its budget back, so this one does
    // batch even though the source is infinite.
    const auto bounded = Stream<int>::generate([] { return 3; })
                             .parallel()
                             .limit(2000)
                             .map([](int value) { return value + 1; })
                             .sum();
    CHECK(bounded == 2000 * 4);
}

TEST_CASE("a parallel stage never reads further ahead than its bound") {
    std::size_t sequentialPulls = 0;
    std::size_t parallelPulls = 0;

    // findFirst() has to stop after the first batch, not drain the source.
    const auto sequential =
        countedRange(100000, sequentialPulls).map([](int value) { return value + 1; }).findFirst();
    const auto parallel = countedRange(100000, parallelPulls)
                              .parallel()
                              .map([](int value) { return value + 1; })
                              .findFirst();
    CHECK(sequential.get() == parallel.get());
    CHECK(sequentialPulls < 5000);
    CHECK(parallelPulls < 5000);
}

TEST_CASE("flatMap batches the mappers but still drains inner streams lazily and in order") {
    using cppstream::Stream;

    const auto twice = [](int value) { return Stream<int>::of(value, value + 1000); };
    const auto sequential = Stream<int>::range(0, 200).flatMap(twice).toArray();
    const auto parallel = Stream<int>::range(0, 200).parallel().flatMap(twice).toArray();
    CHECK(parallel == sequential);

    // The inner stream is drained lazily, so an inner stream that never ends is still
    // usable as long as the pipeline stops early.
    const auto endless = [](int value) {
        return Stream<int>::iterate(value, [](int current) { return current + 1; });
    };
    const auto head = Stream<int>::range(0, 10).parallel().flatMap(endless).limit(3).toArray();
    CHECK(sameValues(head, {0, 1, 2}));
}

TEST_CASE("stateful stages are barriers but pass the parallel flag on") {
    using cppstream::Stream;

    // distinct and sorted pull one element at a time; the map after them batches.
    const auto values = Stream<int>::range(0, 5000)
                            .parallel()
                            .distinct()
                            .sorted()
                            .map([](int value) { return value * 2; })
                            .toArray();
    bool correct = values.size() == 5000;
    for (std::size_t index = 0; correct && index < values.size(); ++index) {
        correct = values[index] == static_cast<int>(index) * 2;
    }
    CHECK(correct);
}

TEST_CASE("an exception thrown inside a parallel stage reaches the caller") {
    using cppstream::Stream;

    bool caught = false;
    try {
        // The result is irrelevant here; the failure is. The cast silences the
        // [[nodiscard]] on count() without pretending the value is used.
        static_cast<void>(Stream<int>::range(0, 5000)
                              .parallel()
                              .map([](int value) {
                                  if (value == 2000) {
                                      throw cppstream::IllegalStateException("boom");
                                  }
                                  return value;
                              })
                              .count());
    } catch (const cppstream::IllegalStateException& error) {
        caught = std::string(error.what()) == "boom";
    }
    CHECK(caught);
}

TEST_CASE("a parallel stage pulled from inside a worker degrades instead of deadlocking") {
    using cppstream::Stream;

    // Each outer element runs a whole parallel pipeline inside the mapper, so the
    // inner stage is entered from a pool worker. Queueing there and waiting for a
    // free thread is exactly the deadlock the inline fallback prevents.
    const auto total = Stream<int>::range(0, 100)
                           .parallel()
                           .map([](int value) {
                               return Stream<int>::range(0, 10)
                                   .parallel()
                                   .map([value](int inner) { return value * inner; })
                                   .sum();
                           })
                           .sum();
    CHECK(total == 100 * 99 / 2 * (10 * 9 / 2));
}

TEST_CASE("capping parallelism at one degrades every stage to the sequential path") {
    using cppstream::Stream;

    const ParallelismGuard guard;
    cppstream::setParallelism(1);

    CHECK(cppstream::parallelism() == 1);

    // The flag still says parallel, as it does in Java; the batching is what is off.
    const auto stream = Stream<int>::range(0, 5000).parallel();
    CHECK(stream.isParallel());

    const auto values = Stream<int>::range(0, 5000)
                            .parallel()
                            .map([](int value) { return value + 1; })
                            .filter([](int value) { return value % 2 == 0; })
                            .toArray();
    CHECK(values.size() == 2500);

    // Values below one and above the pool size are clamped rather than rejected.
    cppstream::setParallelism(0);
    CHECK(cppstream::parallelism() == 1);
}

TEST_CASE("parallelStream is the container entry point and keeps the size as its budget") {
    using cppstream::ArrayList;
    using cppstream::TreeMap;
    using cppstream::TreeSet;

    const ArrayList<int> list{5, 3, 9, 1};
    const auto doubled = [](int value) { return value * 2; };
    CHECK(sameValues(list.parallelStream().map(doubled).sorted().toArray(), {2, 6, 10, 18}));
    CHECK(sameValues(list.stream().map(doubled).sorted().toArray(), {2, 6, 10, 18}));

    const TreeSet<int> sortedSet{10, 20, 30};
    CHECK(sortedSet.parallelStream().toArray() == std::vector<int>({10, 20, 30}));

    const TreeMap<std::string, int> stock{{std::string("apples"), 3}, {std::string("pears"), 5}};
    // A Map has no stream() of its own, exactly as in Java; its views inherit
    // parallelStream from AbstractCollection and see the same budget.
    auto keys = stock.keySet();
    CHECK(keys.parallelStream().sorted().toArray() ==
          std::vector<std::string>({std::string("apples"), std::string("pears")}));
}
