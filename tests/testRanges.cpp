#include <cppstream/cppstream.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <forward_list>
#include <list>
#include <ranges>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr bool isKnown(const cppstream::Stream<int>::SizeHint& hint, std::size_t expected)
{
    return hint.has_value() && *hint == expected;
}

}  // namespace

TEST_CASE("ofRange borrows an lvalue range")
{
    const std::vector<int> values{1, 2, 3, 4};

    CHECK(cppstream::Stream<int>::ofRange(values).toList().toArray() == std::vector<int>({1, 2, 3, 4}));
    CHECK(cppstream::Stream<int>::ofRange(values).map([](int value) { return value * 2; }).sum() == 20);

    const std::list<std::string> words{"a", "bb", "ccc"};
    CHECK(cppstream::Stream<std::string>::ofRange(words).map(
              [](const std::string& word) { return word.size(); })
              .sum() == 6);

    const std::set<int> ordered{3, 1, 2};
    CHECK(cppstream::Stream<int>::ofRange(ordered).toList().toArray() == std::vector<int>({1, 2, 3}));

    // A view composes too, which is the point of accepting any input_range.
    CHECK(cppstream::Stream<int>::ofRange(values | std::views::transform([](int value) {
              return value + 1;
          })).toList().toArray() == std::vector<int>({2, 3, 4, 5}));
}

TEST_CASE("ofRange takes ownership of an rvalue range")
{
    // Nothing outside the stage owns this vector, so the stream has to.
    auto stream = cppstream::Stream<int>::ofRange(std::vector<int>{1, 2, 3});
    CHECK(stream.sizeHint().has_value());
    CHECK(*stream.sizeHint() == 3);
    CHECK(std::move(stream).toList().toArray() == std::vector<int>({1, 2, 3}));

    CHECK(cppstream::Stream<int>::ofRange(std::views::iota(1, 6)).sum() == 15);
}

TEST_CASE("streams carry a size hint from their source")
{
    CHECK(isKnown(cppstream::Stream<int>::empty().sizeHint(), 0));
    CHECK(isKnown(cppstream::Stream<int>::of(std::vector<int>{1, 2, 3}).sizeHint(), 3));
    CHECK(isKnown(cppstream::Stream<int>::of({1, 2, 3, 4}).sizeHint(), 4));
    CHECK(isKnown(cppstream::Stream<int>::of(1, 2).sizeHint(), 2));
    CHECK(isKnown(cppstream::Stream<int>::ofNullable(7).sizeHint(), 1));
    CHECK(isKnown(cppstream::Stream<int>::range(0, 10).sizeHint(), 10));
    CHECK(isKnown(cppstream::Stream<int>::rangeClosed(1, 10).sizeHint(), 10));
    CHECK(isKnown(cppstream::Stream<int>::range(5, 5).sizeHint(), 0));
    CHECK(isKnown(cppstream::Stream<int>::range(5, 1).sizeHint(), 0));

    // Infinite and generated sources cannot promise anything.
    CHECK_FALSE(cppstream::Stream<int>::iterate(0, [](int value) { return value + 1; })
                    .sizeHint()
                    .has_value());
    CHECK_FALSE(cppstream::Stream<int>::generate([] { return 1; }).sizeHint().has_value());

    CHECK(isKnown(cppstream::Stream<int>::concat(
                      cppstream::Stream<int>::of(std::vector<int>{1, 2}),
                      cppstream::Stream<int>::of(std::vector<int>{3}))
                      .sizeHint(),
        3));
    CHECK_FALSE(cppstream::Stream<int>::concat(cppstream::Stream<int>::of(std::vector<int>{1}),
                      cppstream::Stream<int>::iterate(0, [](int value) { return value + 1; }))
                    .sizeHint()
                    .has_value());

    const cppstream::ArrayList<int> container{1, 2, 3, 4, 5};
    CHECK(isKnown(container.stream().sizeHint(), 5));
}

