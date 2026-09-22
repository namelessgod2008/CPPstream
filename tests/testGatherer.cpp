#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <string>
#include <vector>

TEST_CASE("windowFixed partitions the stream and keeps the short tail")
{
    const cppstream::ArrayList<cppstream::ArrayList<int>> windows =
        cppstream::Stream<int>::of(std::vector<int>{1, 2, 3, 4, 5, 6, 7})
            .gather(cppstream::Gatherers::windowFixed<int>(3))
            .toList();

    CHECK(windows.size() == 3);
    CHECK(windows.get(0).toArray() == std::vector<int>({1, 2, 3}));
    CHECK(windows.get(1).toArray() == std::vector<int>({4, 5, 6}));
    CHECK(windows.get(2).toArray() == std::vector<int>({7}));

    // An exact multiple produces no trailing window: the finisher sees an empty one.
    CHECK(cppstream::Stream<int>::of(std::vector<int>{1, 2, 3, 4, 5, 6})
              .gather(cppstream::Gatherers::windowFixed<int>(3))
              .count() == 2);

    // Nothing in, nothing out.
    CHECK(cppstream::Stream<int>::empty()
              .gather(cppstream::Gatherers::windowFixed<int>(3))
              .count() == 0);

    CHECK_THROWS_AS(
        static_cast<void>(cppstream::Gatherers::windowFixed<int>(0)),
        cppstream::IllegalArgumentException);
}

TEST_CASE("windowSliding overlaps windows and drops the incomplete tail")
{
    const cppstream::ArrayList<cppstream::ArrayList<int>> windows =
        cppstream::Stream<int>::of(std::vector<int>{1, 2, 3, 4, 5})
            .gather(cppstream::Gatherers::windowSliding<int>(3))
            .toList();

    CHECK(windows.size() == 3);
    CHECK(windows.get(0).toArray() == std::vector<int>({1, 2, 3}));
    CHECK(windows.get(1).toArray() == std::vector<int>({2, 3, 4}));
    CHECK(windows.get(2).toArray() == std::vector<int>({3, 4, 5}));

    // Fewer elements than the window size emits nothing at all, as in Java.
    CHECK(cppstream::Stream<int>::of(std::vector<int>{1, 2})
              .gather(cppstream::Gatherers::windowSliding<int>(3))
              .count() == 0);

    CHECK_THROWS_AS(
        static_cast<void>(cppstream::Gatherers::windowSliding<int>(-1)),
        cppstream::IllegalArgumentException);
}

TEST_CASE("fold emits exactly one element, and emits it even for an empty stream")
{
    const std::vector<int> folded = cppstream::Stream<int>::of(std::vector<int>{1, 2, 3, 4})
                                        .gather(cppstream::Gatherers::fold<int, int>(
                                            int{0}, [](int total, const int& value) {
                                                return total + value;
                                            }))
                                        .toList()
                                        .toArray();
    CHECK(folded == std::vector<int>({10}));

    CHECK(cppstream::Stream<int>::empty()
              .gather(cppstream::Gatherers::fold<int, std::string>(std::string("empty"),
                  [](std::string total, const int& value) {
                      return total + std::to_string(value);
                  }))
              .findFirst()
              .get() == "empty");
}

TEST_CASE("scan emits the initial value and every intermediate accumulation")
{
    const std::vector<int> scanned =
        cppstream::Stream<int>::of(std::vector<int>{1, 2, 3})
            .gather(cppstream::Gatherers::scan<int, int>(
                int{0}, [](int total, const int& value) { return total + value; }))
            .toList()
            .toArray();
    CHECK(scanned == std::vector<int>({0, 1, 3, 6}));

    // On an empty stream the initial value is still the single output.
    CHECK(cppstream::Stream<int>::empty()
              .gather(cppstream::Gatherers::scan<int, int>(
                  int{7}, [](int total, const int& value) { return total + value; }))
              .findFirst()
              .get() == 7);
}

