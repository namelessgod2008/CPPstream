# CppStream

C++23 reimplementation of the Java `java.util.stream` API and the
`java.util.Collection` hierarchy.

The design is frozen in [DESIGN.md](DESIGN.md). Read that first: it documents the
interface signatures, the 36 deliberate divergences from Java, the places where
the behaviour was verified against a real JDK, and the milestone plan.

## Status

`M0`–`M10` are complete: **223 test cases / 1402 assertions**, all passing in
Debug, `-Werror` and ASan+UBSan configurations, plus 5 compile-failure tests
registered with CTest. The library builds with `-fno-rtti` too.

- `Stream<T>` — 14 intermediate operations, 16 terminal operations, 12 static
  factories. Pull-based, lazy, single-use (enforced at compile time and at run
  time), with `gather()`, `mapMulti()`, `ofRange()` and a `sizeHint()` fast path.
- `Collector<T, A, R>` plus the full `Collectors` vocabulary, and
  `Gatherer<T, A, R>` plus `Gatherers` (`windowFixed`, `windowSliding`, `fold`,
  `scan`).
- The container hierarchy: `Iterable` / `Collection` / `List` / `Set` / `Queue` /
  `Deque` / `Map`, with `ArrayList`, `LinkedList`, `ArrayDeque`, `HashSet`,
  `TreeSet`, `HashMap`, `TreeMap`.
- **Live views**: `subList`, `keySet`, `values` and `entrySet` write through to
  the container they came from, fail fast, and go read-only when taken from a
  `const` container. So do the ordered range views `TreeSet.subSet` / `headSet` /
  `tailSet` and `TreeMap.subMap` / `headMap` / `tailMap`, including the
  `fromInclusive` / `toInclusive` overloads -- their fence rules were checked
  against a real JDK rather than guessed. `descendingSet` / `descendingMap` are in
  too, and they are **views into the same tree**, not reversed-comparer copies:
  writes through them land in the backing container, `descendingSet()` of a
  descending view gives the ascending one back, and every member that swaps its
  two senses does so (`first`/`last`, `pollFirst`/`pollLast`,
  `floor`/`ceiling`, `headSet`/`tailSet`, `subSet`). `TreeMap.navigableKeySet()`
  and `descendingKeySet()` are bounded navigable key views of the same map.
- `std::ranges` in both directions: any range can feed a `Stream`, and CppStream
  containers are ranges themselves.
- Where Java's behaviour is under-specified -- the fence rules of
  `NavigableSubMap` behind `subSet` / `subMap`, and the whole descending family --
  the expectations were read off a real JDK rather than guessed. The probes are
  committed under `tests/jdk/` (`RangeViewProbe`, `DescendingProbe`,
  `DescendingEdgeProbe`) and the assertions they produced live in
  `tests/testRangeViews.cpp`, `tests/testSet.cpp`, `tests/testMap.cpp` and
  `tests/testDescendingViews.cpp`. The descending work was additionally checked by
  diffing a 777-line set transcript and a 606-line map transcript generated from
  both this library and the JDK, byte for byte.
- `Lists` / `Sets` / `Maps` factories, and three runnable examples.

Deliberately still missing, and recorded as such: **parallel streams**. That is
the only gap left. `parallel()` is declared and throws, by request; `mapConcurrent`
is absent with it, because it needs virtual threads to mean anything.

### Quick look

