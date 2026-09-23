// A tour of the Collection layer: the Queue/Deque family, live views (including
// the descending ones and the navigable key sets), gather()/mapMulti(), and the
// std::ranges bridge.
//
// Run: ./build/bin/cppstream_example_collectionTour

#include <cppstream/cppstream.h>

#include <deque>
#include <iostream>
#include <ranges>
#include <string>
#include <vector>

namespace {

void printSection(const std::string& title)
{
    std::cout << "\n== " << title << " ==\n";
}

void printInts(const cppstream::Collection<int>& values)
{
    std::cout << '[';
    bool first = true;
    for (const int value : values) {
        if (!first) {
            std::cout << ", ";
        }
        first = false;
        std::cout << value;
    }
    std::cout << "]\n";
}

}  // namespace

int main()
{
    printSection("ArrayDeque is a queue, a stack and a deque");
    cppstream::ArrayDeque<std::string> tasks;
    static_cast<void>(tasks.offer("build"));
    static_cast<void>(tasks.offer("test"));
    static_cast<void>(tasks.offerFirst("plan"));
    std::cout << "head: " << tasks.getFirst() << ", tail: " << tasks.getLast() << '\n';
    std::cout << "drained:";
    while (tasks.peek().isPresent()) {
        std::cout << ' ' << tasks.poll().get();
    }
    std::cout << '\n';

    printSection("subList is a live window");
    cppstream::ArrayList<int> numbers{9, 5, 1, 3, 7, 8};
    const auto window = numbers.subList(1, 5);
    std::cout << "window before: ";
    printInts(*window);
    window->sort(cppstream::Comparator<int>::naturalOrder());
    std::cout << "the list after sorting only the window: ";
    printInts(numbers);
    window->set(0, 100);
    std::cout << "writing through the window: ";
    printInts(numbers);

    printSection("map views are live too");
    cppstream::TreeMap<std::string, int> ages;
    ages.put(std::string("ada"), 36);
    ages.put(std::string("alan"), 41);
    auto names = ages.keySet();
    auto values = ages.values();
    std::cout << "keys before: " << names.stream().count() << '\n';
    ages.put(std::string("grace"), 45);
    std::cout << "keys after a put: " << names.stream().count()
              << ", total age: " << values.stream().sum() << '\n';
    std::cout << "removing the mapping for alan through the view: "
              << (names.remove(std::string("alan")) ? "yes" : "no") << '\n';
    std::cout << "map size is now " << ages.size() << '\n';

    printSection("subSet/subMap are live windows onto a sorted container");
    cppstream::TreeSet<int> points{10, 20, 30, 40, 50};
    auto middle = points.subSet(20, 40);
    static_cast<void>(points.add(25));
    std::cout << "the window sees a later insert: ";
    printInts(middle);
    static_cast<void>(middle.add(35));
    std::cout << "the tree sees a write through the window: ";
    printInts(points);
    std::cout << "first/last of the window: " << middle.first() << '/' << middle.last() << '\n';
    std::cout << "floor(100) clamps to the window: " << middle.floor(100).get() << '\n';
    try {
        static_cast<void>(middle.add(45));
    } catch (const cppstream::IllegalArgumentException& error) {
        std::cout << "adding 45 through it fails: " << error.what() << '\n';
    }

    auto slice = ages.subMap(std::string("ada"), std::string("grace"));
    static_cast<void>(slice.put(std::string("bob"), 24));
    std::cout << "a put through the slice reached the map: "
              << (ages.containsKey(std::string("bob")) ? "yes" : "no") << '\n';
    std::cout << "the slice's keys:";
    for (const std::string& key : slice.keySet()) {
        std::cout << ' ' << key;
    }
    std::cout << '\n';

    printSection("descending views: the same tree walked backwards");
    auto backwards = points.subSet(20, 40).descendingSet();
    std::cout << "the same window, descending: ";
    printInts(backwards);
    std::cout << "first/last swap sense (35/20 here): " << backwards.first() << '/'
              << backwards.last() << '\n';
    std::cout << "floor(5) clamps to the window's last: " << backwards.floor(5).get()
              << ", ceiling(100) to its first: " << backwards.ceiling(100).get() << '\n';
    static_cast<void>(backwards.add(28));
    std::cout << "a write through the descending view lands in the tree: ";
    printInts(points);
    std::cout << "flipping it twice gives the ascending window back: ";
    printInts(backwards.descendingSet());

    printSection("navigableKeySet and descendingKeySet");
    auto keys = ages.navigableKeySet();
    std::cout << "keys:";
    for (const std::string& key : keys) {
        std::cout << ' ' << key;
    }
    std::cout << '\n';
    std::cout << "floor(\"b\") stays inside the key set: " << keys.floor(std::string("b")).get()
              << '\n';
    auto descendingKeys = ages.descendingKeySet();
    std::cout << "descending keys:";
    for (const std::string& key : descendingKeys) {
        std::cout << ' ' << key;
    }
    std::cout << '\n';
    std::cout << "removing \"bob\" through the descending key set: "
              << (descendingKeys.remove(std::string("bob")) ? "yes" : "no")
              << ", map size is now " << ages.size() << '\n';

    printSection("gather: windows, folds and scans");
    const std::vector<int> data{1, 2, 3, 4, 5, 6, 7};
    std::cout << "windowFixed(3):";
    for (const cppstream::ArrayList<int>& windowed :
        cppstream::Stream<int>::of(data).gather(cppstream::Gatherers::windowFixed<int>(3)).toList()) {
        std::cout << ' ';
        printInts(windowed);
    }
    std::cout << '\n';

    std::cout << "windowSliding(3) sums:";
    for (const int sum :
        cppstream::Stream<int>::of(data)
            .gather(cppstream::Gatherers::windowSliding<int>(3))
            .map([](const cppstream::ArrayList<int>& slide) { return slide.stream().sum(); })
            .toList()) {
        std::cout << ' ' << sum;
    }
    std::cout << '\n';

    std::cout << "scan (rolling total):";
    for (const int running : cppstream::Stream<int>::of(data)
                                 .gather(cppstream::Gatherers::scan<int, int>(
                                     0, [](int total, const int& value) { return total + value; }))
                                 .toList()) {
        std::cout << ' ' << running;
    }
    std::cout << '\n';

    std::cout << "mapMulti (push one result per unit):";
    for (const int repeated :
        cppstream::Stream<int>::of(std::vector<int>{1, 2, 3})
            .mapMulti<int>([](int value, cppstream::Downstream<int>& sink) {
                for (int i = 0; i < value; ++i) {
                    sink.push(value);
                }
            })
            .toList()) {
        std::cout << ' ' << repeated;
    }
    std::cout << '\n';

    std::cout << "fold (one element out): "
              << cppstream::Stream<int>::of(data)
                     .gather(cppstream::Gatherers::fold<int, int>(
                         0, [](int total, const int& value) { return total + value; }))
                     .findFirst()
                     .get()
              << '\n';

    printSection("std::ranges both ways");
    const std::deque<int> fromStd{4, 8, 15, 16, 23, 42};
    std::cout << "a std::deque read as a Stream: "
              << cppstream::Stream<int>::ofRange(fromStd)
                     .filter([](int value) { return value % 2 == 0; })
                     .sum()
              << '\n';
    std::cout << "size hint of that range: "
              << cppstream::Stream<int>::ofRange(fromStd).sizeHint().value_or(0) << '\n';

    // std::ranges::sort over a CppStream container is deliberately impossible:
    // Iterable::begin() hands out a read-only, single-pass cursor (divergence 18),
    // which keeps `for (const auto& x : list)` from aliasing mutable elements. The
    // supported spelling is to sort through the pipeline instead.
    printInts(cppstream::Stream<int>::ofRange(numbers)
                  .sorted(cppstream::Comparator<int>::naturalOrder())
                  .toList());
}
