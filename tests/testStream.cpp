#include <doctest/doctest.h>

#include <cppstream/cppstream.h>

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

/// A finite source that records how many elements have actually been pulled, so
/// tests can prove that intermediate operations really do nothing until a
/// terminal operation asks for an element.
class CountingSource {
public:
    explicit CountingSource(std::vector<int> values) : values_(std::move(values)) {}

    [[nodiscard]] cppstream::Stream<int> stream() {
        return cppstream::Stream<int>(cppstream::Stream<int>::NextFn(
            [this, index = std::size_t{0}]() mutable -> std::optional<int> {
                if (index >= values_.size()) {
                    return std::nullopt;
                }
                ++pulled_;
                return values_[index++];
            }));
    }

    [[nodiscard]] int pulled() const { return pulled_; }

private:
    std::vector<int> values_;
    int pulled_ = 0;
};

/// doctest has no guaranteed stringification for std::vector, so comparisons are
/// wrapped in a plain bool predicate rather than written inline in a CHECK.
[[nodiscard]] bool sameValues(const std::vector<int>& actual, const std::vector<int>& expected) {
    return actual == expected;
}

}  // namespace

TEST_CASE("intermediate operations stay lazy until a terminal operation runs")
{
    CountingSource source({1, 2, 3, 4, 5});

    auto stream = source.stream()
                      .filter([](int value) { return value % 2 == 0; })
                      .map([](int value) { return value * 10; });

    CHECK(source.pulled() == 0);

    const auto values = std::move(stream).toArray();
    CHECK(source.pulled() == 5);
    CHECK(sameValues(values, {20, 40}));
}

TEST_CASE("filter and map compose in the order they were written")
{
    const auto values = cppstream::Stream<int>::range(1, 7)
                            .filter([](int value) { return value % 2 == 1; })
                            .map([](int value) { return value * value; })
                            .toArray();

    CHECK(sameValues(values, {1, 9, 25}));
}

TEST_CASE("map may change the element type")
{
    const auto lengths =
        cppstream::Stream<std::string>::of(std::string("a"), std::string("bb"), std::string("ccc"))
            .map([](const std::string& value) { return static_cast<int>(value.size()); })
            .toArray();

    CHECK(sameValues(lengths, {1, 2, 3}));
}

TEST_CASE("peek observes elements and may mutate them, but only when pulled")
{
    int observed = 0;

    auto stream = cppstream::Stream<int>::of(std::vector<int>{1, 2, 3})
                      .peek([&observed](int& value) {
                          value *= 2;
                          ++observed;
                      });

    CHECK(observed == 0);

    const auto values = std::move(stream).toArray();
    CHECK(observed == 3);
    CHECK(sameValues(values, {2, 4, 6}));
}

TEST_CASE("flatMap drains each inner stream before pulling the next outer element")
{
    const auto values = cppstream::Stream<int>::of(std::vector<int>{1, 2, 3})
                            .flatMap([](int value) {
                                return cppstream::Stream<int>::of(
                                    std::vector<int>{value, value * 10});
                            })
                            .toArray();

    CHECK(sameValues(values, {1, 10, 2, 20, 3, 30}));
}

TEST_CASE("flatMap stays lazy when the outer stream is infinite")
{
    const auto value = cppstream::Stream<int>::iterate(1, [](int current) { return current + 1; })
                           .flatMap([](int current) {
                               return cppstream::Stream<int>::of(std::vector<int>{current});
                           })
                           .skip(3)
                           .findFirst();

    CHECK(value.get() == 4);
}

TEST_CASE("distinct keeps the first occurrence and preserves encounter order")
{
    const auto values =
        cppstream::Stream<int>::of(std::vector<int>{3, 1, 3, 2, 1, 4}).distinct().toArray();

    CHECK(sameValues(values, {3, 1, 2, 4}));
}

TEST_CASE("sorted orders by natural order or by an explicit comparator")
{
    const auto ascending = cppstream::Stream<int>::of(std::vector<int>{3, 1, 2}).sorted().toArray();
    CHECK(sameValues(ascending, {1, 2, 3}));

    const auto descending = cppstream::Stream<int>::of(std::vector<int>{3, 1, 2})
                                .sorted(cppstream::Comparator<int>::reverseOrder())
                                .toArray();
    CHECK(sameValues(descending, {3, 2, 1}));
}

