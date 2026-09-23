// A word-frequency table: groupingBy + counting, then ranked through the container
// layer. This is the shape most Java stream code actually takes.
//
// Run: ./build/bin/cppstream_example_wordFrequency

#include <cppstream/cppstream.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main() {
    const std::vector<std::string> text{
        "the", "quick", "brown", "fox", "jumps", "over",
        "the", "lazy",  "dog",   "the", "fox",   "jumps",
    };

    // groupingBy with a downstream counting() collector: one pass, one map.
    const auto counts = cppstream::Stream<std::string>::of(text).collect(
        cppstream::Collectors::groupingBy<std::string>(
            [](const std::string& word) { return word; },
            cppstream::Collectors::counting<std::string>()));

    // entrySet() is a live view; toArray() takes the snapshot that std::sort
    // wants. Gathering first is also the only way to sort a map's entries, since
    // a view cannot be reordered.
    std::vector<std::pair<std::string, std::int64_t>> ranked;
    ranked.reserve(static_cast<std::size_t>(counts.size()));
    for (const auto& entry : counts.entrySet()) {
        ranked.emplace_back(entry.getKey(), entry.getValue());
    }
    std::ranges::sort(ranked, [](const std::pair<std::string, std::int64_t>& left,
                                 const std::pair<std::string, std::int64_t>& right) {
        if (left.second != right.second) {
            return left.second > right.second;
        }
        return left.first < right.first;
    });

    std::cout << "distinct words: " << counts.size() << '\n';
    for (const auto& [word, count] : ranked) {
        std::cout << "  " << word << " x" << count << '\n';
    }

    // The same question answered without materialising a map, via toMap.
    const cppstream::HashMap<std::string, std::int64_t> again =
        cppstream::Stream<std::string>::of(text).distinct().collect(
            cppstream::Collectors::toMap<std::string>(
                [&counts](const std::string& word) { return word; },
                [&counts](const std::string& word) { return *counts.get(word); }));
    std::cout << "\ntoMap agrees with groupingBy: " << (counts.equals(again) ? "yes" : "no") << '\n';

    // joining, with prefix and suffix, over the distinct words in order.
    const std::string sentence = cppstream::Stream<std::string>::of(text)
                                     .distinct()
                                     .collect(cppstream::Collectors::joining<std::string>(" ", "<", ">"));
    std::cout << sentence << '\n';
    return 0;
}