TEST_CASE("size hints survive the stages that preserve the element count")
{
    const std::vector<int> values{1, 2, 3, 4, 5};
    const auto source = [&values] { return cppstream::Stream<int>::of(values); };

    CHECK(isKnown(source().map([](int value) { return value; }).sizeHint(), 5));
    CHECK(isKnown(source().peek([](int&) {}).sizeHint(), 5));
    CHECK(isKnown(source().sorted().sizeHint(), 5));
    CHECK(isKnown(source().reversed().sizeHint(), 5));
    CHECK(isKnown(source().unordered().sizeHint(), 5));

    CHECK(isKnown(source().limit(2).sizeHint(), 2));
    CHECK(isKnown(source().limit(9).sizeHint(), 5));
    CHECK(isKnown(source().limit(0).sizeHint(), 0));

    CHECK(isKnown(source().skip(1).sizeHint(), 4));
    CHECK(isKnown(source().skip(9).sizeHint(), 0));
    CHECK(isKnown(source().skip(0).sizeHint(), 5));

    CHECK(isKnown(source().skip(1).limit(2).sizeHint(), 2));

    // A stage that drops the hint takes every stage after it down with it: limit
    // has nothing left to narrow, and guessing would be worse than saying nothing.
    CHECK_FALSE(source()
                    .filter([](int value) { return value > 2; })
                    .limit(1)
                    .sizeHint()
                    .has_value());
}

TEST_CASE("size hints are dropped by the stages that cannot predict the count")
{
    const std::vector<int> values{1, 2, 3, 4, 5};
    const auto source = [&values] { return cppstream::Stream<int>::of(values); };

    CHECK_FALSE(source().filter([](int value) { return value > 2; }).sizeHint().has_value());
    CHECK_FALSE(source().distinct().sizeHint().has_value());
    CHECK_FALSE(source().takeWhile([](int value) { return value < 3; }).sizeHint().has_value());
    CHECK_FALSE(source().dropWhile([](int value) { return value < 3; }).sizeHint().has_value());
    CHECK_FALSE(source().flatMap([](int value) { return cppstream::Stream<int>::of(value, value); })
                    .sizeHint()
                    .has_value());
    CHECK_FALSE(source().gather(cppstream::Gatherers::windowFixed<int>(2)).sizeHint().has_value());
}

TEST_CASE("the size hint never changes what a pipeline computes")
{
    const std::vector<int> values{5, 3, 1, 4, 2};

    // Same answer with and without a hint.
    CHECK(cppstream::Stream<int>::of(values).sorted().toList().toArray() ==
        std::vector<int>({1, 2, 3, 4, 5}));
    CHECK(cppstream::Stream<int>::iterate(5, [](int value) { return value - 1; })
              .limit(5)
              .sorted()
              .toList()
              .toArray() == std::vector<int>({1, 2, 3, 4, 5}));

    // A hint the source got wrong must not break anything either: ofRange over a
    // range whose size is known is exact, and every consumer still works when it
    // is absent, which the previous test cases cover stage by stage.
    CHECK(cppstream::Stream<int>::range(0, 4).filter([](int value) { return value % 2 == 0; })
              .count() == 2);
}

TEST_CASE("CppStream containers are std::ranges ranges in their own right")
{
    static_assert(std::ranges::input_range<cppstream::ArrayList<int>>);
    static_assert(std::ranges::sized_range<cppstream::ArrayList<int>>);
    static_assert(std::ranges::input_range<cppstream::LinkedList<int>>);
    static_assert(std::ranges::input_range<cppstream::HashSet<int>>);

    const cppstream::ArrayList<int> values{1, 2, 3, 4, 5};

    CHECK(std::ranges::distance(values) == 5);
    CHECK(std::ranges::find(values, 3) != values.end());
    CHECK(std::ranges::count_if(values, [](int value) { return value % 2 == 1; }) == 3);

    std::vector<int> copied;
    std::ranges::copy(values, std::back_inserter(copied));
    CHECK(copied == std::vector<int>({1, 2, 3, 4, 5}));

    // The library's own view types are ranges too.
    const std::vector<int> backing{1, 2, 3, 4, 5};
    const cppstream::ArrayList<int> store{backing};
    const auto window = store.subList(1, 4);
    CHECK(std::ranges::distance(*window) == 3);
}
