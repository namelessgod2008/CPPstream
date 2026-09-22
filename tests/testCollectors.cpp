#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> words() { return {"alpha", "bb", "ccc", "dd", "e"}; }

}  // namespace

TEST_CASE("toList, toSet and their unmodifiable variants")
{
    const cppstream::ArrayList<int> list =
        cppstream::Stream<int>::of({1, 2, 2, 3}).collect(cppstream::Collectors::toList<int>());
    CHECK(list.size() == 4);
    CHECK_FALSE(list.isFrozen());

    const cppstream::HashSet<int> set =
        cppstream::Stream<int>::of({1, 2, 2, 3}).collect(cppstream::Collectors::toSet<int>());
    CHECK(set.size() == 3);

    cppstream::ArrayList<int> frozen =
        cppstream::Stream<int>::of({1, 2}).collect(cppstream::Collectors::toUnmodifiableList<int>());
    CHECK(frozen.isFrozen());
    CHECK_THROWS_AS(frozen.add(3), cppstream::UnsupportedOperationException);

    cppstream::HashSet<int> frozenSet =
        cppstream::Stream<int>::of({1, 2}).collect(cppstream::Collectors::toUnmodifiableSet<int>());
    CHECK(frozenSet.isFrozen());
    CHECK_THROWS_AS(frozenSet.add(3), cppstream::UnsupportedOperationException);
}

TEST_CASE("toMap rejects duplicates, and the merging overload does not")
{
    const cppstream::HashMap<std::string, int> lengths = cppstream::Stream<std::string>::of(words())
        .collect(cppstream::Collectors::toMap<std::string>(
            [](const std::string& word) { return word; },
            [](const std::string& word) { return static_cast<int>(word.size()); }));
    CHECK(lengths.size() == 5);

    // Java throws IllegalStateException on a duplicate key.
    CHECK_THROWS_AS(static_cast<void>(cppstream::Stream<std::string>::of(words()).collect(
                        cppstream::Collectors::toMap<std::string>(
                            [](const std::string& word) { return static_cast<int>(word.size()); },
                            [](const std::string& word) { return word; }))),
        cppstream::IllegalStateException);

    // With a merge function the same stream succeeds.
    const cppstream::HashMap<int, std::string> merged = cppstream::Stream<std::string>::of(words())
        .collect(cppstream::Collectors::toMap<std::string>(
            [](const std::string& word) { return static_cast<int>(word.size()); },
            [](const std::string& word) { return word; },
            [](const std::string& left, const std::string& right) { return left + "|" + right; }));
    CHECK(merged.size() == 4);
    CHECK(merged.get(2)->find('|') != std::string::npos);

    cppstream::HashMap<std::string, int> frozen = cppstream::Stream<std::string>::of(words())
        .collect(cppstream::Collectors::toUnmodifiableMap<std::string>(
            [](const std::string& word) { return word; },
            [](const std::string& word) { return static_cast<int>(word.size()); }));
    CHECK(frozen.isFrozen());
    CHECK_THROWS_AS(frozen.put(std::string("x"), 1), cppstream::UnsupportedOperationException);
}

TEST_CASE("joining covers all three overloads")
{
    const std::string plain =
        cppstream::Stream<std::string>::of(words()).collect(cppstream::Collectors::joining<std::string>());
    CHECK(plain == "alphabbcccdde");

    const std::string delimited = cppstream::Stream<std::string>::of(words())
                                      .collect(cppstream::Collectors::joining<std::string>(", "));
    CHECK(delimited == "alpha, bb, ccc, dd, e");

    const std::string wrapped = cppstream::Stream<std::string>::of(words())
                                    .collect(cppstream::Collectors::joining<std::string>("-", "[", "]"));
    CHECK(wrapped == "[alpha-bb-ccc-dd-e]");

    // Java's joining on an empty stream yields just prefix + suffix.
    CHECK(cppstream::Stream<std::string>::empty().collect(
              cppstream::Collectors::joining<std::string>(",", "<", ">")) == "<>");

    // A single element must not grow a delimiter.
    CHECK(cppstream::Stream<std::string>::of(std::string("solo"))
              .collect(cppstream::Collectors::joining<std::string>(",", "<", ">")) == "<solo>");
}