TEST_CASE("sorted buffers only when the first element is requested")
{
    CountingSource source({3, 1, 2});

    auto stream = source.stream().sorted();
    CHECK(source.pulled() == 0);

    CHECK(std::move(stream).findFirst().get() == 1);
    CHECK(source.pulled() == 3);
}

TEST_CASE("reversed walks the buffered stream backwards")
{
    const auto values =
        cppstream::Stream<int>::of(std::vector<int>{1, 2, 3}).reversed().toArray();

    CHECK(sameValues(values, {3, 2, 1}));
}

TEST_CASE("limit and skip slice the stream")
{
    CHECK(sameValues(cppstream::Stream<int>::range(0, 10).limit(3).toArray(), {0, 1, 2}));
    CHECK(sameValues(cppstream::Stream<int>::range(0, 6).skip(3).toArray(), {3, 4, 5}));
    CHECK(sameValues(cppstream::Stream<int>::range(0, 3).skip(10).toArray(), {}));
    CHECK(sameValues(cppstream::Stream<int>::range(0, 3).limit(10).toArray(), {0, 1, 2}));
}

TEST_CASE("a negative limit or skip is rejected with Java's exception type")
{
    CHECK_THROWS_AS(cppstream::Stream<int>::range(0, 3).limit(-1), cppstream::IllegalArgumentException);
    CHECK_THROWS_AS(cppstream::Stream<int>::range(0, 3).skip(-1), cppstream::IllegalArgumentException);
}

TEST_CASE("takeWhile and dropWhile react at the first failing element")
{
    const auto taken = cppstream::Stream<int>::of(std::vector<int>{1, 2, 3, 1, 2})
                           .takeWhile([](int value) { return value < 3; })
                           .toArray();
    CHECK(sameValues(taken, {1, 2}));

    const auto dropped = cppstream::Stream<int>::of(std::vector<int>{1, 2, 3, 1, 2})
                             .dropWhile([](int value) { return value < 3; })
                             .toArray();
    CHECK(sameValues(dropped, {3, 1, 2}));
}

TEST_CASE("findFirst short-circuits an infinite stream")
{
    int generated = 0;

    const auto value =
        cppstream::Stream<int>::generate([&generated] { return ++generated; })
            .filter([](int candidate) { return candidate % 7 == 0; })
            .findFirst();

    CHECK(value.get() == 7);
    CHECK(generated == 7);
}

TEST_CASE("anyMatch short-circuits an infinite stream")
{
    int generated = 0;

    const bool found = cppstream::Stream<int>::generate([&generated] { return ++generated; })
                           .anyMatch([](int candidate) { return candidate == 5; });

    CHECK(found);
    CHECK(generated == 5);
}

TEST_CASE("match operations follow the quantifier they name")
{
    const auto greaterThanThree = [](int value) { return value > 3; };

    CHECK(cppstream::Stream<int>::range(1, 6).anyMatch(greaterThanThree));
    CHECK_FALSE(cppstream::Stream<int>::range(1, 4).anyMatch(greaterThanThree));

    CHECK(cppstream::Stream<int>::range(4, 6).allMatch(greaterThanThree));
    CHECK_FALSE(cppstream::Stream<int>::range(1, 6).allMatch(greaterThanThree));

    CHECK(cppstream::Stream<int>::range(1, 4).noneMatch(greaterThanThree));
    CHECK_FALSE(cppstream::Stream<int>::range(1, 6).noneMatch(greaterThanThree));
}

