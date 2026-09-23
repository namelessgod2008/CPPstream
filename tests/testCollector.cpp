#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

/// A Collector that sums ints, spelled with Collector::of exactly as Java's
/// Collector.of is.
cppstream::Collector<int, int, int> summingCollector() {
    return cppstream::Collector<int, int, int>::of(
        [] { return 0; }, [](int& accumulator, int& element) { accumulator += element; },
        [](int&& left, int&& right) { return left + right; },
        [](int&& accumulator) { return accumulator; },
        {cppstream::Characteristics::identityFinish});
}

}  // namespace

TEST_CASE("Collector exposes Java's four functions and its characteristics")
{
    const cppstream::Collector<int, int, int> collector = summingCollector();

    CHECK(collector.has(cppstream::Characteristics::identityFinish));
    CHECK_FALSE(collector.has(cppstream::Characteristics::concurrent));
    CHECK(collector.characteristics().size() == 1);

    // The sub-functions are usable directly, as Java's accessors allow.
    int accumulator = collector.supplier()();
    int element = 41;
    collector.accumulator()(accumulator, element);
    CHECK(collector.finisher()(std::move(accumulator)) == 41);
}

TEST_CASE("Collectors are copyable specification objects")
{
    // The pipeline is move-only; the collector is not. That asymmetry is what
    // lets the combinators hold a downstream collector by value.
    const cppstream::Collector<int, int, int> original = summingCollector();
    const cppstream::Collector<int, int, int>& copy = original;

    CHECK(copy.has(cppstream::Characteristics::identityFinish));
    CHECK(std::move(original).finisher()(0) == 0);
}

TEST_CASE("Stream.collect drives supplier, accumulator and finisher once")
{
    const int total = cppstream::Stream<int>::of({1, 2, 3, 4})
                          .collect(summingCollector());
    CHECK(total == 10);
}

TEST_CASE("Stream.collect on an empty stream uses the supplier alone")
{
    const int total = cppstream::Stream<int>::of(std::vector<int>{}).collect(summingCollector());
    CHECK(total == 0);
}

TEST_CASE("collect is a terminal operation and consumes the stream")
{
    cppstream::Stream<int> stream = cppstream::Stream<int>::of({1, 2, 3});
    CHECK(std::move(stream).collect(summingCollector()) == 6);

    // The pipeline was moved out by the first call, exactly as Java's streams
    // behave after a terminal operation.
    CHECK_THROWS_AS(static_cast<void>(std::move(stream).collect(summingCollector())),
        cppstream::IllegalStateException);
}

TEST_CASE("toList returns a frozen ArrayList")
{
    const cppstream::ArrayList<int> list = cppstream::Stream<int>::of({3, 1, 2}).toList();

    CHECK(list.size() == 3);
    CHECK(list.get(0) == 3);
    CHECK(list.get(2) == 2);
    CHECK(list.isFrozen());

    // Java's Stream.toList() is documented as unmodifiable, so every mutator has
    // to refuse -- including the ones reached through an iterator.
    cppstream::ArrayList<int> mutableCopy = list;
    CHECK_THROWS_AS(mutableCopy.add(9), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(mutableCopy.set(0, 9), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(mutableCopy.removeAt(0), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(mutableCopy.clear(), cppstream::UnsupportedOperationException);
    CHECK_THROWS_AS(static_cast<void>(mutableCopy.listIterator()->remove()),
        cppstream::UnsupportedOperationException);

    // A copy of a frozen list is also frozen: freeze is a property of the value,
    // not of one handle to it.
    CHECK(mutableCopy.isFrozen());
}

TEST_CASE("sum and average are constrained to arithmetic element types")
{
    CHECK(cppstream::Stream<int>::of({1, 2, 3, 4}).sum() == 10);
    CHECK(cppstream::Stream<double>::of({1.5, 2.5}).sum() == doctest::Approx(4.0));

    const cppstream::Optional<double> average = cppstream::Stream<int>::of({1, 2, 3, 4}).average();
    CHECK(average.get() == doctest::Approx(2.5));

    // Java's IntStream.average() is OptionalDouble and stays empty for no input.
    CHECK(cppstream::Stream<int>::of(std::vector<int>{}).average().isEmpty());
    CHECK(cppstream::Stream<int>::empty().sum() == 0);
}