TEST_CASE("counting, summing and averaging")
{
    CHECK(cppstream::Stream<int>::of({1, 2, 3, 4})
              .collect(cppstream::Collectors::counting<int>()) == 4);

    CHECK(cppstream::Stream<std::string>::of(words())
              .collect(cppstream::Collectors::summingInt<std::string>(
                  [](const std::string& word) { return static_cast<int>(word.size()); })) == 13);

    CHECK(cppstream::Stream<int>::of({1, 2, 3, 4})
              .collect(cppstream::Collectors::summingLong<int>([](int value) { return value; })) == 10);
    CHECK(cppstream::Stream<int>::of({1, 2, 3, 4})
              .collect(cppstream::Collectors::summingDouble<int>([](int value) { return value; })) ==
          doctest::Approx(10.0));

    CHECK(cppstream::Stream<int>::of({1, 2, 3, 4})
              .collect(cppstream::Collectors::averagingInt<int>([](int value) { return value; })) ==
          doctest::Approx(2.5));

    // Java returns 0.0 for the empty case rather than NaN.
    CHECK(cppstream::Stream<int>::empty().collect(
              cppstream::Collectors::averagingDouble<int>([](int value) { return value; })) ==
          doctest::Approx(0.0));
}

TEST_CASE("minBy and maxBy carry Java's tie-breaking")
{
    using Pair = std::pair<int, int>;
    const std::vector<Pair> pairs{{1, 5}, {1, 9}, {0, 7}};
    const auto byKey = [](const Pair& pair) { return pair.first; };
    cppstream::Comparator<Pair> comparator = cppstream::Comparator<Pair>::comparing(byKey);

    const auto minimum =
        cppstream::Stream<Pair>::of(pairs).collect(cppstream::Collectors::minBy(comparator));
    CHECK(minimum.get().second == 7);

    // The 1-keyed pair that comes first in encounter order wins the tie, which is
    // what Java's `compare(a, b) <= 0 ? a : b` produces.
    const std::vector<Pair> tied{{1, 5}, {1, 9}};
    const auto firstTie =
        cppstream::Stream<Pair>::of(tied).collect(cppstream::Collectors::minBy(comparator));
    CHECK(firstTie.get().second == 5);

    CHECK(cppstream::Stream<Pair>::of(pairs)
              .collect(cppstream::Collectors::maxBy(comparator))
              .get()
              .second == 5);

    CHECK(cppstream::Stream<Pair>::empty()
              .collect(cppstream::Collectors::minBy(comparator))
              .isEmpty());
}

TEST_CASE("the three reducing overloads")
{
    CHECK(cppstream::Stream<int>::of({1, 2, 3, 4})
              .collect(cppstream::Collectors::reducing<int>(0,
                  [](int left, int right) { return left + right; })) == 10);

    CHECK(cppstream::Stream<int>::of({5, 2, 9})
              .collect(cppstream::Collectors::reducing<int>(
                  [](int left, int right) { return left < right ? left : right; }))
              .get() == 2);
    CHECK(cppstream::Stream<int>::empty()
              .collect(cppstream::Collectors::reducing<int>(
                  [](int left, int right) { return left + right; }))
              .isEmpty());

    CHECK(cppstream::Stream<std::string>::of(words())
              .collect(cppstream::Collectors::reducing<std::string, int>(0,
                  [](const std::string& word) { return static_cast<int>(word.size()); },
                  [](int left, int right) { return left + right; })) == 13);
}

TEST_CASE("mapping, filtering and flatMapping delegate to a downstream collector")
{
    const cppstream::ArrayList<int> lengths = cppstream::Stream<std::string>::of(words())
        .collect(cppstream::Collectors::mapping<std::string>(
            [](const std::string& word) { return static_cast<int>(word.size()); },
            cppstream::Collectors::toList<int>()));
    CHECK(lengths.size() == 5);
    CHECK(lengths.get(0) == 5);

    const cppstream::ArrayList<std::string> longWords = cppstream::Stream<std::string>::of(words())
        .collect(cppstream::Collectors::filtering<std::string>(
            [](const std::string& word) { return word.size() > 1; },
            cppstream::Collectors::toList<std::string>()));
    CHECK(longWords.size() == 4);

    const cppstream::ArrayList<int> flat = cppstream::Stream<std::string>::of(words())
        .collect(cppstream::Collectors::flatMapping<std::string>(
            [](const std::string& word) {
                return cppstream::Stream<int>::of(static_cast<int>(word.size()), 100);
            },
            cppstream::Collectors::toList<int>()));
    CHECK(flat.size() == 10);
}