TEST_CASE("reduce folds left, and min/max go through a comparator")
{
    const auto total = cppstream::Stream<int>::range(1, 5).reduce(
        [](int left, int right) { return left + right; });
    CHECK(total.get() == 10);

    CHECK(cppstream::Stream<int>::empty()
              .reduce([](int left, int right) { return left + right; })
              .isEmpty());
    CHECK(cppstream::Stream<int>::empty().reduce(0, [](int left, int right) { return left + right; }) ==
          0);

    const auto natural = cppstream::Comparator<int>::naturalOrder();
    CHECK(cppstream::Stream<int>::of(std::vector<int>{4, 1, 3, 2}).min(natural).get() == 1);
    CHECK(cppstream::Stream<int>::of(std::vector<int>{4, 1, 3, 2}).max(natural).get() == 4);
    CHECK(cppstream::Stream<int>::empty().min(natural).isEmpty());
    CHECK(cppstream::Stream<int>::empty().max(natural).isEmpty());
}

TEST_CASE("count and toArray drain the stream")
{
    CHECK(cppstream::Stream<int>::empty().count() == 0);
    CHECK(cppstream::Stream<int>::range(0, 7).count() == 7);
    CHECK(sameValues(cppstream::Stream<int>::range(0, 3).toArray(), {0, 1, 2}));
}

TEST_CASE("a stream can be consumed exactly once")
{
    auto stream = cppstream::Stream<int>::of(std::vector<int>{1, 2, 3});

    CHECK(std::move(stream).count() == 3);
    // The static_cast<void> is required because these terminals are [[nodiscard]]
    // and CHECK_THROWS_* discards the value by design.
    CHECK_THROWS_WITH_AS(static_cast<void>(std::move(stream).count()),
        "stream has already been operated upon or closed", cppstream::IllegalStateException);
    CHECK_THROWS_AS(static_cast<void>(std::move(stream).toArray()),
        cppstream::IllegalStateException);
    CHECK_THROWS_AS(std::move(stream).filter([](int) { return true; }),
        cppstream::IllegalStateException);
}

TEST_CASE("a stream can be built from every Java factory")
{
    CHECK(cppstream::Stream<int>::empty().count() == 0);

    CHECK(sameValues(cppstream::Stream<int>::of(1, 2, 3).toArray(), {1, 2, 3}));
    CHECK(sameValues(cppstream::Stream<int>::of({1, 2, 3}).toArray(), {1, 2, 3}));
    CHECK(sameValues(cppstream::Stream<int>::of(std::vector<int>{1, 2, 3}).toArray(), {1, 2, 3}));

    int* const absent = nullptr;
    CHECK(cppstream::Stream<int*>::ofNullable(absent).count() == 0);

    int present = 9;
    const auto pointers = cppstream::Stream<int*>::ofNullable(&present).toArray();
    REQUIRE(pointers.size() == std::size_t{1});
    CHECK(*pointers[0] == 9);
}

TEST_CASE("concat appends the second stream after the first is exhausted")
{
    const auto values =
        cppstream::Stream<int>::concat(cppstream::Stream<int>::of(std::vector<int>{1, 2}),
            cppstream::Stream<int>::of(std::vector<int>{3, 4}))
            .toArray();

    CHECK(sameValues(values, {1, 2, 3, 4}));
}

TEST_CASE("iterate supports the infinite and the conditioned form")
{
    CHECK(sameValues(
        cppstream::Stream<int>::iterate(1, [](int current) { return current * 2; }).limit(5).toArray(),
        {1, 2, 4, 8, 16}));

    CHECK(sameValues(cppstream::Stream<int>::iterate(1,
                         [](int current) { return current < 5; },
                         [](int current) { return current + 1; })
                         .toArray(),
        {1, 2, 3, 4}));

    // A seed that fails the predicate yields nothing, matching Java's for-loop
    // semantics.
    CHECK(cppstream::Stream<int>::iterate(9,
              [](int current) { return current < 5; },
              [](int current) { return current + 1; })
              .count() == 0);
}

TEST_CASE("range and rangeClosed handle empty and boundary cases")
{
    CHECK(sameValues(cppstream::Stream<int>::range(0, 5).toArray(), {0, 1, 2, 3, 4}));
    CHECK(sameValues(cppstream::Stream<int>::rangeClosed(1, 5).toArray(), {1, 2, 3, 4, 5}));
    CHECK(sameValues(cppstream::Stream<int>::range(5, 5).toArray(), {}));
    CHECK(sameValues(cppstream::Stream<int>::range(5, 0).toArray(), {}));

    const int maximum = std::numeric_limits<int>::max();
    CHECK(sameValues(cppstream::Stream<int>::rangeClosed(maximum, maximum).toArray(), {maximum}));
    CHECK(sameValues(
        cppstream::Stream<int>::rangeClosed(maximum - 1, maximum).toArray(), {maximum - 1, maximum}));
}

