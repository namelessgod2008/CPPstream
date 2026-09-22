# CppStream

**中文** ｜ [English](#cppstream-english)

C++23 复刻 Java 的 `java.util.stream` API 与 `java.util.Collection` 体系。

设计冻结在 [DESIGN.md](DESIGN.md)，建议先读：里面记录了接口签名、37 处刻意与 Java 的偏离、哪些行为是照着真实 JDK 实测校准出来的，以及里程碑计划。

## 状态

`M0`–`M10` 全部完成：**223 个测试用例 / 1402 条断言**，在 Debug、`-Werror`、ASan+UBSan
三种配置下全部通过，另有 5 个编译失败用例接入 CTest。库在 `-fno-rtti` 下同样可以构建。

- `Stream<T>` —— 14 个中间操作、16 个终结操作、12 个静态工厂。拉取式、惰性、单次消费
  （编译期与运行期双重防护），并带 `gather()`、`mapMulti()`、`ofRange()` 与 `sizeHint()` 快路径。
- `Collector<T, A, R>` 与全套 `Collectors`，以及 `Gatherer<T, A, R>` 与 `Gatherers`
  （`windowFixed`、`windowSliding`、`fold`、`scan`）。
- 容器体系：`Iterable` / `Collection` / `List` / `Set` / `Queue` / `Deque` / `Map`，
  实现类有 `ArrayList`、`LinkedList`、`ArrayDeque`、`HashSet`、`TreeSet`、`HashMap`、
  `TreeMap`。
- **活视图**：`subList`、`keySet`、`values`、`entrySet` 都写透到它们所属的容器、fail-fast，
  而从 `const` 容器取到的视图是只读的。有序范围视图 `TreeSet.subSet` / `headSet` /
  `tailSet` 与 `TreeMap.subMap` / `headMap` / `tailMap` 同理，含 `fromInclusive` /
  `toInclusive` 重载 —— 它们的栅栏规则是照着真实 JDK 实测校准的，不是猜的。
  `descendingSet` / `descendingMap` 也在其中，而且是**同一棵树的视图**而非换反向比较器的
  副本：通过它们写进去会落到背后那个容器，对递减视图再取一次 `descendingSet()`
  会得到升序的那一个，所有"换义"的成员也都真的互换（`first`/`last`、`pollFirst`/`pollLast`、
  `floor`/`ceiling`、`headSet`/`tailSet`、`subSet`）。`TreeMap.navigableKeySet()` 与
  `descendingKeySet()` 是同一个 map 上、有界且可导航的键视图。
- `std::ranges` 双向互通：任何 range 都能做 `Stream` 的源，CppStream 的容器本身也是 range。
- Java 行为规定得不清楚的地方 —— `subSet` / `subMap` 背后 `NavigableSubMap` 的栅栏规则，
  以及整个递减系列 —— 期望值都是从真实 JDK 上读出来的，不是凭直觉写的。探针提交在
  `tests/jdk/`（`RangeViewProbe`、`DescendingProbe`、`DescendingEdgeProbe`），它们产出的
  断言落在 `tests/testRangeViews.cpp`、`tests/testSet.cpp`、`tests/testMap.cpp` 与
  `tests/testDescendingViews.cpp`。递减这部分另外做了对拍：本库与 JDK 各生成一份 777 行的
  集合转录和 606 行的映射转录，两份都逐字节相同。
- `Lists` / `Sets` / `Maps` 工厂，以及三个可运行的示例。

刻意仍然缺席、并如实记录的只有**并行流**：`parallel()` 按要求只声明、调用即抛；
`mapConcurrent` 跟着一起缺席，因为离开虚拟线程它就没有意义。

### 快速浏览

```cpp
#include <cppstream/cppstream.h>

// 惰性成立：终结操作之前，一个元素都不会被拉取。
const auto squares = cppstream::Stream<int>::range(1, 7)
                         .filter([](int value) { return value % 2 == 1; })
                         .map([](int value) { return value * value; })
                         .toArray();   // {1, 9, 25}

// 无限源按需短路。
const auto firstMultipleOfSeven =
    cppstream::Stream<int>::iterate(1, [](int current) { return current + 1; })
        .filter([](int candidate) { return candidate % 7 == 0; })
        .findFirst();                  // Optional<int>，值为 7

// 容器可以桥接回管道，stream() 对容器是惰性的。
cppstream::ArrayList<std::string> words{"the", "quick", "brown", "fox", "the"};
auto counts = words.stream().collect(
    cppstream::Collectors::groupingBy<std::string>(
        [](const std::string& word) { return word; },
        cppstream::Collectors::counting<std::string>()));
// counts: HashMap<std::string, std::int64_t>，"the" 的计数为 2

// range-for 与 std::ranges 走只读迭代器那条路径。
int characters = 0;
for (const std::string& word : words) {
    characters += static_cast<int>(word.size());
}

// 容器像 Java 一样交出错落有致的活视图：这一行删掉的是映射本身。
auto keys = counts.keySet();
const bool removed = keys.remove(std::string("fox"));

// gather() 把开窗与扫描带进管道。
const auto windows = cppstream::Stream<int>::range(1, 8)
                         .gather(cppstream::Gatherers::windowFixed<int>(3))
                         .toList();   // [1,2,3] [4,5,6] [7]

// mapMulti() 是 flatMap 的命令式版本：推入零个或多个结果。
const auto repeated = cppstream::Stream<int>::of(1, 2, 3)
                          .mapMulti<int>([](int value, cppstream::Downstream<int>& sink) {
                              for (int i = 0; i < value; ++i) sink.push(value);
                          })
                          .toList();   // [1, 2, 2, 3, 3, 3]

// 任何 std::ranges range 都能做源，size hint 会跟着一起走。
const std::vector<int> raw{5, 3, 1};
const auto sorted = cppstream::Stream<int>::ofRange(raw).sorted().toList();

// subSet 是同一棵树上的窗口：写进去会落到树上，窗口外的值会被拒绝。
cppstream::TreeSet<int> points{10, 20, 30, 40, 50};
auto middle = points.subSet(20, 40);     // [20, 40)
const bool inserted = middle.add(35);    // 落到 `points` 里
middle.add(45);                          // 抛 IllegalArgumentException

// descendingSet 是*视图*而不是排序副本：它把同一棵树倒着走，写进去会落到
// `points`。它的成员意义随方向一起翻转。
auto backwards = points.descendingSet();             // 50, 40, 35, 30, 20, 10
const int top = backwards.first();                   // 50
const int atMostThirty = backwards.ceiling(30).get();   // 30
backwards.subSet(30, true, 20, false);               // (20, 30]，递减方向

// map 交出的键视图同样是可写、可导航的。
cppstream::TreeMap<std::string, int> stock{{"apples", 3}, {"pears", 5}};
auto descendingStock = stock.descendingMap();        // pears, apples
auto stockKeys = descendingStock.navigableKeySet();  // "pears", "apples"
const bool soldOut = stockKeys.remove(std::string("pears"));   // 删掉映射
```

三个可运行的导览：`./build/bin/cppstream_example_pipelineTour`、
`./build/bin/cppstream_example_wordFrequency` 与
`./build/bin/cppstream_example_collectionTour`。

## 环境要求

- C++23 编译器。开发与验证使用的是 `g++ 16.2.1`。
- CMake 3.25 或更新。CLion 2026.2.1 自带 CMake 4.3.1，可以直接用。
- Ninja（CLion 自带）或 Make。

## 构建

在 shell 里，使用 CLion 自带的工具链：

```sh
export PATH="/opt/clion-2026.2.1/bin/cmake/linux/x64/bin:/opt/clion-2026.2.1/bin/ninja/linux/x64:$PATH"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

在 CLion 里直接打开项目目录即可，它会自动识别 `CMakeLists.txt`。

## 有用的 CMake 选项

| 选项 | 默认值 | 作用 |
|---|---|---|
| `CPPSTREAM_BUILD_TESTS` | `ON` | 构建 doctest 测试套件并注册到 CTest |
| `CPPSTREAM_WARNINGS_AS_ERRORS` | `OFF` | 把测试与示例的警告提升为错误 |
| `CPPSTREAM_ENABLE_SANITIZERS` | `OFF` | 给测试目标加上 ASan + UBSan |
| `CPPSTREAM_BUILD_EXAMPLES` | 顶层项目时为 `ON` | 构建 `examples/` 下的程序 |

## 使用这个库

```cpp
#include <cppstream/cppstream.h>
```

这个 umbrella 头是唯一受支持的入口。单个头文件不保证完全自包含，因为 `Stream` 与容器
互相依赖，得靠声明顺序解开（见 DESIGN.md 第 7.4 节）。库是 header-only 的 INTERFACE
target：

```cmake
target_link_libraries(your_target PRIVATE cppstream::cppstream)
```

## 命名

全库 camelCase，与它镜像的 Java API 一致：类型与 concept 用 `CamelCase`，函数、变量、
枚举常量用 `camelBack`，私有数据成员带尾下划线。规则写在 `.clang-format` 与
`.clang-tidy` 里。

---

# CppStream (English)

C++23 reimplementation of the Java `java.util.stream` API and the
`java.util.Collection` hierarchy.

The design is frozen in [DESIGN.md](DESIGN.md). Read that first: it documents the
interface signatures, the 37 deliberate divergences from Java, the places where
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
// counts: HashMap<std::string, std::int64_t> with "the" counting 2

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
auto stockKeys = descendingStock.navigableKeySet();  // "pears", "apples"
const bool soldOut = stockKeys.remove(std::string("pears"));   // removes the mapping
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