TEST_CASE("teeing drives two collectors and merges their results")
{
    const std::string summary = cppstream::Stream<int>::of({1, 2, 3, 4})
        .collect(cppstream::Collectors::teeing<int>(cppstream::Collectors::counting<int>(),
            cppstream::Collectors::summingInt<int>([](int value) { return value; }),
            [](std::int64_t count, int sum) {
                return std::to_string(count) + ":" + std::to_string(sum);
            }));
    CHECK(summary == "4:10");
}

TEST_CASE("groupingBy groups by the classifier's key")
{
    const cppstream::HashMap<bool, cppstream::ArrayList<int>> byParity =
        cppstream::Stream<int>::of({1, 2, 3, 4, 5}).collect(
            cppstream::Collectors::groupingBy<int>([](int value) { return value % 2 == 0; }));

    CHECK(byParity.size() == 2);
    CHECK(byParity.get(true)->size() == 2);
    CHECK(byParity.get(false)->size() == 3);

    // Encounter order inside each group is preserved, which is a documented
    // promise of Java's groupingBy.
    CHECK(byParity.get(false)->get(0) == 1);
    CHECK(byParity.get(false)->get(2) == 5);
}

TEST_CASE("groupingBy accepts a downstream collector")
{
    const cppstream::HashMap<std::string, int> counts = cppstream::Stream<std::string>::of(words())
        .collect(cppstream::Collectors::groupingBy<std::string>(
            [](const std::string& word) { return std::string(1, word.front()); },
            cppstream::Collectors::summingInt<std::string>(
                [](const std::string& word) { return static_cast<int>(word.size()); })));

    CHECK(counts.get(std::string("a")) != nullptr);
    CHECK(*counts.get(std::string("a")) == 5);
}

TEST_CASE("partitioningBy always reports both partitions")
{
    const cppstream::HashMap<bool, cppstream::ArrayList<int>> partitioned =
        cppstream::Stream<int>::of({1, 3, 5}).collect(
            cppstream::Collectors::partitioningBy<int>([](int value) { return value % 2 == 0; }));

    CHECK(partitioned.size() == 2);
    CHECK(partitioned.get(true)->isEmpty());
    CHECK(partitioned.get(false)->size() == 3);

    const cppstream::HashMap<bool, std::int64_t> counted =
        cppstream::Stream<int>::of({1, 2, 3, 4}).collect(
        cppstream::Collectors::partitioningBy<int>([](int value) { return value % 2 == 0; },
            cppstream::Collectors::counting<int>()));
    CHECK(*counted.get(true) == 2);
    CHECK(*counted.get(false) == 2);
}

TEST_CASE("summarizingInt reports count, sum, min, max and average")
{
    const cppstream::IntSummaryStatistics statistics = cppstream::Stream<int>::of({4, 1, 7, 2})
        .collect(cppstream::Collectors::summarizingInt<int>([](int value) { return value; }));

    CHECK(statistics.getCount() == 4);
    CHECK(statistics.getSum() == 14);
    CHECK(statistics.getMin() == 1);
    CHECK(statistics.getMax() == 7);
    CHECK(statistics.getAverage() == doctest::Approx(3.5));

    // Java's empty IntSummaryStatistics keeps the sentinel extremes.
    const cppstream::IntSummaryStatistics empty = cppstream::Stream<int>::empty().collect(
        cppstream::Collectors::summarizingInt<int>([](int value) { return value; }));
    CHECK(empty.getCount() == 0);
    CHECK(empty.getAverage() == doctest::Approx(0.0));
    CHECK(empty.getMin() == std::numeric_limits<int>::max());

    const cppstream::LongSummaryStatistics longs = cppstream::Stream<int>::of({1, 2})
        .collect(cppstream::Collectors::summarizingLong<int>([](int value) { return value; }));
    CHECK(longs.getSum() == 3);

    const cppstream::DoubleSummaryStatistics doubles = cppstream::Stream<int>::of({1, 2})
        .collect(cppstream::Collectors::summarizingDouble<int>([](int value) { return value; }));
    CHECK(doubles.getAverage() == doctest::Approx(1.5));
}

TEST_CASE("container streams flow through the whole collector vocabulary")
{
    const cppstream::HashSet<int> doubled = cppstream::ArrayList<int>{1, 2, 3}
                                                .stream()
                                                .map([](int value) { return value * 2; })
                                                .collect(cppstream::Collectors::toSet<int>());
    CHECK(doubled.size() == 3);
    CHECK(doubled.contains(6));
}