```cpp
#include <cppstream/cppstream.h>

// Laziness holds: nothing runs until the terminal operation.
const auto squares = cppstream::Stream<int>::range(1, 7)
                         .filter([](int value) { return value % 2 == 1; })
                         .map([](int value) { return value * value; })
                         .toArray();   // {1, 9, 25}

// Infinite sources short-circuit on demand.
const auto firstMultipleOfSeven =
    cppstream::Stream<int>::iterate(1, [](int current) { return current + 1; })
        .filter([](int candidate) { return candidate % 7 == 0; })
        .findFirst();                  // Optional<int> holding 7

// Containers bridge back into the pipeline, and stream() is lazy over them.
cppstream::ArrayList<std::string> words{"the", "quick", "brown", "fox", "the"};
auto counts = words.stream().collect(
    cppstream::Collectors::groupingBy<std::string>(
        [](const std::string& word) { return word; },
        cppstream::Collectors::counting<std::string>()));
// counts: HashMap<std::string, std::int64_t> with the == 2

// Range-for and std::ranges work through the read-only iterator path.
int characters = 0;
for (const std::string& word : words) {
    characters += static_cast<int>(word.size());
}

// Containers hand out live views, as in Java: this removes the mapping.
auto keys = counts.keySet();
const bool removed = keys.remove(std::string("fox"));

// gather() brings windowing and scanning into the pipeline.
const auto windows = cppstream::Stream<int>::range(1, 8)
                         .gather(cppstream::Gatherers::windowFixed<int>(3))
                         .toList();   // [1,2,3] [4,5,6] [7]

// mapMulti() is the imperative counterpart of flatMap: push zero or many results.
const auto repeated = cppstream::Stream<int>::of(1, 2, 3)
                          .mapMulti<int>([](int value, cppstream::Downstream<int>& sink) {
                              for (int i = 0; i < value; ++i) sink.push(value);
                          })
                          .toList();   // [1, 2, 2, 3, 3, 3]

// Any std::ranges range can be the source, and the size hint travels with it.
const std::vector<int> raw{5, 3, 1};
const auto sorted = cppstream::Stream<int>::ofRange(raw).sorted().toList();

// subSet is a window onto the same tree: writes go through, and anything outside
// the fence pair is rejected.
cppstream::TreeSet<int> points{10, 20, 30, 40, 50};
auto middle = points.subSet(20, 40);     // [20, 40)
const bool inserted = middle.add(35);    // lands in `points`
middle.add(45);                          // throws IllegalArgumentException

// descendingSet is a *view*, not a sorted copy: it walks the same tree backwards,
// and writes through it land in `points`. Its members flip sense with the walk.
auto backwards = points.descendingSet();             // 50, 40, 35, 30, 20, 10
const int top = backwards.first();                   // 50
const int atMostThirty = backwards.ceiling(30).get();   // 30
backwards.subSet(30, true, 20, false);               // (20, 30] walked downwards

// A map hands out the same kind of key view, writable and navigable.
cppstream::TreeMap<std::string, int> stock{{"apples", 3}, {"pears", 5}};
auto descendingStock = stock.descendingMap();        // pears, apples
auto keys = descendingStock.navigableKeySet();       // "pears", "apples"
const bool soldOut = keys.remove(std::string("pears"));   // removes the mapping
```

Three runnable tours: `./build/bin/cppstream_example_pipelineTour`,
`./build/bin/cppstream_example_wordFrequency` and
`./build/bin/cppstream_example_collectionTour`.

## Requirements

- A C++23 compiler. Developed and verified against `g++ 16.2.1`.
- CMake 3.25 or newer. CLion 2026.2.1 bundles CMake 4.3.1, which is fine.
- Ninja (bundled with CLion) or Make.

## Building

From a shell, using the toolchain CLion ships:

```sh
export PATH="/opt/clion-2026.2.1/bin/cmake/linux/x64/bin:/opt/clion-2026.2.1/bin/ninja/linux/x64:$PATH"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

In CLion, just open the project directory; it picks up `CMakeLists.txt`
automatically.

## Useful CMake options

| Option | Default | Effect |
|---|---|---|
| `CPPSTREAM_BUILD_TESTS` | `ON` | Build the doctest suite and register it with CTest |
| `CPPSTREAM_WARNINGS_AS_ERRORS` | `OFF` | Promote warnings to errors for tests and examples |
| `CPPSTREAM_ENABLE_SANITIZERS` | `OFF` | Add ASan + UBSan to test targets |
| `CPPSTREAM_BUILD_EXAMPLES` | `ON` when top level | Build the programs under `examples/` |

## Using the library

```cpp
#include <cppstream/cppstream.h>
```

That umbrella header is the only supported entry point. Individual headers are
not guaranteed to be self-contained, because `Stream` and the containers have a
mutual dependency that is resolved by declaration ordering (see DESIGN.md
section 7.4). The library is a header-only INTERFACE target:

```cmake
target_link_libraries(your_target PRIVATE cppstream::cppstream)
```

## Naming

camelCase throughout, matching the Java API it mirrors: `CamelCase` for types and
concepts, `camelBack` for functions, variables and enum constants, with a
trailing underscore on private data members. `.clang-format` and `.clang-tidy`
encode the rules.