TEST_CASE("parallel is declared but not implemented, and sequential is a no-op")
{
    CHECK_FALSE(cppstream::Stream<int>::of(1).isParallel());
    CHECK_FALSE(cppstream::Stream<int>::of(1).sequential().isParallel());

    CHECK(sameValues(cppstream::Stream<int>::of(1, 2, 3).unordered().toArray(), {1, 2, 3}));

    CHECK_THROWS_AS(
        cppstream::Stream<int>::of(1).parallel(), cppstream::UnsupportedOperationException);
}

TEST_CASE("sorted works on a non-trivial element type")
{
    const auto names = cppstream::Stream<std::string>::of(
                           std::string("carol"), std::string("alice"), std::string("bob"))
                           .sorted()
                           .toArray();

    REQUIRE(names.size() == std::size_t{3});
    CHECK(names[0] == "alice");
    CHECK(names[1] == "bob");
    CHECK(names[2] == "carol");
}

TEST_CASE("mapMulti pushes zero, one or many results per element")
{
    // One input, many outputs: Java's canonical flatMap-via-consumer shape.
    const auto mirrored = cppstream::Stream<int>::of(1, 2, 3)
                              .mapMulti<int>([](const int& value, cppstream::Downstream<int>& sink) {
                                  sink.push(value);
                                  sink.push(-value);
                              })
                              .toArray();
    CHECK(sameValues(mirrored, {1, -1, 2, -2, 3, -3}));

    // Many inputs, one output: a hand-rolled filter is just "push nothing".
    const auto evens = cppstream::Stream<int>::range(1, 7)
                           .mapMulti<int>([](const int& value, cppstream::Downstream<int>& sink) {
                               if (value % 2 == 0) {
                                   sink.push(value);
                               }
                           })
                           .toArray();
    CHECK(sameValues(evens, {2, 4, 6}));

    // Zero outputs for every input is legal and yields an empty stream.
    const auto nothing = cppstream::Stream<int>::of(1, 2)
                             .mapMulti<int>([](const int&, cppstream::Downstream<int>&) {})
                             .toArray();
    CHECK(nothing.empty());
}

TEST_CASE("mapMulti is lazy and works for non-trivial element types")
{
    CountingSource source({1, 2, 3});

    auto stream = source.stream().mapMulti<int>(
        [](const int& value, cppstream::Downstream<int>& sink) { sink.push(value * 10); });

    CHECK(source.pulled() == 0);

    const auto values = std::move(stream).toArray();
    CHECK(source.pulled() == 3);
    CHECK(sameValues(values, {10, 20, 30}));

    // The sink is a Downstream<R>, so R is named at the call site; it may differ
    // from the input type, exactly as Java's BiConsumer<... Consumer<R>> allows.
    const auto letters =
        cppstream::Stream<std::string>::of(std::string("ab"), std::string("c"))
            .mapMulti<std::string>(
                [](const std::string& word, cppstream::Downstream<std::string>& sink) {
                    for (const char letter : word) {
                        sink.push(std::string(1, letter));
                    }
                })
            .toArray();
    CHECK(letters == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("mapMulti composes with the rest of the pipeline")
{
    const auto values = cppstream::Stream<int>::range(1, 4)
                            .mapMulti<int>([](const int& value, cppstream::Downstream<int>& sink) {
                                sink.push(value);
                                sink.push(value * 100);
                            })
                            .filter([](int value) { return value > 50; })
                            .sorted()
                            .toArray();
    CHECK(sameValues(values, {100, 200, 300}));
}

TEST_CASE("Optional::stream yields zero or one element")
{
    CHECK(sameValues(cppstream::Optional<int>::of(7).stream().toArray(), {7}));
    CHECK(sameValues(cppstream::Optional<int>::empty().stream().toArray(), {}));
}