TEST_CASE("a hand-written stateless gatherer works through Gatherers::of")
{
    // Emits every element twice, which is the smallest thing a plain map cannot
    // do: many outputs per input.
    auto duplicated = cppstream::Gatherers::of<int, int>(
        [](cppstream::Void&, const int& element, cppstream::Downstream<int>& downstream) {
            CHECK_FALSE(downstream.isRejecting());
            static_cast<void>(downstream.push(element));
            static_cast<void>(downstream.push(element));
            return true;
        });

    CHECK(cppstream::Stream<int>::of(std::vector<int>{1, 2}).gather(duplicated).toList().toArray() ==
        std::vector<int>({1, 1, 2, 2}));
}

TEST_CASE("an integrator that returns false stops consumption immediately")
{
    std::int64_t pulls = 0;
    auto pullCounter = [&pulls](int value) {
        ++pulls;
        return value + 1;
    };

    auto threeThenStop = cppstream::Gatherers::ofSequential<int, int, int>(
        [] { return 0; },
        [](int& emitted, const int& element, cppstream::Downstream<int>& downstream) {
            static_cast<void>(downstream.push(element));
            return ++emitted < 3;
        });

    const std::vector<int> result = cppstream::Stream<int>::iterate(1, pullCounter)
                                        .gather(std::move(threeThenStop))
                                        .toList()
                                        .toArray();

    CHECK(result == std::vector<int>({1, 2, 3}));
    // Exactly three elements were pulled from an infinite source: the integrator's
    // false stopped the pipeline there and then.
    CHECK(pulls == 3);
}

TEST_CASE("gather stays lazy and terminates on an infinite source")
{
    std::int64_t pulls = 0;
    auto countingSequence = [&pulls](int value) {
        ++pulls;
        return value + 1;
    };

    auto pipeline = cppstream::Stream<int>::iterate(1, countingSequence)
                        .gather(cppstream::Gatherers::windowFixed<int>(3));

    // Intermediate stages do not pull: the pipeline above has not run at all.
    CHECK(pulls == 0);

    const cppstream::ArrayList<cppstream::ArrayList<int>> firstTwo =
        std::move(pipeline).limit(2).toList();

    CHECK(firstTwo.size() == 2);
    CHECK(firstTwo.get(0).toArray() == std::vector<int>({1, 2, 3}));
    CHECK(firstTwo.get(1).toArray() == std::vector<int>({4, 5, 6}));
    CHECK(pulls == 6);
}

TEST_CASE("gather composes with the rest of the pipeline")
{
    const std::vector<int> result = cppstream::Stream<int>::of(std::vector<int>{1, 2, 3, 4, 5})
                                        .gather(cppstream::Gatherers::windowSliding<int>(2))
                                        .map([](const cppstream::ArrayList<int>& window) {
                                            return window.get(0) + window.get(1);
                                        })
                                        .filter([](int sum) { return sum > 5; })
                                        .toList()
                                        .toArray();
    CHECK(result == std::vector<int>({7, 9}));

    // A gatherer can also be built by hand from the class template, without the
    // Gatherers bundle, which is what a caller with a bespoke state type does.
    cppstream::Gatherer<int, std::string, std::string> labelled(
        [] { return std::string("seen:"); },
        [](std::string& state, const int& element, cppstream::Downstream<std::string>& downstream) {
            state += std::to_string(element);
            static_cast<void>(downstream.push(state));
            return true;
        });
    CHECK(cppstream::Stream<int>::of(std::vector<int>{1, 2})
              .gather(labelled)
              .toList()
              .toArray() == std::vector<std::string>({"seen:1", "seen:12"}));
}

TEST_CASE("a consumed stream cannot be gathered again")
{
    auto stream = cppstream::Stream<int>::of(std::vector<int>{1, 2, 3});
    auto pipeline = std::move(stream).gather(cppstream::Gatherers::windowFixed<int>(2));

    CHECK(std::move(pipeline).count() == 2);
    CHECK_THROWS_AS(static_cast<void>(std::move(pipeline).count()), cppstream::IllegalStateException);
}
