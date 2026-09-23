// A tour of the parallel pipeline: what parallel() speeds up, what it leaves
// sequential, and why the answer never changes.
//
// Run: ./build/bin/cppstream_example_parallelTour

#include <cppstream/cppstream.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

void printSection(const std::string& title) {
    std::cout << "\n== " << title << " ==\n";
}

/// Deliberately expensive enough that the pool is visible, and cheap enough that
/// the example finishes immediately: a few thousand iterations of integer work per
/// element.
[[nodiscard]] std::int64_t expensive(std::int64_t value) {
    std::int64_t accumulator = value;
    for (std::int64_t step = 0; step < 4000; ++step) {
        accumulator = ((accumulator * 1103515245) + 12345) % 2147483647;
    }
    return accumulator;
}

using Clock = std::chrono::steady_clock;

[[nodiscard]] std::int64_t millisecondsSince(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

}  // namespace

int main() {
    std::cout << "shared pool size: " << cppstream::parallelism() << '\n';

    printSection("the same pipeline, sequentially and in parallel");
    const auto startSequential = Clock::now();
    const std::int64_t sequentialSum =
        cppstream::Stream<std::int64_t>::range(0, 20000).map(expensive).sum();
    const std::int64_t sequentialMillis = millisecondsSince(startSequential);

    // parallel() applies from here on: the stages after it batch their elements
    // and run them on the shared pool. Summing happens in slices, in encounter
    // order, so the total is the sequential one exactly.
    const auto startParallel = Clock::now();
    const std::int64_t parallelSum =
        cppstream::Stream<std::int64_t>::range(0, 20000).parallel().map(expensive).sum();
    const std::int64_t parallelMillis = millisecondsSince(startParallel);

    std::cout << "sequential: " << sequentialMillis << " ms\n";
    std::cout << "parallel:   " << parallelMillis << " ms\n";
    std::cout << "same answer: " << std::boolalpha << (sequentialSum == parallelSum) << '\n';

    printSection("encounter order survives batching");
    const auto squares = cppstream::Stream<int>::range(1, 7)
                             .parallel()
                             .map([](int value) { return value * value; })
                             .toArray();
    for (int value : squares) {
        std::cout << value << ' ';
    }
    std::cout << '\n';

    printSection("a container's parallelStream() is the idiomatic entry point");
    const cppstream::ArrayList<std::string> words{"the", "quick", "brown", "fox"};
    const auto joined =
        words.parallelStream().collect(cppstream::Collectors::joining<std::string>("-"));
    std::cout << joined << '\n';

    printSection("an unbounded source stays sequential, so nothing hangs");
    // iterate() has no size, so there is nothing to batch against and the stage
    // pulls one element at a time -- which is also why this terminates.
    const auto firstMultipleOfSeven =
        cppstream::Stream<std::int64_t>::iterate(1, [](std::int64_t value) { return value + 1; })
            .parallel()
            .filter([](std::int64_t value) { return value % 7 == 0; })
            .findFirst();
    std::cout << "first multiple of seven: " << firstMultipleOfSeven.get() << '\n';

    printSection("barriers: stateful stages still run one element at a time");
    // distinct() has to see every element before it can decide, so it is a barrier;
    // the map after it is batched again.
    const auto distinctDoubled = cppstream::Stream<int>::of({3, 1, 3, 2, 1})
                                     .parallel()
                                     .distinct()
                                     .map([](int value) { return value * 2; })
                                     .toArray();
    for (int value : distinctDoubled) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
}
