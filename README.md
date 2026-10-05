# CppStream

**中文** ｜ [English](#cppstream-english)

这是一个给写 C++ 的人用的小工具箱。它让你把一堆东西——一列数字、一筐单词、一张名单——
用一两行话就挑出想要的、改一改、数一数、加一加，剩下那些繁琐的记账活儿它全替你办。

它的用法是照着 Java 里一套很受欢迎的老工具做的，所以写法几乎一样，只是换成了 C++。
整套东西都是现成的文件，加进来就能用，不需要单独编译。

想看得更深，请翻 [DESIGN.md](DESIGN.md)。那本手册记得更细：每一处和 Java 不一样的地方、
为什么这样定、哪些细节是拿真的 Java 一条条比对的，以及接下来打算做什么。

改写之前那份术语更多、更"专业"的旧版说明，我们原样留了一份，放在
[`professionalreadme/README.md`](professionalreadme/README.md)，需要查旧措辞时去那里看。

## 状态

已经全部做完，也全都测过了：**242 个测试用例、1490 条断言**，在最严格的三种检查方式下都
通过；另外还有 5 个"故意写错、看它拦不拦得住"的用例。整套东西由 34 个文件组成，配上 25 个
测试和示例文件，用检查工具扫过一遍，一处警告都没有。

它能替你做什么：

- **一条流水线**：给一串东西，你可以随手筛、随手改、排个序、数一数、加起来。你写一行，它算一行。
- **不着急算**：你先写好要做什么，它先按兵不动；等到你真要用结果的那一刻才动手。所以哪怕是一串
  永远数不完的东西，也能随时喊停，只取前面几个就走。
- **省事的收尾**：把结果攒成一个列表、一个集合、一张对照表（给每样东西配一个值），或者干脆汇总
  成一个数，它都能办。
- **现成的容器**：列表、集合、队列、对照表一应俱全，用法和 Java 里一样。
- **会跟着变的"窗口"**：你可以只看容器里的一段，也可以倒过来看；你的改动会直接落到原来那份数据上，
  而不是另抄一份。
- **两头都通**：C++ 自带的序列可以直接喂给它，它自己的容器也能当普通序列一个个拿出来用。
- **几个工人一起干**：活儿多的时候，可以让几个人分着算，最后答案和一个人慢慢算出来的分毫不差；
  要是碰上永远数不完的东西，它会自己改回一个人算，免得空等。
- **拿真的比对过**：有些细节 Java 自己都写得含糊，我们没拍脑袋，而是把真的 Java 跑一遍照着抄，
  这些对照就放在 `tests/jdk/`。

还差一样没做：`mapConcurrent`。就直说放在这里，不藏着。

### 快速浏览

```cpp
#include <cppstream/cppstream.h>

// 先写好要做什么，此刻它一点活儿都还没干。
const auto squares = cppstream::Stream<int>::range(1, 7)
                         .filter([](int value) { return value % 2 == 1; })
                         .map([](int value) { return value * value; })
                         .toArray();   // {1, 9, 25}

// 数不完的一串，也能只取前面几个就收手。
const auto firstMultipleOfSeven =
    cppstream::Stream<int>::iterate(1, [](int current) { return current + 1; })
        .filter([](int candidate) { return candidate % 7 == 0; })
        .findFirst();                  // Optional<int>，值为 7

// 自家容器可以直接接上流水线：这一行按内容归类并数数。
cppstream::ArrayList<std::string> words{"the", "quick", "brown", "fox", "the"};
auto counts = words.stream().collect(
    cppstream::Collectors::groupingBy<std::string>(
        [](const std::string& word) { return word; },
        cppstream::Collectors::counting<std::string>()));
// counts: HashMap<std::string, std::int64_t>，"the" 的计数为 2

// 也能像普通序列那样一个个拿出来读。
int characters = 0;
for (const std::string& word : words) {
    characters += static_cast<int>(word.size());
}

// 这里拿到的是一扇"窗口"：删掉的东西，原来的容器里就真没了。
auto keys = counts.keySet();
const bool removed = keys.remove(std::string("fox"));

// 按三个一组切开。
const auto windows = cppstream::Stream<int>::range(1, 8)
                         .gather(cppstream::Gatherers::windowFixed<int>(3))
                         .toList();   // [1,2,3] [4,5,6] [7]

// 一样东西可以变成零个、一个或好几个。
const auto repeated = cppstream::Stream<int>::of(1, 2, 3)
                          .mapMulti<int>([](int value, cppstream::Downstream<int>& sink) {
                              for (int i = 0; i < value; ++i) sink.push(value);
                          })
                          .toList();   // [1, 2, 2, 3, 3, 3]

// C++ 自带的序列也能直接当源头。
const std::vector<int> raw{5, 3, 1};
const auto sorted = cppstream::Stream<int>::ofRange(raw).sorted().toList();

// 让几个工人一起算，答案和一个人算的分毫不差。
const auto parallelSum = cppstream::Stream<std::int64_t>::range(0, 1'000'000)
                             .parallel()
                             .map([](std::int64_t value) { return value * value; })
                             .sum();

// 从容器出发时这样写：先说要并行，再写要干的活儿。
cppstream::ArrayList<std::string> names{"carol", "alice", "bob"};
const auto roster = names.parallelStream().collect(
    cppstream::Collectors::joining<std::string>(", "));

// 数不完的一串没法让工人分着算，于是它自动按一个人算——这行能停下来，靠的就是这个。
const auto firstMultipleOfSevenParallel =
    cppstream::Stream<std::int64_t>::iterate(1, [](std::int64_t value) { return value + 1; })
        .parallel()
        .filter([](std::int64_t value) { return value % 7 == 0; })
        .findFirst();

// 这是看向同一份数据的一小扇窗：写进去会落到原来的容器，窗外的值会被挡下。
cppstream::TreeSet<int> points{10, 20, 30, 40, 50};
auto middle = points.subSet(20, 40);     // [20, 40)
const bool inserted = middle.add(35);    // 落进 `points`
middle.add(45);                          // 抛 IllegalArgumentException

// 倒过来看的窗口，背后还是同一份数据，改动照样落回去。
auto backwards = points.descendingSet();             // 50, 40, 35, 30, 20, 10
const int top = backwards.first();                   // 50
const int atMostThirty = backwards.ceiling(30).get();   // 30
backwards.subSet(30, true, 20, false);               // (20, 30]，倒着数

// 对照表也能给出同样的键窗口，可写、可前后翻。
cppstream::TreeMap<std::string, int> stock{{"apples", 3}, {"pears", 5}};
auto descendingStock = stock.descendingMap();        // pears, apples
auto stockKeys = descendingStock.navigableKeySet();  // "pears", "apples"
const bool soldOut = stockKeys.remove(std::string("pears"));   // 删掉这一条
```

四个可以亲手跑一跑的小例子：`./build/bin/cppstream_example_pipelineTour`、
`./build/bin/cppstream_example_wordFrequency`、`./build/bin/cppstream_example_collectionTour`，
再外加一个演示多人并行干活的 `./build/bin/cppstream_example_parallelTour`。

## 环境要求

- 一个支持 C++23 的编译器。我们是用 `g++ 16.2.1` 开发和验证的。
- CMake 3.25 或更新。CLion 2026.2.1 自带的就够用。
- Ninja（CLion 自带）或者 Make。
- 能开线程（"几个工人一起干"那部分要用）。配置脚本已经替你接好了，不用自己动手加参数。

## 构建

在命令行里，用 CLion 自带的工具，照着敲这三步：

```sh
export PATH="/opt/clion-2026.2.1/bin/cmake/linux/x64/bin:/opt/clion-2026.2.1/bin/ninja/linux/x64:$PATH"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

在 CLion 里就更简单：直接打开这个项目文件夹，它会自己认出配置并准备好。

## 有用的 CMake 选项

| 选项 | 默认值 | 作用 |
|---|---|---|
| `CPPSTREAM_BUILD_TESTS` | `ON` | 连测试一起建 |
| `CPPSTREAM_WARNINGS_AS_ERRORS` | `OFF` | 把警告当成错误，逼着代码写干净 |
| `CPPSTREAM_ENABLE_SANITIZERS` | `OFF` | 给测试多加一层更严格的内存检查 |
| `CPPSTREAM_BUILD_EXAMPLES` | 顶层项目时为 `ON` | 连例子一起建 |

## 使用这个库

```cpp
#include <cppstream/cppstream.h>
```

记住这一行就够了，别的文件不用管。整套库都是现成的文件，不需要单独编译；在你的工程里连上它：

```cmake
target_link_libraries(your_target PRIVATE cppstream::cppstream)
```

（"几个工人一起干"用到的那组工人是等你真要用时才请来的；一个从头到尾都单干的程序，不会白请人。）

## 命名

名字一律是"驼峰"式：由几个单词拼起来，每个单词首字母大写，其余小写。具体来说，类型和概念
首字母大写（`CamelCase`），函数、变量和枚举值首字母小写（`camelBack`），私有的成员名字末尾
加一个下划线。规则写在 `.clang-format` 和 `.clang-tidy` 两个配置里，CLion 自带的工具就能按它
检查。顺带一句：还有几十个文件的排版没统一整理，我们打算留到单独一次改动里专门处理。

---

# CppStream (English)

This is a small toolbox for people who write C++. It lets you take a pile of
things -- a column of numbers, a bag of words, a roster of names -- and in a line
or two pick out the ones you want, change them, count them or add them up. The
tiresome bookkeeping is done for you.

It follows a popular old tool from Java, so the way you write it will feel
familiar; only the language is C++. Everything ships as ready-made files you
include and use -- there is nothing separate to compile.

For the deeper story, read [DESIGN.md](DESIGN.md). That manual records every
place we differ from Java and why, which details were checked one by one against
the real Java, and what we plan to do next.

The older, more technical write-up (the version from before this plain-language
rewrite) is kept as-is in
[`professionalreadme/README.md`](professionalreadme/README.md), in case you want the
original wording.

## Status

All done and well tested: **242 test cases, 1490 checks**, passing under three of
the strictest checking modes, plus 5 cases that are deliberately written wrong to
prove the tool catches them. It is 34 files, with 25 test and example files, and a
sweep with the checking tools found not one warning.

What it can do for you:

- **A pipeline**: hand it a line of things and filter, reshape, sort, count or add
  them in one go. You write one line; it does one line's worth of work.
- **Nothing runs too early**: you write down what you want and it waits; only when
  you actually ask for the result does it start. So even an endless line can be
  stopped whenever you like, taking just the first few and leaving the rest.
- **Easy endings**: gather the result into a list, a set, a lookup table (each
  thing paired with a value), or a single number.
- **Ready-made containers**: lists, sets, queues and lookup tables, used the way
  Java does it.
- **Windows that stay live**: look at just one stretch of a container, or look at
  it backwards; your changes land on the original data, not on a copy.
- **Works both ways**: the sequence types built into C++ feed straight in, and its
  own containers can be read one item at a time like any ordinary sequence.
- **Several workers at once**: when there is a lot to do you can split it among
  workers and still get exactly the answer one person would have reached. An
  endless line switches back to a single worker by itself, so it cannot wait
  forever.
- **Checked against the real thing**: where Java's own documents are vague we did
  not guess -- we ran the real Java and copied what it does. Those comparisons sit
  under `tests/jdk/`.

One thing is still deliberately missing: `mapConcurrent`. It is written down here
rather than hidden.

### Quick look

```cpp
#include <cppstream/cppstream.h>

// Write down what you want; at this point it has not done a stroke of work.
const auto squares = cppstream::Stream<int>::range(1, 7)
                         .filter([](int value) { return value % 2 == 1; })
                         .map([](int value) { return value * value; })
                         .toArray();   // {1, 9, 25}

// Even an endless line can stop after the first few results.
const auto firstMultipleOfSeven =
    cppstream::Stream<int>::iterate(1, [](int current) { return current + 1; })
        .filter([](int candidate) { return candidate % 7 == 0; })
        .findFirst();                  // Optional<int> holding 7

// Its own containers plug straight into the pipeline; this sorts and counts them.
cppstream::ArrayList<std::string> words{"the", "quick", "brown", "fox", "the"};
auto counts = words.stream().collect(
    cppstream::Collectors::groupingBy<std::string>(
        [](const std::string& word) { return word; },
        cppstream::Collectors::counting<std::string>()));
// counts: HashMap<std::string, std::int64_t> with "the" counting 2

// It can also be read one item at a time like an ordinary sequence.
int characters = 0;
for (const std::string& word : words) {
    characters += static_cast<int>(word.size());
}

// This is a live window: removing here really removes it from the container.
auto keys = counts.keySet();
const bool removed = keys.remove(std::string("fox"));

// Cut it into groups of three.
const auto windows = cppstream::Stream<int>::range(1, 8)
                         .gather(cppstream::Gatherers::windowFixed<int>(3))
                         .toList();   // [1,2,3] [4,5,6] [7]

// One thing can become zero, one or many.
const auto repeated = cppstream::Stream<int>::of(1, 2, 3)
                          .mapMulti<int>([](int value, cppstream::Downstream<int>& sink) {
                              for (int i = 0; i < value; ++i) sink.push(value);
                          })
                          .toList();   // [1, 2, 2, 3, 3, 3]

// The sequence types built into C++ can be the source too.
const std::vector<int> raw{5, 3, 1};
const auto sorted = cppstream::Stream<int>::ofRange(raw).sorted().toList();

// Several workers share the job and reach exactly the same answer.
const auto parallelSum = cppstream::Stream<std::int64_t>::range(0, 1'000'000)
                             .parallel()
                             .map([](std::int64_t value) { return value * value; })
                             .sum();

// Starting from a container: say you want several workers, then say the job.
cppstream::ArrayList<std::string> names{"carol", "alice", "bob"};
const auto roster = names.parallelStream().collect(
    cppstream::Collectors::joining<std::string>(", "));

// An endless line cannot be split between workers, so it falls back to one
// worker by itself -- which is why this line returns.
const auto firstMultipleOfSevenParallel =
    cppstream::Stream<std::int64_t>::iterate(1, [](std::int64_t value) { return value + 1; })
        .parallel()
        .filter([](std::int64_t value) { return value % 7 == 0; })
        .findFirst();

// A window onto the same data: writes land in the original container, and
// anything outside the window is turned away.
cppstream::TreeSet<int> points{10, 20, 30, 40, 50};
auto middle = points.subSet(20, 40);     // [20, 40)
const bool inserted = middle.add(35);    // lands in `points`
middle.add(45);                          // throws IllegalArgumentException

// A backwards window over the same data; changes still land in the original.
auto backwards = points.descendingSet();             // 50, 40, 35, 30, 20, 10
const int top = backwards.first();                   // 50
const int atMostThirty = backwards.ceiling(30).get();   // 30
backwards.subSet(30, true, 20, false);               // (20, 30], walked backwards

// A lookup table hands out the same kind of key window: writable and walkable.
cppstream::TreeMap<std::string, int> stock{{"apples", 3}, {"pears", 5}};
auto descendingStock = stock.descendingMap();        // pears, apples
auto stockKeys = descendingStock.navigableKeySet();  // "pears", "apples"
const bool soldOut = stockKeys.remove(std::string("pears"));   // removes the entry
```

Four small programs you can run yourself: `./build/bin/cppstream_example_pipelineTour`,
`./build/bin/cppstream_example_wordFrequency`, `./build/bin/cppstream_example_collectionTour`,
plus `./build/bin/cppstream_example_parallelTour` for the several-workers demo.

## Requirements

- A C++23 compiler. We develop and verify with `g++ 16.2.1`.
- CMake 3.25 or newer. The one bundled with CLion 2026.2.1 is fine.
- Ninja (bundled with CLion) or Make.
- Working threads (needed for the several-workers part). The setup files already
  wire this up for you, so there is no flag to add by hand.

## Building

In a shell, using the tools CLion ships, type these three:

```sh
export PATH="/opt/clion-2026.2.1/bin/cmake/linux/x64/bin:/opt/clion-2026.2.1/bin/ninja/linux/x64:$PATH"

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

In CLion it is even simpler: just open the project folder and it sorts itself out.

## Useful CMake options

| Option | Default | Effect |
|---|---|---|
| `CPPSTREAM_BUILD_TESTS` | `ON` | Build the tests too |
| `CPPSTREAM_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as mistakes, to keep the code clean |
| `CPPSTREAM_ENABLE_SANITIZERS` | `OFF` | Add a stricter memory check to the tests |
| `CPPSTREAM_BUILD_EXAMPLES` | `ON` when top level | Build the examples too |

## Using the library

```cpp
#include <cppstream/cppstream.h>
```

That one line is all you need to remember; no other file has to be picked out.
Everything is ready-made and nothing separate is compiled. Point your project at
it:

```cmake
target_link_libraries(your_target PRIVATE cppstream::cppstream)
```

(The workers used by the several-workers part are only called in when you really
need them; a program that stays single-handed never hires anyone.)

## Naming

Names are "hump-shaped": several words joined together, each starting with a
capital and the rest lower case. Concretely, types and concepts start with a
capital (`CamelCase`); functions, variables and enum values start lower
(`camelBack`); private members end with an underscore. The rules live in two
config files, `.clang-format` and `.clang-tidy`, and the tools bundled with CLion
can check them for you. One aside: a few dozen files have not been tidied into
that shape yet, and we intend to do that in a separate change of their own.
