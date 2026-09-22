// A tour of the Stream layer: laziness, short-circuiting, infinite sources, and
// the bridge back into the container layer.
//
// Run: ./build/bin/cppstream_example_pipelineTour

#include <cppstream/cppstream.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printSection(const std::string& title) {
    std::cout << "\n== " << title << " ==\n";
}

}  // namespace

int main() {
    const std::vector<int> numbers{5, 3, 8, 1, 9, 3, 7, 2};

    printSection("map / filter / sorted");
    const cppstream::ArrayList<int> evensSquared = cppstream::Stream<int>::of(numbers)
                                                       .filter([](int value) { return value % 2 == 0; })
                                                       .map([](int value) { return value * value; })
                                                       .sorted()
                                                       .collect(cppstream::Collectors::toList<int>());
    for (int value : evensSquared) {
        std::cout << value << ' ';
    }
    std::cout << '\n';

    printSection("groupingBy");
    const auto byRemainder = cppstream::Stream<int>::of(numbers).collect(
        cppstream::Collectors::groupingBy<int>([](int value) { return value % 3; }));
    for (const auto& entry : byRemainder.entrySet()) {
        std::cout << "remainder " << entry.getKey() << ": " << entry.getValue().size() << " values\n";
    }

    printSection("laziness and short-circuiting");
    std::int64_t pulls = 0;
    const auto counted = [&pulls](int value) {
        ++pulls;
        return value * 2;
    };
    // limit() stops the pipeline after two elements, so map runs exactly twice.
    const std::vector<int> limited =
        cppstream::Stream<int>::of(numbers).map(counted).limit(2).toArray();
    std::cout << "pulled " << pulls << " elements for " << limited.size() << " results\n";

    printSection("infinite source + takeWhile");
    // Java's 2-argument iterate is unary, so powers of two are the natural fit; a
    // stateful recurrence goes through generate() instead.
    const std::vector<std::int64_t> powers = cppstream::Stream<std::int64_t>::iterate(
        std::int64_t{1}, [](std::int64_t value) { return value * 2; })
                                                 .takeWhile([](std::int64_t value) { return value < 100; })
                                                 .toArray();
    for (std::int64_t value : powers) {
        std::cout << value << ' ';
    }
    std::cout << '\n';

    printSection("numeric terminals");
    cppstream::TreeSet<int> unique;
    for (int value : numbers) {
        unique.add(value);
    }
    std::cout << "sum=" << unique.stream().sum() << " average=" << unique.stream().average().orElse(0.0)
              << " first=" << unique.first() << " last=" << unique.last() << '\n';

    printSection("teeing two collectors");
    const std::string summary = cppstream::Stream<int>::of(numbers).collect(
        cppstream::Collectors::teeing<int>(cppstream::Collectors::counting<int>(),
            cppstream::Collectors::summingInt<int>([](int value) { return value; }),
            [](std::int64_t count, int sum) {
                return "count=" + std::to_string(count) + " sum=" + std::to_string(sum);
            }));
    std::cout << summary << '\n';

    // Repeated consumption is caught at run time as well as at compile time.
    std::cout << "\nreusing a consumed stream throws: ";
    cppstream::Stream<int> consumed = cppstream::Stream<int>::of({1, 2, 3});
    std::cout << std::move(consumed).count() << " then ";
    try {
        std::cout << std::move(consumed).count();
    } catch (const cppstream::IllegalStateException& error) {
        std::cout << error.what();
    }
    std::cout << '\n';
    return 0;
}
