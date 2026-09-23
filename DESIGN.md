# CppStream 设计文档

用 C++23 复刻 Java `java.util.stream` + `java.util.Collection` 层级。

- 目标语言：C++23（`g++ 16.2.1`）
- 构建：CMake + Ninja，由 CLion 驱动
- 测试：doctest（单头文件，随仓库 vendor，configure 阶段不联网）
- 命名空间：`cppstream`；CMake target：`cppstream::cppstream`

---

## 1. 目标与非目标

### 目标

1. **API 保真**：方法名、语义、异常行为对齐 Java 17+。`stream().filter(...).map(...).collect(Collectors.groupingBy(...))` 应当能直接翻译过来。
2. **惰性求值**：中间操作不产生中间容器，支持无限流与短路（`findFirst` / `anyMatch` 不遍历完）。
3. **单次消费**：`Stream<T>` 是 move-only，一次只能终结一次，重复消费抛 `IllegalStateException`。
4. **统一的 Collection 层级**：`Iterable → Collection → List / Set / Queue`，`Map` 独立分支（同 Java），并提供 `stream()` 桥接。

### 非目标

- **不追求性能**。目标是保真与可读性。每元素一次虚调用/间接调用是可接受成本。快路径只做了两处、而且都是与 Java 同源的：`Stream::sizeHint()` 让 `sorted` / `reversed` / `toArray` 按源尺寸一次分配（§7.8），以及并行流（§7.10）——后者是唯一一处以吞吐为目标的功能，因此它的语义代价也记得最清楚。
- **不做线程安全容器**。与 Java 一致：`ArrayList` / `HashMap` 非线程安全。
- **不做可切分的源**。并行流不做 `Spliterator` 那套切分协议，执行模型见 §7.10；`parallel()` 之后的阶段按批并行，源本身永远顺序拉取。

---

## 2. 命名规范

| 类别 | 规范 | 示例 |
|---|---|---|
| 类 / 结构体 / 概念 | 大驼峰 | `ArrayList`, `HashSet`, `Stream`, `CollectorLike` |
| 枚举类型 | 大驼峰 | `Characteristics` |
| 枚举值 | 小驼峰 | `Characteristics::unordered` |
| 函数 / 方法 | 小驼峰 | `add`, `stream`, `findFirst`, `dropWhile`, `equals` |
| 局部变量 / 参数 | 小驼峰 | `expectedModCount`, `elementCount` |
| 私有数据成员 | 小驼峰 + 尾下划线 | `modCount_`, `next_`, `source_` |
| 模板参数 | Java 风格单字母大写 | `T`, `R`, `A`, `K`, `V`, `E`, `U` |
| 常量 | `constexpr` + 小驼峰 | `constexpr int defaultCapacity = 10;` |
| 头文件 | 大驼峰，与主类型同名 | `ArrayList.h`, `Stream.h` |
| 宏 | 全大写下划线 | `CPPSTREAM_ENABLE_SANITIZERS` |
| 头文件保护 | `#pragma once` | 无守卫宏，避免出现大写下划线标识符 |

尾下划线是必要的：`size()` 方法与 `size` 成员在同一作用域会撞名。这属于在"纯驼峰"约束下必须引入的一个消歧后缀，不是风格偏好。

**头文件后缀用 `.h`**：C++ 标准从未规定后缀，`.h` / `.hpp` 都是社区约定。`.hpp` 的价值在于标记"C++ only"、避免与 C 头文件撞名（Boost 就这么干）。本库是纯 C++23、include 路径自带 `cppstream/` 前缀，不存在撞名风险，因此用更短的 `.h`。

`.clang-format` + `.clang-tidy` 落地约束（CLion 内置这两个工具，无需额外安装；可执行文件路径与调用方式见 §4.1）：

- `readability-identifier-naming`：`ClassCase: CamelCase`，`FunctionCase: camelBack`，`VariableCase: camelBack`，`MemberCase: camelBack`，`EnumConstantCase: camelBack`，`PrivateMemberSuffix: '_'`
- `.clang-format` 基于 Google，`ColumnLimit: 100`，`NamespaceIndentation: None`，`PointerAlignment: Left`

---

## 3. 目录结构

```
CppStream/
├── CMakeLists.txt
├── DESIGN.md
├── README.md
├── .clang-format
├── .clang-tidy
├── cmake/
│   └── CppStreamWarnings.cmake
├── include/cppstream/
│   ├── cppstream.h          # 唯一推荐入口（umbrella）
│   ├── cppstream_fwd.h      # 前向声明集合
│   ├── RuntimeException.h
│   ├── Optional.h
│   ├── Comparator.h
│   ├── Elements.h           # elementEquals / elementHash / requireNonNull
│   ├── Stream.h             # 惰性管道 + 中间/终结操作 + 静态工厂 + sizeHint
│   ├── Iterator.h
│   ├── Iterable.h           # 含 ReadOnlyIterator / ReadOnlySentinel
│   ├── Collection.h
│   ├── AbstractCollection.h # modCount_ / freeze() / 共享算法
│   ├── List.h               # 含 ListView（subList 的活视图）
│   ├── Queue.h              # offer / poll / peek / element
│   ├── Deque.h              # 双端 + 栈操作 + descendingIterator
│   ├── ArrayDeque.h         # 包装 std::deque
│   ├── Set.h
│   ├── AbstractSet.h        # 无序 equals / hashCode
│   ├── Map.h                # Map + AbstractMap 合并 + 三个活视图
│   ├── ArrayList.h          # 同时承载 Stream::toList() 的后置定义
│   ├── LinkedList.h
│   ├── HashSet.h
│   ├── TreeSet.h            # 含 NavigableSet 查询 + TreeSetRangeView（双向）
│   ├── HashMap.h
│   ├── TreeMap.h            # 含 NavigableMap 查询 + TreeMapRangeView + TreeMapKeySetView
│   ├── Collector.h
│   ├── Collectors.h
│   ├── Gatherer.h           # Gatherer / Downstream / Void
│   ├── Gatherers.h          # windowFixed / windowSliding / fold / scan
│   ├── SummaryStatistics.h  # Int / Long / DoubleSummaryStatistics
│   └── Lists.h / Sets.h / Maps.h
├── tests/
│   ├── CMakeLists.txt
│   ├── third_party/doctest/doctest/doctest.h
│   ├── testSmoke.cpp
│   ├── testException.cpp
│   ├── testOptional.cpp
│   ├── testComparator.cpp
│   ├── testStream.cpp
│   ├── testArrayList.cpp / testLinkedList.cpp
│   ├── testListView.cpp     # subList 活视图
│   ├── testRangeViews.cpp   # subSet / subMap 活视图（期望值取自 JDK 实测）
│   ├── testDescendingViews.cpp  # descendingSet / descendingMap / navigableKeySet（期望值取自 JDK 实测）
│   ├── testDeque.cpp        # Queue / Deque / ArrayDeque
│   ├── testSet.cpp / testMap.cpp / testMapViews.cpp
│   ├── testCollector.cpp / testCollectors.cpp / testGatherer.cpp
│   ├── testRanges.cpp       # sizeHint 与 std::ranges 互操作
│   ├── testUtilities.cpp
│   ├── compile_fail/        # 5 个编译失败用例，WILL_FAIL 接入 CTest
│   └── jdk/                 # 打印 JDK 行为的探针，测试期望值的来源
│       ├── RangeViewProbe.java      # 范围视图的栅栏规则
│       ├── DescendingProbe.java     # descendingSet / descendingMap 全量行为
│       └── DescendingEdgeProbe.java # 递减视图的边角与异常消息
└── examples/
    ├── pipelineTour.cpp
    ├── collectionTour.cpp   # 容器层 + 活视图 + gather + ranges
    └── wordFrequency.cpp
```

**入口约定**：`#include <cppstream/cppstream.h>` 是唯一保证可用的入口。单头文件不保证完全自包含——原因见 §7.4 的循环包含处理。同时提供 `cppstream_fwd.h` 供只需要前向声明的用户。

**关于 Java 的静态工厂**：`Stream.of` / `Stream.iterate` / `Stream.generate` / `IntStream.range` 在本库中全部变成 `Stream<T>` 的静态成员。Java 需要一个独立的 `StreamSupport` 工具类是因为它的静态方法挂在接口上；C++ 的元素类型由类模板参数给出（`Stream<int>::range(0, 10)`），不需要额外容器，因此原设计中的 `Streams.h` 被取消。`Optional` 的静态工厂同理直接放在 `Optional<T>` 上，`Optionals.h` 亦取消。

---

## 4. 构建配置

```cmake
cmake_minimum_required(VERSION 3.25)
project(CppStream VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cppstream INTERFACE)
add_library(cppstream::cppstream ALIAS cppstream)
target_include_directories(cppstream INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(cppstream INTERFACE cxx_std_23)

option(CPPSTREAM_BUILD_TESTS       "Build tests"        ON)
option(CPPSTREAM_BUILD_EXAMPLES    "Build examples"     ON)
option(CPPSTREAM_ENABLE_SANITIZERS "ASan + UBSan"       OFF)
```

- **INTERFACE（header-only）**：模板密集的库做成静态库需要显式实例化，收益不抵复杂度。缺点是用例编译时间偏长，用 `examples` 的编译时间来监控。
- 警告列表单独放进 `cppstream_warnings` INTERFACE target，只挂到 tests/examples 上，不污染使用者：`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wold-style-cast -Wnon-virtual-dtor`。`-Wnon-virtual-dtor` 对本库尤其重要（虚基类层级）。
- 测试用 `include(CTest)` + 普通 `add_test`，不用 `doctest_discover_tests`（后者依赖 doctest 仓库的 CMake 模块；vendor 单头文件时可以直接 `add_test(NAME cppstream_tests COMMAND cppstream_tests)`，CLion 里点运行按钮的体验更好）。
- Sanitizer 打开时额外挂 `-fsanitize=address,undefined -fno-omit-frame-pointer`。

### 4.1 clang-format / clang-tidy（CLion 自带，无需安装）

`which clang-format` 是空的，但 CLion 2026.2 自己带了一套（本机路径）：

| 工具 | 路径 | 版本 |
|---|---|---|
| clang-format | `/opt/clion-2026.2.1/plugins/clion-radler/DotFiles/linux-x64/clang-format` | 22.0.0git |
| clang-tidy | `/opt/clion-2026.2.1/bin/clang/linux/x64/bin/clang-tidy` | 23.0.0git |

```sh
CF=/opt/clion-2026.2.1/plugins/clion-radler/DotFiles/linux-x64/clang-format
CT=/opt/clion-2026.2.1/bin/clang/linux/x64/bin/clang-tidy
GCC_INC=/usr/lib/gcc/x86_64-pc-linux-gnu/16/include   # 见下面第 2 条

# 格式化整份文件：在仓库根目录执行，工具自己往上找 .clang-format
# （历史文件会被顺带重排，见下面"现状"，所以改老文件时更常用下面这条）
"$CF" -i include/cppstream/Stream.h

# 只格式化自己改动的行（行号取 git diff 里 @@ 头"新文件侧"的范围）
"$CF" -i --lines=92:114 --lines=1115:1383 include/cppstream/Stream.h

# 静态检查：头文件直接当 TU 喂进去最快，整库 TU 会跑很久
"$CT" --config-file=.clang-tidy include/cppstream/Parallel.h -- \
    -std=c++23 -Iinclude -x c++-header -isystem "$GCC_INC"

# 测试 / 示例是普通 TU
"$CT" --config-file=.clang-tidy tests/testParallel.cpp -- \
    -std=c++23 -Iinclude -Itests/third_party/doctest -isystem "$GCC_INC"
```

两个踩过的坑，记在这里免得再踩：

1. **`.clang-format` 里只能写 `Standard: Latest`，不能写 `c++23`。** clang-format 的 `Standard` 枚举只到 `c++20`，写 `c++23` 会让整份配置解析失败（`unknown enumerated scalar`）；更麻烦的是读不动的配置会让 IDE **静默退回默认样式**，看起来"有配置"其实没生效。`Latest` 是"本版本支持的最新标准"，语义等价，且不会随工具升级失效。
2. **命令行跑 clang-tidy 要补一个 `-isystem`。** CLion 的 `bin/clang/linux/x64/bin` 下只有 `clang-tidy` / `clangd` / `clazy-standalone` / `llvm-symbolizer`，没有 clang 的 resource dir（`lib/clang/<ver>/include/stddef.h`），于是 clang-tidy 以 `'stddef.h' file not found` 致命错误收尾。致命错误会让 AST 不完整，还会顺带报出假告警（例如 `readability-convert-member-functions-to-static`）。补上 GCC 的 include 目录即可；CLion 自己的 clang-tidy 集成处理了这件事，只有命令行需要。

**M11 结束时的状态**：

- 本次改动的文件都已按 `.clang-format` 排过（新文件整份排，改动文件只排碰过的 hunk）；`clang-tidy` 在 `Parallel.h`、`testParallel.cpp` 上是**零告警**，`Stream.h` 里唯一一条 `bugprone-easily-swappable-parameters`（构造函数里 `SizeHint` / `Budget` 同型、可互换）用带说明的 `NOLINTNEXTLINE` 显式记下，没有偷偷绕过去。
- 历史代码不干净：44 个文件、约 1300 行（一方代码共 15272 行）与 `.clang-format` 的期望不一致；整库重排会产出一份横跨 50 个文件的大 diff，所以没顺手做——要做应当单独一次提交。典型差异是 lambda 形参对齐、`->` 返回类型的换行位置、`#include` 分组顺序。
- `clang-tidy` 还有 158 条告警（每个头文件单独跑一遍，同一条只在其所属文件的 TU 里计一次；`Stream.h` 26 条、`Collectors.h` 35 条、`TreeSet.h` 30 条）。前排全是纯风格规则：`readability-redundant-typename` 67、`readability-redundant-lambda-parameter-list` 46（`[x]() mutable -> T` 是本库统一写法）、`readability-use-std-min-max` 12、`readability-named-parameter` 9，其余都是个位数。这些要一起决定——关掉，还是全库修一遍；零散改只会让风格更不一致。（本次新增代码里只剩 3 条这类风格告警，都在 `tests/testParallel.cpp`，为了跟既有写法一致故意留着。）

---

## 5. 核心设计决策

### 5.1 分层与依赖方向

```
RuntimeException / Optional / Comparator        （零依赖，基础层）
        ↑
Iterator → Iterable → Collection → List / Set    （接口层，只用 Iterator）
        ↑                                    ↑
ArrayList / LinkedList / ...            Stream 内核（pull 管道）
        ↑                                    ↑
        └────── Collectors / Streams ────────┘
```

**Stream 与 Collection 解耦**：Stream 内核不依赖任何容器。它唯一的桥接点是 `Collection::stream()`，实现方式是拿 `Iterator<T>` 当数据源。这意味着 Stream 可以先于 Collection 独立开发和测试（用 `std::vector` 当源即可），是排期上的关键松弛点。

### 5.2 【最重要的一处偏离】返回值一律用具体类型

Java 可以写 `List<Integer> toList()`、`Map<K,V> groupingBy(...)`，因为对象是引用 + GC 负责生命周期。C++ 值语义下 `List<T>` 是抽象类，**按值返回不可能**（切片 / 无法实例化）。

本库采用：

| 场景 | 做法 |
|---|---|
| 多态**参数** | 接口引用：`void printAll(const List<int>& list)` |
| 多态**持有** | 接口指针：`std::unique_ptr<Set<K>>`（仅当确实需要多态时） |
| **返回值** | 具体类型：`ArrayList<T>`、`HashSet<T>`、`HashMap<K,V>` |
| 成员函数内部 | 全程虚函数分派，接口完整性不受影响 |

代价：`Stream<T>::toList()` 的静态返回类型是 `ArrayList<T>` 而不是 `List<T>`。用 `auto` 时完全无感；显式写类型时多敲几个字符。收益是不引入堆分配、不引入 `shared_ptr`、不违反 C++ 值语义。

这一条是全库唯一"结构级"的偏离，其余偏离都是局部的（见 §8）。

### 5.3 Stream 内核：拉取式 + 类型擦除

```cpp
template <class T>
class Stream {
public:
    using NextFn = std::move_only_function<std::optional<T>()>;  // 拉一个，空 = 流结束

private:
    NextFn next_;   // 空表示已被消费或已关闭

public:
    explicit Stream(NextFn next);
    Stream(Stream&&) noexcept            = default;
    Stream& operator=(Stream&&) noexcept = default;
    Stream(const Stream&)                = delete;   // 单次消费的第一道闸

    template <class F> auto map(F mapper) &&;
    template <class P> Stream<T> filter(P predicate) &&;
    // ... 所有中间/终结操作都限定 && 或 const&&
};
```

**为什么是拉取（pull）而不是推送（push）**：

- 短路语义免费。`findFirst` / `anyMatch` 直接停止拉取即可。推送模型需要在 sink 上挂 `cancellationRequested` 标志位（这正是 JDK 的做法，但 C++ 里没必要）。
- 无限流天然支持。`iterate` / `generate` 不需要任何特殊处理。
- `flatMap` 只需要在闭包里持有一个"当前内层流"，不用实现回压。

**类型擦除**：`Stream<T>` 是单一具体类（与 Java 的 `Stream<T>` 一致），每个阶段用 `std::move_only_function` 把上一层闭包打包进去。`std::function` 在这里不可用——它要求可拷贝，装不下 move-only 的管道状态。这是选择 C++23 而非 C++17 的硬性理由。

代价：每元素一次间接调用。可接受。

`sorted` / `distinct` / `reversed` 这类有状态操作，在闭包里用 `std::make_shared` 持有状态（`std::unordered_set`、缓冲 vector、比较器等），`prev` 闭包与状态一起被 move 进下一层。每阶段一次堆分配，与 Java 的 `ReferencePipeline` 节点分配等价。

**为什么不基于 `std::views`**：`views` 是多趟（multi-pass）、可拷贝的，与"Stream 只能消费一次"直接冲突；`sorted` / `distinct` / `groupingBy` 也没有现成对应；而且 views 的性能优势在这个设计目标下不成立。用 views 等于同时放弃语义和收益。

### 5.4 单次消费：编译期 + 运行期双层防护

```cpp
auto names = ArrayList<std::string>{"alice", "bob"};
auto s = names.stream();          // s 是 lvalue
s.filter(p);                      // 编译错误：filter 是 && 限定的
s = std::move(s).filter(p);       // 合法

auto n = std::move(s).count();    // 消费掉
std::move(s).count();             // 运行期 IllegalStateException
```

- **编译期**：`&&` 限定成员函数拦掉"对 lvalue 直接调链式方法"这个最常见误用。
- **运行期**：`next_` 被移走后置空，任何后续操作抛 `IllegalStateException("stream has already been operated upon or closed")`——与 Java 报错信息一致，且**不是 UB**。

比 Java 严格：Java 全是运行期检查，我们有一半能提前到编译期。

### 5.5 Optional 的形态

**默认方案：自建 `Optional<T>`，内部持有一个 `std::optional<T>`。**

理由：Java `Optional` 的方法名与 `std::optional` 差异过大，只做别名保真度不够。

| Java | `std::optional` | 自建 `Optional<T>` |
|---|---|---|
| `isPresent()` | `has_value()` | `isPresent()` |
| `isEmpty()` | `!has_value()` | `isEmpty()` |
| `get()` → 空则抛 `NoSuchElementException` | `value()` → 抛 `bad_optional_access` | `get()` |
| `orElse(v)` | `value_or(v)` | `orElse(v)` |
| `orElseGet(fn)` | `or_else(fn)`（语义略异） | `orElseGet(fn)` |
| `orElseThrow(fn)` | 无 | `orElseThrow(fn)` |
| `ifPresent(fn)` / `ifPresentOrElse` | 无 | 有 |
| `map` / `filter` / `flatMap` | `transform` / 无 / `and_then` | 同名 |
| `stream()` | 无 | 有 |

自建类额外提供 `unwrap()` / `fromStd()` 与 `std::optional` 互转，并且**隐式转换构造**，避免生态割裂。`Stream::findFirst()` / `max()` / `min()` / `reduce()` 返回它。

### 5.6 异常层级

不做 Java 的 checked/unchecked 之分——C++ 没有这套机制，硬模拟只会变成噪音。所有异常统一派生自 `RuntimeException : std::runtime_error`，与 Java 中它们都是 unchecked 的事实一致：

```
RuntimeException : std::runtime_error
├── IllegalStateException          // 流已消费、Map.put 无值可返回
│   └── IllegalCollectorStateException  // toMap 重复键（Java 抛裸的 IllegalStateException，这里细分但保持可捕获）
├── NoSuchElementException         // Iterator.next() 越界、Optional.get() 为空
├── UnsupportedOperationException  // 不可变容器被修改、Iterator.remove 不支持
├── NullPointerException           // 容器拒绝 null
├── IllegalArgumentException       // limit / skip 为负
│   └── IndexOutOfBoundsException  // List.get 下标越界
├── ConcurrentModificationException// fail-fast
└── ArithmeticException            // 数值运算溢出（sum 溢出检测等）
```

两个子类都保持了 Java 的捕获兼容性：捕获 `IllegalStateException` 仍能拿到重复键异常，捕获 `IllegalArgumentException`（或 C++ 惯用的 `IndexOutOfBoundsException`）仍能拿到下标越界。实测用例断言了这两条继承关系。

`Iterator::next()` 越界抛 `NoSuchElementException`，与 Java 一致。

### 5.7 Comparator 的接口形态

`Comparator<T>` 是一个持有 `std::function<int(const T&, const T&)>` 的具体类，比较契约是 Java 的（负数 / 0 / 正数），不是 C++ 的（bool）。工厂与组合子齐全：`naturalOrder` / `reverseOrder` / `comparing`（含显式 key comparator 重载）/ `reversed` / `thenComparing`（三重重载）/ `nullsFirst` / `nullsLast`。

两处与 Java 的差异：

1. **额外提供 `operator()`**，返回 `compare(...) < 0`。这是唯一一处主动加的 C++ 惯用法成员，理由是硬性的：不能交给 `std::sort`、`std::ranges` 算法和 `std::map` 的比较器在这个语言里没有使用价值。`compare()` 仍然保留，Java 风格的调用照常可用。
2. **不提供 `equals`**。Java 的 `Comparator.equals` 默认是引用同一性，而 `std::function` 无法有意义地复刻这个语义。测试里改用 `compare()` 在样本值上验证。

`Comparator<T>` 是同质的（T 对 T），与 Java 的 `Comparator<T>` 一致；没有复刻 `comparingInt` / `comparingLong` / `comparingDouble`，理由与砍掉 `IntStream` 族相同（§8 第 7 条）——那些方法存在的唯一目的是避免装箱。

`naturalOrder()` 用 `operator<` 而不是 `std::compare_three_way` 定义，这样三向比较符之前写好的类型也能直接用。

另有一个自由函数 `comparatorFrom<T>(const Compare&)`，把**任意 std 风格**的 bool 比较器（即 `TreeSet<T, Compare>` / `TreeMap<K, V, Compare>` 的 `Compare` 模板参数）桥接成 Java 风格的 `Comparator<T>`。`TreeSet::comparator()` / `TreeMap::comparator()` / 两个范围视图的 `comparator()` / `navigableKeySet()` 的 `comparator()` 全部返回 `Comparator<T>`——与 Java 的返回类型一致——靠的就是它（§11 第 23 条）。

---

## 6. Collection 层接口签名

接口用**抽象基类 + 虚函数**，不用 concepts/CRTP：`const List<int>&` 这种多态引用是 Java Collection 的精髓，概念做不到这一点。

关键可行性检查：`stream()` 返回的是具体类 `Stream<T>`，返回类型**不依赖调用方的 lambda 类型**，所以不需要虚模板函数——虚函数完全够用，没有阻塞点。

### 6.1 Iterator（fail-fast）

```cpp
template <class T>
class Iterator {
public:
    virtual ~Iterator() = default;
    virtual bool hasNext() const = 0;
    virtual T& next() = 0;                                    // 越界抛 NoSuchElementException
    virtual void remove() = 0;                                // 默认抛 UnsupportedOperationException
    virtual void forEachRemaining(std::function<void(T&)> action) = 0;
};
```

`remove()` 默认实现抛 `UnsupportedOperationException`，与 Java 的 optional operation 语义一致；`ArrayList` / `LinkedList` / `HashMap` 的迭代器支持它。

**fail-fast**：`AbstractCollection<T>` 持有 `int modCount_`，所有结构性修改自增。迭代器构造时记录 `expectedModCount_`，`next()` / `remove()` 检测不一致时抛 `ConcurrentModificationException`。迭代器持有容器裸指针（与 Java 一样：容器先析构再迭代迭代器是 UB，文档标注）。

### 6.2 Iterable / Collection

```cpp
template <class T>
class Iterable {
public:
    virtual ~Iterable() = default;
    virtual std::unique_ptr<Iterator<T>> iterator() = 0;              // 非 const，交出 T&
    virtual std::unique_ptr<Iterator<const T>> constIterator() const = 0;
    virtual void forEach(std::move_only_function<void(T&)> action);   // 回调拿可写引用
    // C++ 互操作糖：range-for 与 std::ranges 算法可用，只读且单趟
    ReadOnlyIterator<T> begin() const;  ReadOnlySentinel end() const;
};

template <class T>
class Collection : public Iterable<T> {
public:
    virtual int  size() const = 0;                       // Java 也是 int
    virtual bool isEmpty() const = 0;
    virtual bool contains(const T& element) const = 0;
    virtual bool add(const T& element) = 0;
    virtual bool add(T&& element) = 0;
    virtual bool remove(const T& element) = 0;
    virtual bool containsAll(const Collection<T>& other) const = 0;
    virtual bool addAll(const Collection<T>& other) = 0;
    virtual bool removeAll(const Collection<T>& other) = 0;
    virtual bool retainAll(const Collection<T>& other) = 0;
    virtual void clear() = 0;
    virtual std::vector<T> toArray() const = 0;

    // 桥接：虚函数 + 基类默认实现（等价 Java 的 default method），
    // 用 iterator() 实现一次，所有容器共享，子类可覆写
    virtual Stream<T> stream() const;
    virtual bool equals(const Collection<T>& other) const;   // 逐元素比较
    virtual std::size_t hashCode() const;                     // 逐元素组合
};
```

`stream()` 只需实现一次：从只读迭代器拉取，因此天然惰性，且迭代期间修改容器会触发 fail-fast——与 Java 行为一致。它顺带把容器的 `size()` 交给 `Stream::sizeHint`（§7.8）。注意 `Collection` 上没有 `toList()`（Java 也没有），那属于 `Stream` 与 `Lists`。

上面是最终签名，与初稿的两处差异（`iterator()` 的非 const / 只读二分、`forEach` 拿 `T&`）在 §6.6 说明。

### 6.3 List

```cpp
template <class T>
class List : public Collection<T> {
public:
    virtual T&       get(int index) = 0;                  // 越界抛 IndexOutOfBoundsException
    virtual const T& get(int index) const = 0;
    virtual T        set(int index, const T& element) = 0; // 返回被覆盖的旧元素
    virtual void     add(int index, const T& element) = 0;
    virtual T        removeAt(int index) = 0;              // ★ Java 的 remove(int)
    virtual int      indexOf(const T& element) const = 0;
    virtual int      lastIndexOf(const T& element) const = 0;
    virtual std::unique_ptr<ListIterator<T>> listIterator() = 0;
    virtual std::unique_ptr<ListView<T>> subList(int, int);        // ★ 活视图，M8 起
    virtual std::unique_ptr<ListView<T>> subList(int, int) const;  // ★ 只读活视图
};

`subList` 的两个重载按 const 分裂：从非 const list 取到可写视图，从 const list 取到只读视图（§8 第 35 条）。**只读会沿视图链传递**：只读视图上再取 `subList` / `subSet` / `subMap` 得到的是只读视图而不是异常，这正是 Java `Collections.unmodifiableList(l).subList(a, b)` 的行为。返回 `unique_ptr` 而不是值，因为视图本身没有可拷贝的状态——两个副本会指向同一个窗口，那是迭代器级别的语义，不是容器级别的。
```

`get` 返回 `T&`：Java 的 `List.get` 返回引用型对象，Java 里可以直接改它引用的对象，C++ 里对应的就是返回可写引用。

### 6.4 Set / Map

```cpp
template <class T>
class Set : public AbstractCollection<T> { /* 无新增虚函数，语义约束：元素唯一 */ };

template <class K, class V>
class Map {
public:
    class Entry;                                       // getKey / getValue / setValue / equals / hashCode
    virtual ~Map() = default;
    virtual int  size() const = 0;
    virtual bool isEmpty() const;                       // 默认实现：size() == 0
    virtual bool containsKey(const K& key) const = 0;
    virtual bool containsValue(const V& value) const = 0;
    virtual const V* get(const K& key) const = 0;       // ★ nullptr 表示不存在
    virtual V*       get(const K& key) = 0;
    virtual V  getOrDefault(const K& key, const V& defaultValue) const;
    virtual Optional<V> put(const K& key, const V& value) = 0;      // ★ 空 = 原本没有映射
    virtual Optional<V> put(K&& key, V&& value) = 0;
    virtual Optional<V> putIfAbsent(const K& key, const V& value) = 0;
    virtual Optional<V> putIfAbsent(K&& key, V&& value) = 0;
    virtual void putAll(const Map<K, V>& other);
    virtual Optional<V> remove(const K& key) = 0;
    virtual void clear() = 0;
    virtual Optional<V> computeIfAbsent(const K&, const std::function<Optional<V>(const K&)>&) = 0;
    virtual Optional<V> computeIfPresent(const K&, const std::function<Optional<V>(const K&, const V&)>&) = 0;
    virtual Optional<V> merge(const K&, const V&, const std::function<Optional<V>(const V&, const V&)>&) = 0;
    virtual void forEach(const std::function<void(const K&, V&)>& action);
    int modCount() const;                               // C++ 扩展，供视图 fail-fast（§7.7 前身）
    void freeze();  bool isFrozen() const;              // C++ 扩展，见 §8 第 26 条
    KeySetView<K,V>   keySet();      KeySetView<K,V>   keySet()   const;   // ★ 活视图
    ValuesView<K,V>   values();      ValuesView<K,V>   values()   const;   // ★ 活视图
    EntrySetView<K,V> entrySet();    EntrySetView<K,V> entrySet() const;  // ★ 活视图
    virtual bool equals(const Map<K, V>& other) const;
    virtual std::size_t hashCode() const;
protected:
    virtual void visitEntries(const std::function<void(const K&, V&)>&) = 0;
    virtual void visitEntries(const std::function<void(const K&, const V&)>&) const = 0;
};
```

`put` 返回"旧值"这件事在 C++ 里没有 null 可用，所以统一用 `Optional<V>`：**空 Optional 就是 Java 的 null**，与 `get` 返回 `nullptr` 是同一条规则的两个侧面（`get` 返回指针是因为值还留在 map 里，可以长期持有；`put` 返回 Optional 是因为旧值已经离开 map，必须拷贝出来）。`computeIfAbsent` / `computeIfPresent` / `merge` 的回调也返回 `Optional<V>`，这样"不要建立映射"（Java 的 `return null`）才可表达。

**`Set` 从 `AbstractCollection` 而不是 `Collection` 派生**，与 `List` 同理：单一继承链。这是承重的——`AbstractSet` 派生自 `Set`，如果 `Set` 与 `AbstractCollection` 并列，`HashSet` 就不是 `Set`，`AbstractSet::equals` 里的 `instanceof Set` 判定永远为假（实测踩过这个坑）。

**`keySet()` / `values()` / `entrySet()` / `equals()` / `hashCode()` 是非虚成员**（Java 里它们由 `AbstractMap` 实现）。若声明为纯虚，`TreeMap<不可哈希的键, V>` 会因为 `keySet()` 返回 `HashSet<K>` 而强制键可哈希。作为普通成员函数，它们只在使用时实例化，与库里其它"附加操作"的规则一致。

### 6.5 实现类与 hash 策略

接口保持"无策略"（与 Java 一致），策略参数只出现在实现类上，用默认模板参数兜底：

```cpp
template <class T, class Hash = std::hash<T>, class Equal = std::equal_to<T>>
class HashSet : public Set<T> { /* 包装 std::unordered_set<T, Hash, Equal> */ };

template <class T, class Compare = std::less<T>>
class TreeSet : public Set<T> { /* 包装 std::set<T, Compare> */ };

template <class K, class V, class Hash = std::hash<K>, class Equal = std::equal_to<K>>
class HashMap : public Map<K, V> { /* 包装 std::unordered_map<K, V, Hash, Equal> */ };
```

这让 `HashSet<MyType>` 在 `MyType` 有 `std::hash` 特化和 `operator==` 时开箱可用，也让不想特化 `std::hash` 的用户能传自定义策略——比 Java 更灵活，且不牺牲接口纯度。

需要 `std::hash` 的地方（`distinct()`、`Collectors::toSet()`、`Collectors::groupingBy()` 的键）同理：默认走 `std::hash`，并提供接受自定义 Hash/Equal 的重载作为文档化扩展。

`ArrayList<T>` / `LinkedList<T>` / `ArrayDeque<T>` 分别包装 `std::vector<T>` / `std::list<T>` / `std::deque<T>`；`HashSet` / `TreeSet` / `HashMap` / `TreeMap` 同理。不自己实现容器数据结构——那是纯风险无收益。

### 6.6 两条迭代器路径

Java 只有一个 `iterator()`，因为它没有 const。C++ 有 const 之后，一个"既能读又能删"的 `iterator()` 放在 const 成员上就必须靠 `const_cast` 才能交出 `T&`，而那是货真价实的未定义行为，不只是不好看。所以本库拆成两条：

| 路径 | 限定 | 元素 | `remove()` |
|---|---|---|---|
| `iterator()` | 非 const | `T&` | 生效 |
| `constIterator()` | const | `const T&` | 抛 `UnsupportedOperationException` |

派生物：`stream()` 走只读路径（不需要写权限，因此 const 容器也能 `stream()`）；`begin()` / `end()` 也只读，`for (const auto& x : list)` 不会意外拿到可写引用；`Iterator<const T>` 的 `remove()` 直接落到基类默认实现，零额外代码（§8 第 28 条讲的是它的哨兵为什么要单独一个类型）。

代价是"遍历并删除"必须显式走非 const 的 `iterator()`，不能在 const 上下文里做——这正是想要的结果。

### 6.7 Queue / Deque / ArrayDeque

```cpp
template <class T>
class Queue : public AbstractCollection<T> {           // Java: java.util.Queue
public:
    using AbstractCollection<T>::remove;                // 否则 remove() 会隐藏 remove(element)
    virtual bool offer(const T&) = 0;  virtual bool offer(T&&) = 0;
    virtual T remove() = 0;                             // 空则抛 NoSuchElementException
    virtual Optional<T> poll() = 0;                     // 空则 empty
    virtual const T& element() const = 0;               // 空则抛
    virtual Optional<T> peek() const = 0;
};

template <class T>
class Deque : public Queue<T> {                         // Java: java.util.Deque
public:
    // addFirst / addLast / offerFirst / offerLast / removeFirst / removeLast
    // pollFirst / pollLast / removeFirstOccurrence / removeLastOccurrence
    // getFirst / getLast / peekFirst / peekLast / push / pop / descendingIterator
};
```

Java 的"抛异常版 / 哨兵版"成对操作原样保留，但哨兵从 `null` 换成 `Optional<T>`（§8 第 4、5 条）。`ArrayDeque` 是唯一的实现，同时具备队列、栈、双端三种用法。

`Queue` 与 `List` / `Set` 一样直接继承 `AbstractCollection`，全库保持单一继承链。`LinkedList` 不继承 `Deque`，原因见 §8 第 29 条。

### 6.8 活视图

```cpp
template <class T> class ListView : public List<T>;                       // subList
template <class K, class V> class KeySetView : public AbstractSet<K>;     // keySet
template <class K, class V> class ValuesView : public AbstractCollection<V>;  // values
template <class K, class V> class EntrySetView : public AbstractSet<typename Map<K,V>::Entry>;
template <class T, class Compare> class TreeSetRangeView : public AbstractSet<T>;   // subSet / headSet / tailSet
template <class K, class V, class Compare> class TreeMapRangeView : public Map<K,V>;  // subMap / headMap / tailMap
template <class K, class V, class Compare> class TreeMapKeySetView : public NavigableSet<K>;  // navigableKeySet / descendingKeySet
```

六个视图都是"一个容器指针 + 一点簿记"，没有自己的存储：

- **写透**：视图上的 `set` / `add` / `remove` / `clear` 直接落到被看的容器上；map 视图的 `remove` 删的是**映射**，`add` 抛 `UnsupportedOperationException`（与 Java 一致，一个有 key 没 value 的条目在 Map 里不存在）。
- **fail-fast**：视图迭代器持有被看容器的 `modCount` 快照，改动就抛 `ConcurrentModificationException`——和 Java 的 `SubList` / `KeySet` 完全同一套机制（§11 第 17 条）。
- **const 只读**：从 const 容器取的视图，mutator 与 `iterator()` 都抛（§8 第 35 条）。只读视图再派生的子视图同样是只读的——`unmodifiableNavigableSet(set).subSet(a, b)` 读得通，与 Java 一致。
- **视图对象自身的 const 是编译期保护**：`const auto window = m.subMap(a, b);` 让 mutator 直接编译不过；从 const 容器取得的只读性是运行期的 `UnsupportedOperationException`。两者互补，测试两条路径都覆盖。
- **按值返回**：map 的三个视图按值返回（无状态，拷贝无成本），`subList` 返回 `unique_ptr`（视图没有可拷贝的状态——两个副本指向同一个窗口，那是迭代器级别的东西）。两个范围视图与 `TreeMapKeySetView` 同样按值返回：它们的"状态"就是根指针加一对栅栏加方向，拷贝下来仍然指向同一棵树，不会悬垂。

`Map::Entry` 仍是值类型：entry 视图的迭代器每次物化一个 `Entry`，`setValue` 写副本而不写回 map。这是活视图里唯一残留的差异，已记在 §8 第 12 条。

**范围视图**（M9）比上面四个多一层状态：一对**栅栏**（`std::optional<T> from_ / to_` + 各自的 `fromInclusive_` / `toInclusive_`）与一个可选的只写指针。它们的语义完全按 `java.util.TreeMap.NavigableSubMap` 复刻，并且逐条对着 JDK 实测校准（§9）：

- **窗口归属**：`key ∈ 窗口` ⟺ 在两个栅栏之间，且按各自的开闭区间判定。`contains` / `get` / `remove` / `keySize` 全按它过滤。
- **写操作**：`add` / `put` / `putIfAbsent` / `computeIfAbsent` / `merge` 在 key 落在窗口外时抛 `IllegalArgumentException`（消息与 Java 逐字一致：`key out of range` / `fromKey out of range` / `toKey out of range` / `fromKey > toKey`）；`remove` 落在窗口外只是返回 `false` / `empty`，不抛。
- **新栅栏不得放宽窗口**：`subSet` / `headMap` 之类在视图上再开窗口时，新栅栏必须落在当前窗口内；"等于**开区间**栅栏却声称包含它"会被拒绝。这是 `NavigableSubMap.inRange(key, inclusive)` 的规则，实测确认（例如 `[20,40)` 上 `headSet(40,true)` 抛、`headSet(40,false)` 可以）。
- **查找会被夹到窗口内**：`floor(100)` 在 `[20,40)` 上返回窗口最后一个元素而不是 `empty`；`ceiling(10)` 返回窗口第一个元素。这一条与直觉相反，是照着 JDK 输出抄的。
- **`computeIfPresent` 必须自己判窗口**：窗口外的 key 返回 `empty` 且**不**改动底层映射。最初直接委托给底层 map，把窗口外的映射改了——JDK 实测抓出来的真 bug。
- **迭代路径**：`TreeSetRangeView` 自己有 `iterator()`（游标从树的 `lower_bound` 走到 `upper_bound`）；`TreeMapRangeView` 没有，因为 `Map` 本来就不交迭代器——它的 `keySet()` / `values()` / `entrySet()` 靠覆写 `visitEntries` 过滤，所以那三个视图看到的就是窗口。
- **读底层存储**：两个视图都被 `TreeSet` / `TreeMap` 声明为 `friend`，直接对树做 `lower_bound` / `upper_bound`。写**不**走这份友谊：全部经由底层的公开 mutator，`modCount` 只在拥有它的那一处递增。

**方向只是视图上的一个 bool**（M10）。`descendingSet` / `descendingMap` 与 `subSet` 接受的是同一对**升序**栅栏，唯一的区别是 `TreeSetRangeView` / `TreeMapRangeView` 多一个 `descending_` 标志：

- **`descendingSet()` / `descendingMap()` 是视图，不是副本**。同一个窗口换个方向走，第二次调用换回来（`descendingSet().descendingSet()` 与原文等价），写透 / fail-fast / 只读传递全部照旧。这正是 §8 第 30 条的结论被推翻的地方：Java 的 `descendingSet` 就是同一棵树的镜像，本库现在也是。
- **方向决定"哪个成员换哪个意义"**：`first` / `last`、`pollFirst` / `pollLast`、`floor` / `ceiling`、`lower` / `higher`、`headSet` / `tailSet`（递减视图的 `headSet(k)` 留下的是升序意义上 **大于** k 的部分）在 `descending_` 下两两互换。`descendingIterator()` 是**本视图顺序的反面**，所以在递减视图上它向前走——与 `TreeSet::descendingIterator()` 对升序集合的行为一致。实现上不是逐个重写：四个查找先按升序问底层，再按方向挑一对；`subSet` 的 `requireAcceptableWindow` 检查的仍是升序那一对，因为 Java 的 `DescendingSubMap.subMap` 就是把两个参数**颠倒**着送进基类构造器的。
- **`comparator()` 返回 `Comparator<T>`，递减视图返回它的 `reversed()`**。视图的 `Compare` 是 std 风格的 bool 比较器，无法"取反"后仍然当 `Compare` 用，所以返回值改用 Java 风格的 `Comparator<T>`，靠 `comparatorFrom` 桥接（§11 第 23 条）。
- **`navigableKeySet()` / `descendingKeySet()`**：`TreeMapKeySetView<K, V, Compare>` 是一个 `NavigableSet<K>`，内部**按值**持有一个 `TreeMapRangeView`（视图本身就是"根指针 + 一对栅栏 + 方向"的把手，拷贝无成本、也不会悬垂），另存一个可写指针把只读性传下去。键集上的 `floor` / `ceiling` / `subSet` / `headSet` / `tailSet` / `descendingIterator` 全部转发给窗口，`remove` 删的是映射（§11 第 24 条）。
- **被"接受"的空窗口**：`headSet(40, false)` 加在 `[20, 40)` 的递减视图上时，Java 只要求开区间栅栏落在**闭**区间内，于是构造出升序的 `[40, 40)`——一个合法但为空的窗口。这种窗口的原始边界是反的（`upper_bound(40)` 越过 `lower_bound(40)`），所以 `firstInRange()` / `pastLastInRange()` 必须先把空判定做完再返回，否则 `std::distance` 和递减迭代器的 `--cursor_` 会直接走出树尾。判定用的就是 `NavigableSubMap.inRange` 对候选首元素的那一问：**候选首元素越过上栅栏 ⇒ 窗口为空 ⇒ 两端都收敛到 `end()`**。这个 bug 是 M10 里被 JDK 对拍抓出来的，见 §9、§12。

---

## 7. Stream 层

### 7.1 中间操作（全部惰性，`&&` 限定）

| 方法 | 签名要点 | 实现备注 |
|---|---|---|
| `filter` | `Stream<T> filter(P) &&` | 包一层 `while` 循环 |
| `map` | `Stream<R> map(F) &&`（`R = invoke_result_t<F&, T>`） | 逐元素变换 |
| `flatMap` | `Stream<R> flatMap(F) &&`，`F: T → Stream<R>` | 闭包持有内层流，排空后拉下一个 |
| `peek` | `Stream<T> peek(Consumer) &&` | 旁路副作用，调试用 |
| `distinct` | `Stream<T> distinct() &&` | 状态：`std::unordered_set<T>` |
| `sorted` | `Stream<T> sorted() &&` / `sorted(Comparator<T>) &&` | 首次拉取时全量缓冲 + 排序 |
| `limit` | `Stream<T> limit(int64) &&` | 负值抛 `IllegalArgumentException` |
| `skip` | `Stream<T> skip(int64) &&` | 非负短路优化（`skip(n)` 后 `limit(m)` 可在阶段层合并） |
| `takeWhile` / `dropWhile` | `Stream<T> takeWhile(P) &&` | 短路友好 |
| `gather` | `Stream<R> gather(Gatherer<T,A,R>) &&` | 推送式 gatherer 桥接进拉取管道：见 §7.7 |
| `reversed` | `Stream<T> reversed() &&` | 需要缓冲；Java 靠 `List.reversed()`，本库额外提供 |
| `unordered` | `Stream<T> unordered() &&` | 恒等透传；Java 只把它当"可打乱遭遇顺序"的提示，顺序流的保持顺序是合法实现 |
| `mapMulti` | `Stream<R> mapMulti<R>(F) &&`，`F: (const T&, Downstream<R>&) -> void` | Java 16 的 `Stream.mapMulti`。就是 `gather()` 的一个特例，所以实现只有一行转发（§7.7） |

`gather` 已在 M8 落地、`mapMulti` 在 M10 落地（§7.7）；两者共用同一条推送式桥接，也就是说 `mapMulti` 不需要自己的机制。

### 7.7 gather：把推送式 gatherer 接进拉取式管道

Java 22 的 `Gatherer<T,A,R>` 是"每个元素该做什么"的命令式版本：一个 gatherer 可以对一个输入元素发出零个、一个或多个结果，因此窗口、扫描这类操作不需要把流整体物化。

| 组件 | 本库形态 |
|---|---|
| `Gatherer<T,A,R>` | 三个 `std::function`：`initializer` / `integrator` / `finisher`（`initializer` 与 `finisher` 可空） |
| `Downstream<R>` | 持有 `std::function<bool(R)>`；`push` 恒真，`isRejecting` 恒假（§8 第 33 条） |

`Stream::mapMulti<R>(mapper)` 直接建在 `gather()` 上：mapper 被包成一个 `Gatherer<T, Void, R>`，其 integrator 把 `Void&` 状态忽略掉、把元素与 `Downstream<R>` 交给调用者的 lambda。"一个 gatherer 对每个输入发出零个或多个结果"就是 `mapMulti` 的定义，所以推送/拉取的桥接不必写第二遍，`sizeHint` 也按 `gather` 的规矩丢掉。`R` 必须在调用点显式给出（`mapMulti<int>(...)`），理由与 `Gatherers::of<T, A, R>` 相同：C++ 没有目标类型推断（§8 第 34 条）。
| `Gatherers` | `windowFixed` / `windowSliding` / `fold` / `scan` / `of` / `ofSequential` |
| `Void` | `using Void = std::monostate`，等价于 Java 的 `Void` 单值类型 |

**拉取/推送的接缝**：管道是拉取的，gatherer 是推送的，两者用一个缓冲区对齐——`gather` 阶段在每次被拉取时反复运行 integrator，直到它产生了输出或上游枯竭，产出的元素进入队列逐个交出。上游枯竭或 integrator 返回 `false` 时调用一次 finisher，finisher 推入的元素照样从队列里交出。`limit` 停止拉取即可，队列剩下的元素不会被生产，这正是 `windowFixed(3)` 配 `limit(2)` 能在无限流上终止的原因。

### 7.8 sizeHint：Java 的 `Spliterator.estimateSize()` 对应物

Java 的管道之所以不用把元素逐个塞进"会反复扩容的 vector"，靠的是 spliterator 的 size 估计。本库用 `Stream<T>::SizeHint = std::optional<std::size_t>` 复刻：

- **源**给出精确值：`of(vector)` / `of(a,b,c)` / `range` / `rangeClosed` / `concat`（两侧都已知时）/ `ofRange`（`sized_range` 时）/ `Collection::stream()`。
- **保数阶段**原样传递：`map` / `peek` / `sorted` / `reversed` / `unordered`；`limit(n)` 取 `min`，`skip(n)` 取差。
- **变数阶段**丢弃：`filter` / `flatMap` / `distinct` / `takeWhile` / `dropWhile` / `gather`。
- **消费方**只用它来预留：`sorted` / `reversed` 的缓冲、`toArray`（`toList` 走 `toArray`）。

它是**估计值而非契约**：任何消费方都必须接受 `nullopt`，任何操作都不允许因为提示错误而算错。因此没有用它实现 `count()` 的 O(1) 快路径——那会把估计值变成正确性依赖。

### 7.9 std::ranges 互操作

- `Stream<T>::ofRange(range)`：直接吃任何 `std::ranges::input_range`，左值借用、右值搬进阶段并持有，尺寸来自 `sized_range`。省掉"先包一层 `Iterable`"这一步。
- 容器本身就是 range（§8 第 18 条）：`std::ranges::distance` / `find` / `copy` / `count_if` 直接可用；`ArrayList` 等还满足 `sized_range`。
- 只读单趟是刻意保留的边界：`Iterable::begin()` 交出 `ReadOnlyIterator`，所以 `std::ranges::sort(list)` 这类需要可写随机访问的算法不成立——排序要走 `list.sort(...)` 或管道里的 `sorted()`。

**副作用回调收到可写引用**：`peek` / `forEach` 把元素以 `T&` 交给回调，`filter` / 匹配类谓词同样传 `T&`（建议写成 `const T&`）。这是刻意的：Java 的元素是引用，回调里改字段对后续阶段可见，C++ 里对应的就是传引用而不是传值。实测用例 `peek` 里把元素乘 2 后 `toArray()` 得到 `{2, 4, 6}`。

**状态存在闭包里，不需要堆上状态对象**：Java 每个阶段分配一个 `ReferencePipeline` 节点 + 状态对象；本库用 move-only lambda 按值捕获状态（如 `distinct` 的 `std::unordered_set`、`sorted` 的缓冲 vector），随阶段一起被 move 进下一层。少一次间接寻址，也不需要 `shared_ptr`。

### 7.10 并行执行模型（`parallel()` / `parallelStream()`）

Java 的并行流是"把源切开 + ForkJoinPool 上分治"：`Spliterator.trySplit()` 递归对半分，任务提交给 commonPool，终结操作用 `Collector::combiner` 把各分片的累加器合并起来。

**本库的源不可切分**，所以照抄这条路等于重写整条管道。这里的阶段是 `std::move_only_function` 拉取闭包（§5.3），一个 `NextFn` 就是"给我下一个元素"，没有任何"能不能对半劈开"的信息，也无法凭空发明：`filter` 之后的元素个数要真跑一遍才知道，`iterate` 根本没有第二个副本可切。唯一能在不改架构的前提下真正并行化用户 lambda 的做法是：

**`parallel()` 之后的阶段按批处理，每批丢到共享线程池上并行求值，再按原顺序吐回。**

```cpp
// 概念示意：map 的并行分支
pull up to `chunk` elements from the upstream;      // 顺序的，但每批只有一次
parallelPool().forEach(batch.size(), [&](i) { out[i] = mapper(in[i]); });
// out[i] 永远对应 in[i]，所以遭遇顺序不变
```

四个要件：

- **`Budget`（拉取预算）**：`sizeHint` 回答"这个阶段**会**产出多少"，`Budget` 回答"这个阶段**可以**往前读多少"。两者不同：`filter` 丢掉 `sizeHint`（产出个数未知）但保留 `Budget`（输入个数已知）。批次大小只会取 `min(chunk, Budget 剩余)`，且 `Budget` 只会被下界收紧、永不被放宽，所以"读到预算耗尽"就等于"上游真的空了"。源没有任何界（`iterate` / `generate`、以及 `flatMap` 之后）时 `Budget` 是 `nullopt`，阶段退化成一次一个元素——**并行管道永远不会预读到它界不住的地方**，`iterate(...).parallel().findFirst()` 因此不会挂死。代价是：**没有已知规模的源，阶段就是顺序执行的**（Java 对 `generate()` 也无能为力，它同样不可切分）。

- **`ThreadPool`（`Parallel.h`）**：进程级单例、线程数在首次使用时定为 `hardware_concurrency()`，之后只能通过 `cppstream::setParallelism(n)` 调整**可用**的部分（不动线程，改一个 atomic）。`forEach(count, fn)` 用共享游标动态派活而不是静态均分：元素的开销很少均匀，一个慢元素不该拖住整块。任一线程抛出的第一个异常在全部任务收拢后重抛给调用者。

- **嵌套保护**：从池内工作线程发起的并行阶段**内联执行**而不是再排一个任务然后等线程。这不是优化，是防死锁：外层批次占着 N 个 worker 且都在等内层结果时，池里没有空线程可派，ForkJoinPool 靠 work-stealing 解决这个问题，这里用一个 `thread_local` 标志解决——代价是嵌套的并行降级为顺序，与 ForkJoin 无法切分时的取舍相同。测试 `a parallel stage pulled from inside a worker degrades instead of deadlocking` 盯着它。

- **终结操作的分片合并**：`collect` / `reduce`（两个重载）/ `min` / `max` / `sum` 把一批切成 `parallelism()` 片，每片在自己的累加器上累加，再**按遭遇顺序**逐个 `combiner` 合并进一个总累加器。这正是 §8 第 8 条里被点名的"`Collector::combiner` 变成硬需求"——它是构造参数，一直都在，这次终于被调用（`Gatherer::combiner` 仍然缺席，理由见下）。合并顺序保证了 `toList` / `joining` / `groupingBy` 这些对顺序敏感的收集器在并行下也给出与顺序版逐字节相同的结果。内存上每次只保留 `parallelism()` 个分片，不会随元素总数增长。

**哪些真的并行、哪些是屏障**（`parallel()` 之后）：

| 阶段 / 终结操作 | 并行？ | 说明 |
|---|---|---|
| `map` / `filter` / `peek` | ✅ 批处理 | 一元、无状态，批内顺序无关，结果按序写回 |
| `forEach` | ✅ 批处理 | 动作重叠执行，元素仍按遭遇顺序取出 |
| `collect` / `reduce` / `min` / `max` / `sum` | ✅ 分片 + 有序合并 | 累加器须可结合，与 Java 对并行归约的要求一致 |
| `anyMatch` / `allMatch` / `noneMatch` | ✅ 批处理 + 短路 | 一批命中就停止拉取，靠一个 relaxed atomic 做见证 |
| `flatMap` | ⚠️ 部分 | mapper 并行求值；内层流仍**惰性且按序**排空（只有外层批次是急切的） |
| `sorted` / `reversed` / `distinct` | ❌ 屏障 | 有状态 / 需要全量，一次一个元素；并行标志继续往下传 |
| `mapMulti` / `gather` | ❌ 屏障 | 每个输入可产出任意多个元素（§7.7），批处理必须先物化 |
| `count` / `toArray` / `toList` / `findFirst` / `findAny` / `average` / `forEachOrdered` | ❌ 顺序 | 自身无可并行的算力；上游阶段照常批处理。`findFirst` 更短：只拉一个元素，所以有界并行流不会被它抽干 |

三个必须记住的语义后果（都写进了 §8 第 8 条）：

1. **标志只作用于调用点之后**。阶段在 `parallel()` 之前就已经构造完成，拉取阶段的并行能力只能作用于自己执行的代码。惯用写法（`collection.parallelStream()`、`Stream.of(...).parallel().map(...)`）本来就把标志放在重活之前，所以移植过来的 Java 代码落在并行路径上；`stream.map(f).parallel()` 里的 `f` 则不会并行。
2. **批处理有预读**。`findFirst()` 会读过一批，`peek` / `forEach` 的动作不再按顺序完成。`chunk` 取 1024，终结操作再乘 worker 数，预读量有界但非零。
3. **用户 lambda 必须是线程安全的**。顺序管道里带可变状态的 `mutable` lambda 能跑，并行管道里就是数据竞争——Java 的"非干扰 / 无状态"要求在这里同样成立。

### 7.2 终结操作（`&&` 限定）

`forEach` / `forEachOrdered`、`count`（返回 `int64`）、`toArray`（`std::vector<T>`）、`toList`（返回冻结的 `ArrayList<T>`）、`collect`、`reduce`（两种重载）、`min` / `max`、`findFirst` / `findAny`、`anyMatch` / `allMatch` / `noneMatch`、`sum` / `average`（用 concept 约束到算术类型）。

**数值操作的处理**：Java 需要 `IntStream` / `LongStream` / `DoubleStream` 纯粹是为了避免装箱。C++ 模板没有装箱成本，所以**整类砍掉**，把数值操作作为 `Stream<T>` 上受 concept 约束的成员：

```cpp
T sum() && requires std::integral<T> || std::floating_point<T>;
Optional<double> average() && requires std::integral<T> || std::floating_point<T>;
```

这是全库唯一一处"因为 C++ 更好所以不需要复刻"的地方。缺失的 `IntStream.range(a, b)` 由 `Stream<T>::range` / `rangeClosed` 补上。

**并行流下的终结操作**：`forEach` / `collect` / `reduce`（两种重载）/ `min` / `max` / `sum` / 三种 `match` 在 `parallel()` 之后按批并行——`collect` 一系走"每片一个累加器 + 按遭遇顺序 `combiner` 合并"，`match` 一系靠一批命中就停止拉取。`forEachOrdered` / `count` / `toArray` / `toList` / `findFirst` / `findAny` / `average` 保持顺序执行，只享受上游阶段的并行：它们自身没有可并行的算力，而 `findFirst` 的"只拉一个元素"恰恰是它该有的短路。分片合并要求累加器可结合，与 Java 对并行归约的要求一致。详见 §7.10。

### 7.3 Collector 模型

```cpp
enum class Characteristics { concurrent, unordered, identityFinish };

template <class T, class A, class R>
class Collector {
public:
    using elementType     = T;
    using accumulatorType = A;
    using resultType      = R;
    using Supplier    = std::function<A()>;
    using Accumulator = std::function<void(A&, T&)>;
    using Combiner    = std::function<A(A&&, A&&)>;
    using Finisher    = std::function<R(A&&)>;

    Collector(Supplier, Accumulator, Combiner, Finisher,
              std::vector<Characteristics> = {});

    static Collector of(Supplier, Accumulator, Combiner, Finisher,
                        std::initializer_list<Characteristics> = {});
    const Supplier& supplier() const;      const Accumulator& accumulator() const;
    const Combiner& combiner() const;      const Finisher& finisher() const;
    const std::vector<Characteristics>& characteristics() const;
    bool has(Characteristics) const;
};

template <class C>
concept CollectorType = requires { typename C::elementType;
    typename C::accumulatorType; typename C::resultType; };

template <class C, class T>
concept CollectorLike = CollectorType<C> && requires(const C& c) { /* 四个函数都可调用 */ }
                        && std::same_as<typename C::elementType, T>;
```

`Characteristics` 存 `std::vector` 而不是 `initializer_list`：`mapping` / `filtering` / `flatMapping` 要把下游收集器的 characteristics 原样转发，concept 也需要能读取它。

`concurrent` 仍然只为保真而存在：本库的并行 `collect` 不共享累加器，而是每片一个、按遭遇顺序合并，所以不需要"并发安全的累加器"这一承诺。真正被 `parallel()` 用起来的是 `combiner`（§7.10）。

累加器签名是 `void(A&, T&)` 而非 `void(A&, const T&)`：Java 的 `BiConsumer<A, super T>` 拿到的就是元素对象本身，而管道交出的元素是刚从源里取出的、无人别名的值，所以累加器可以自由 move（`toList` 就是 `push_back(std::move(element))`）。写成 `const T&` 的 lambda 依然能装进 `std::function<void(A&, T&)>`，两种写法都可用。

`Collector` 内部用 `std::function` 而非 `move_only_function`：收集器是**可拷贝的规格对象**（Java 里 `Collectors.toList()` 返回的是无状态单例），可拷贝性让 `groupingBy` 能按值持有下游收集器，写起来干净得多。管道用 move-only、收集器用 copyable，这个不对称是有意的。

`collect` 用 concept 约束，把错误挡在函数边界：

```cpp
template <class C>
    requires CollectorLike<C, T>
auto collect(C collector) && -> typename C::result_type;
```

这是控制 `groupingBy` 模板报错爆炸的关键手段——没有 concept，用户写错一个 lambda 会得到几百行错误。

### 7.4 循环包含的处理

`Collection::stream()` 返回 `Stream<T>`，而 `Stream::toList()` 返回 `ArrayList<T>`，直接 include 会成环。处理办法：

1. `cppstream_fwd.h` 前向声明所有模板类。
2. `Stream.h` 只**声明**返回容器的方法（返回类型可以是 incomplete type），定义放到 `Collectors.h` / `ArrayList.h` 之后（out-of-line definition of a member of a class template 在头文件里是合法的）。
3. 因此 `Stream::toList()` 在包含 `<cppstream/cppstream.h>` 或 `<cppstream/Collectors.h>` 后可用。**单头文件不保证自包含**，这是为什么 `cppstream.h` 是推荐入口。

备选方案是给每个头加自包含保证，代价是把 `Stream` 拆成两三段并引入更细的头文件，收益不抵复杂度。

### 7.5 Collectors 清单

`toList` / `toUnmodifiableList` / `toSet` / `toUnmodifiableSet` / `toMap(k, v)` / `toMap(k, v, merge)` / `toUnmodifiableMap` / `joining()` / `joining(delim)` / `joining(delim, prefix, suffix)` / `counting()` / `summingInt` / `summingLong` / `summingDouble` / `averagingInt` / `averagingLong` / `averagingDouble` / `minBy` / `maxBy` / `reducing(...)`（三重重载）/ `mapping(fn, downstream)` / `filtering(pred, downstream)` / `flatMapping` / `groupingBy(classifier)` / `groupingBy(classifier, downstream)` / `groupingBy(classifier, mapFactory, downstream)` / `partitioningBy(pred)` / `partitioningBy(pred, downstream)` / `teeing(d1, d2, merger)` / `summarizingInt` 等（含 `IntSummaryStatistics`）。

`groupingBy` 是整块最硬的部分：`R = HashMap<K, Downstream::R>`，需要按值持有下游收集器、在一次遍历里同时驱动多组累加器。用 concept 约束下游，`static_assert` 给出可读错误。

### 7.6 静态工厂

全部是 `Stream<T>` 的静态成员：`empty()` / `of(values...)` / `of(std::vector<T>)` / `of(std::initializer_list<T>)` / `ofNullable(T)` / `iterate(seed, next)` / `iterate(seed, hasNext, next)` / `generate(supplier)` / `concat(a, b)` / `range(a, b)` / `rangeClosed(a, b)`。

Java 把它们挂在 `Stream` / `IntStream` 接口的静态方法上；C++ 的元素类型由类模板参数给出（`Stream<int>::range(0, 10)`），所以形式几乎一致，不需要 `Streams` 这样的辅助类。原设计中的 `Streams.h` 因此取消。

`rangeClosed` 用"耗尽哨兵"而不是自增后再比较来实现，因此当上界是 `T` 的最大值时也能正常终止（`rangeClosed(INT_MAX, INT_MAX)` 产出恰好一个元素而不是溢出）——Java 内部用同样的策略。`range` 的下界大于等于上界时产出空流。

`of(values...)` 要求**每个实参的退化类型恰好是 `T`**。C++ 不会像 Java 的推断那样把 `const char*` 提升成 `std::string`，所以字符串要显式写 `Stream<std::string>::of(std::string("a"))`，或改用 `of({"a", "b"})`（`initializer_list` 重载会做转换）。

---

## 8. 偏离清单（逐条记录原因）

37 条里 31 条是被语言特性逼出来的，5 条（第 18、20、23、26、36）是主动增益，第 8 条是执行模型上的让步（见 §7.10）；每条都注明了属于哪一类。

| # | Java | 本库 | 原因 |
|---|---|---|---|
| 1 | `List<T> toList()` | `ArrayList<T> toList()` | 值语义下抽象类不能按值返回（§5.2） |
| 2 | `Map<K,V> groupingBy(...)` | `HashMap<K, ArrayList<T>>` | 同上 |
| 3 | `list.remove(int)` 与 `remove(Object)` 重载 | `removeAt(int)` + `remove(const T&)` | `T = int` 时 C++ 重载无法区分，会二义 |
| 4 | `map.get(k)` 返回 null 表示不存在 | `get(k)` 返回 `const V*`，`nullptr` 表示不存在 | C++ 无通用空值 |
| 5 | `map.put(k,v)` 返回旧值或 null | 返回 `Optional<V>`，空表示原本没有映射 | C++ 无通用空值；与第 4 条同一条规则（null ↔ `Optional::empty`）。`computeIfAbsent` / `computeIfPresent` / `merge` 的回调同样返回 `Optional<V>`，以表达 Java 的"返回 null 表示不建立/删除映射" |
| 6 | `List.of(a,b,c)` | `Lists::of(a,b,c)` | `List<T>::of` 定义需要 `ArrayList<T>` 完整类型，会造成 `List.h → ArrayList.h → List.h` 环 |
| 7 | `IntStream` / `LongStream` / `DoubleStream` | 不存在；`Stream<T>` + concept 约束的 `sum` / `average`；`Streams::range` | C++ 无装箱成本，原始类型流的目的消失 |
| 8 | 并行流 = `Spliterator.trySplit()` 切分源 + ForkJoinPool + `combiner` 归并 | `parallel()` / `parallelStream()` 已实现，标志与顺序语义保真；**执行模型换成"`parallel()` 之后的阶段按批上共享线程池"**（§7.10） | 本库的源是拉取闭包，没有"对半劈开"的信息，复刻 `trySplit()` 等于重写全部中间操作。换来三条必须记住的语义差别：标志只作用于调用点之后、批处理有预读、无界源退化为顺序。副产品是 `Collector::combiner` 成了并行终结操作的硬需求——它一直在构造参数里，这次终于被调用；`Gatherer::combiner` 仍然缺席，因为 `gather` 是可产出任意多元素的屏障 |
| 9 | `Optional<T>` | 自建 `Optional<T>`（内部 `std::optional`），隐式双向转换 | 方法名差异过大，别名保真度不足（§5.5） |
| 10 | `Collection` 允许元素为 null | 容器拒绝 null，抛 `NullPointerException` | 用户决策；且值类型本就没有 null 概念 |
| 11 | `list.subList(a,b)` 返回视图 | 返回 `std::unique_ptr<ListView<T>>`，**活视图** | M8 补齐。视图持 `const List<T>*` + 可选的可写指针 + 偏移；生命周期按第 17 条同款"文档而非机制"约束。从 const list 取的视图是只读的，见第 35 条 |
| 12 | `map.keySet()` / `values()` / `entrySet()` 返回活视图 | 返回活视图（`KeySetView` / `ValuesView` / `EntrySetView`，按值） | M8 补齐。三者都持一个 map 指针，`remove` / `clear` 直接作用于 map。唯一残留差异是 `Entry` 仍是值类型，`setValue` 写副本而不写回 map（Java 的 entry 迭代器给的是活节点） |
| 13 | 每个对象都有 `Object.equals` / `Object.hashCode` | 有 `operator==` / `std::hash<T>` 就用，没有就退化为**同一性**（`Elements.h` 的 `elementEquals` / `elementHash`） | Java 的默认实现本来就是引用同一性，所以这是保真而非妥协；代价是容器不复刻 Java 的 null 容错（容器本来就拒绝 null，见第 10 条） |
| 14 | checked exception / `throws` 子句 | 全部 `RuntimeException` 派生，无 `throws` | C++ 无此机制，模拟只会变噪音 |
| 15 | `Collection.size()` 返回 `int` | 保持 `int`（内部 `static_cast`） | 保真；Java 同样有 2^31 上限，行为一致 |
| 16 | `List.get` 越界抛 `IndexOutOfBoundsException` | 抛 `IndexOutOfBoundsException`（派生自 `IllegalArgumentException`） | 兼顾保真与 C++ 常识：`out_of_range` 风格 |
| 17 | 迭代器在容器析构后使用 = 不确定行为 | 同样是不确定行为，文档明确标注 | 与 Java 一致，不做额外防护 |
| 18 | `Iterable` 不能用于 C++ range-for | 额外提供 `begin()` / `end()`，返回单趟 `input_iterator` 包装 | 纯增益：让 `std::ranges` 算法和 range-for 能直接用。注意是**单趟**，不要拿去做多趟算法 |
| 19 | `Comparator.equals` | 不提供 | Java 默认实现是引用同一性，`std::function` 无法有意义地复刻 |
| 20 | `Comparator` 只能 `compare` | 额外提供 `operator()`（`compare < 0`） | 不能交给 `std::sort` / `std::ranges` / `std::map` 的比较器没有使用价值（§5.7） |
| 21 | `comparingInt` / `comparingLong` / `comparingDouble` | 不存在，用 `comparing` + 自然序 | 与第 7 条同因：避免装箱的理由在 C++ 不成立 |
| 22 | `Optional.get()` 返回 T | 返回 `const T&`（另有 `&` / `&&` 重载） | Java 返回的也是被引用的对象而非副本，返回引用更贴近引用语义，且免去一次拷贝 |
| 23 | `Stream` 无 `reversed()` | `Stream<T>::reversed()` | Java 把这个能力放在 `SequencedCollection`（Java 21）上；拉取式管道里加它只需一次缓冲 |
| 24 | `unordered()` 会打乱遭遇顺序 | 恒等透传 | Java 自己就把它定义为提示而非保证；顺序流保持顺序是合法实现 |
| 25 | `Stream.of("a")` 推断出 `Stream<String>` | `of(values...)` 要求实参退化类型恰为 `T`；`Lists::of` / `Sets::of` 同理 | C++ 不做 Java 那种隐式提升；用 `of({"a","b"})` 或显式 `std::string` |
| 26 | `Collections.unmodifiableList/Set/Map` 返回包装视图 | 容器自带 `freeze()` / `isFrozen()`，就地冻结**具体类型** | 值语义下不能按值返回抽象类（§5.2），所以 `Stream::toList()`、`Collectors::toUnmodifiable*` 必须把冻结做在 `ArrayList<T>` / `HashSet<T>` / `HashMap<T>` 自己身上。副产物：**值拷贝保留冻结状态**（拷贝构造函数优先于 `Collection` 构造函数），要可变副本就用 `addAll` 装进新容器——这与 Java 的 `new ArrayList<>(); addAll(x)` 是同一个写法 |
| 27 | `Collectors.toList()` 由目标类型推断 `T` | `Collectors::toList<T>()`，元素类型显式给出 | C++ 没有目标类型推断。需要 mapper 结果类型的工厂（`toMap` / `groupingBy`）仍从 lambda 推导，所以那部分读起来与 Java 一致 |
| 28 | `Iterator` 自己比较自己判断是否耗尽 | `ReadOnlyIterator<T>` + `ReadOnlySentinel` 两个类型 | `std::sentinel_for` 要求哨兵是 `semiregular`（可拷贝），而 `ReadOnlyIterator` 持有单趟游标、故意 move-only。拆出空哨兵后 `std::ranges` 与 range-for 都能用，且 `end()` 零分配 |
| 29 | `LinkedList implements List, Deque` | `Queue` / `Deque` 接口存在（M8），由 `ArrayDeque` 实现；`LinkedList` 保留同名同义的 `addFirst` / `pollLast` … 但不继承它们 | Java 的两个接口无状态，C++ 里 `List` 与 `Deque` 都是抽象类，同时继承会让 `Collection` 出现两份子对象，需要贯穿整条继承链的虚继承。命名与语义对齐 Java，所以将来若真要做虚继承也不用改签名 |
| 30 | `TreeSet.subSet` / `TreeMap.subMap` 等返回活视图，另有 `descendingSet` / `descendingMap` | `subSet` / `headSet` / `tailSet` / `subMap` / `headMap` / `tailMap`（含 `fromInclusive` / `toInclusive` 重载）与 `descendingSet` / `descendingMap` / `descendingIterator` / `navigableKeySet` / `descendingKeySet` **全部**是活视图（`TreeSetRangeView` / `TreeMapRangeView` / `TreeMapKeySetView`），栅栏规则与方向语义照 JDK 实测复刻 | 范围视图在值语义里表达"树 + 一对升序栅栏 + 一个方向标志"，代价是每个视图多两个 `std::optional` 与一套栅栏判定，换回来的是写透 / fail-fast / 越界检查 / 双向遍历全部与 Java 一致。M9 一度把 `descending*` 留作"换反向比较器的副本"，M10 推翻了那个取舍：Java 的 `descendingSet` 本就是同一棵树的镜像，做成视图后 `descendingSet().descendingSet()` 能回到原窗口，而副本做不到（§6.8） |
| 31 | `Collector<T, A, R>` 的 `A` 在 Java 里是 `?` | `A` 必须可命名（`AveragingState`、`std::pair<A1, A2>` 等） | C++ 返回类型不能含未知量；`Collectors::AveragingState` 因此是公开但标注为实现细节的嵌套类型 |
| 32 | `Map` 的 `keySet` / `values` / `entrySet` / `equals` / `hashCode` 由 `AbstractMap` 提供 | 非虚成员，直接实现在 `Map` 上，由 `visitEntries` 原语驱动 | 若声明为纯虚，`keySet()` 返回 `HashSet<K>` 会强制每个 `TreeMap` 的键可哈希，即使调用者从不需要键集 |
| 33 | `Downstream.push` 可返回 `false`，`isRejecting()` 可返回 `true` | `push` 恒返回 `true`，`isRejecting()` 恒返回 `false` | 推送的拒绝只发生在短路的下游（如 `limit`）上，而本库的管道是拉取的：`limit` 停止拉取即可，不需要反向通知。gatherer 想提前收工，就从 integrator 返回 `false`——同一个信号，且立刻生效 |
| 34 | `Gatherer.of(...)` 由目标类型推断 `T` / `R`，且有 `combiner` 与 `andThen` | 类型实参在调用点显式给出（`Gatherers::of<T,R>`）；无 `combiner`；无 `andThen` | C++ 没有目标类型推断，返回类型无法告诉编译器 `R` 是什么。`combiner` 只在并行流里合并部分状态，而 `gather` 即使加了 `parallel()` 也是屏障（它每个输入可产出任意多个元素，§7.10），所以它仍是一个从不被调用的必需参数。`andThen` 在顺序管道里等价于两次 `gather()`（第二个 gatherer 的输入就是第一个的输出，finisher 顺序也一致），因此组合能力本来就在管道层 |
| 35 | 视图总是可写的（`Collections.unmodifiableList` 除外） | 从 const 容器取到的视图是只读的：所有 mutator、`iterator()` 与交出可写引用的 `forEach` 都抛 `UnsupportedOperationException`，`constIterator()` / range-for 正常；map 范围视图的**可写** `V* get(key)` 重载同样抛，只读读取要走 const 重载（`const V* get(key) const`）。只读视图派生出的子视图也是只读的（**不抛**，与 Java 的 `unmodifiable*` 包装一致）。视图对象自身声明为 `const` 时，mutator 是编译错误而不是运行期异常 | C++ 的 const 是类型系统的一部分。`iterator()` 交出可写引用，而 const 容器没有可写引用可交，所以它必须抛。视图对象自身若不是 const，还多一层编译期保护 |
| 36 | 无 `sizeHint` / `ofRange` | `Stream<T>::sizeHint()` 与 `Stream<T>::ofRange(range)` | 主动增益，对齐 Java 的 `Spliterator.estimateSize()` 与"任何集合都能成为流源"两件事，见 §7.8 / §7.9 |
| 37 | 有序容器的比较器是**构造期**的 `Comparator`，`comparator()` 返回它 | 有序容器是 `TreeSet<T, Compare>` / `TreeMap<K, V, Compare>`，`Compare` 是**编译期**的 std 风格 bool 比较器；`comparator()` 返回 Java 风格的 `Comparator<T>`，另有自由函数 `comparatorFrom<T>(Compare)` 做桥接 | C++ 不能在运行期给 `std::set` 换比较器——那是数据结构的一部分，不是参数。所以 `Compare` 只能进模板参数（§6.5），这是"包装 std 容器"的必然结果。但 Java 的 `comparator()` 返回类型是 `Comparator`，且递减视图必须返回真正反向的那一个；std 的 bool 比较器无法取反后仍当 `Compare` 用，因此返回值统一为 `Comparator<T>`，由 `comparatorFrom` 包一层 `std::function`（§11 第 23 条） |

第 18、20、23、26、36 条属于"主动加戏"，其余都是被语言特性逼出来的。加它们的共同理由：纯增益且零风险。一个 C++ 容器库如果不能 `for (auto& x : list)`、一个比较器如果不能交给 `std::sort`，使用体验会立刻劝退；而 `reversed()` 在拉取式管道里只是一个缓冲闭包。

**一处主动收紧**：`AbstractSet::equals` 复刻 Java 的 `instanceof Set`，所以 `Set` 与同元素的 `List` 不相等。M8 把它从 `dynamic_cast` 换成了 `Collection::isSet()` 这个虚判别器（`Set` 覆写为 `true`），于是**全库不再依赖 RTTI**，`-fno-rtti` 可以直接构建。代价是一个 vtable 槽位。

---

## 9. 测试策略

- **doctest** v2.4.12 单头文件 vendor 在 `tests/third_party/doctest/doctest/doctest.h`，以 SYSTEM include 暴露，因此 vendored 头不贡献任何警告。不用 `FetchContent`：CLion 里 configure 阶段不联网，构建可靠性优先级高于省仓库体积。
- 每个能力域一个 `TEST_CASE` 分组的翻译单元，避免单文件编译时间失控。
- **语义测试优先于数值测试**：`peek` 的调用次数、`filter` 是否被短路、`findFirst` 在无限流上是否返回、`distinct` 是否保持首次出现顺序、`groupingBy` 的分组是否保持插入顺序。这些才是复刻的核心。
- **惰性测试**：用 `CountingSource`（记录实际拉取次数的源）断言"中间操作不终结就一个元素都不拉"。这比用 `peek` 计数更强：它证明的是源都没被碰过。
- **异常测试**：`IllegalStateException`（重复消费）、`NoSuchElementException`（空 `Optional::get`）、`ConcurrentModificationException`（迭代中修改）、`UnsupportedOperationException`（冻结列表修改）。
- **编译失败测试**：把几种误用喂给编译器并断言编译失败，是证明"编译期单次消费"真的生效的唯一办法。已在 M2 手工验证通过的三种：lvalue 上调中间操作、lvalue 上调终结操作、拷贝构造。M6 把它们接进 CTest。
- **三个配置每次都跑**：默认 Debug、`CPPSTREAM_WARNINGS_AS_ERRORS=ON`、`CPPSTREAM_ENABLE_SANITIZERS=ON`。Sanitizer 从 M0 就开着，不等 Collection 层——早开早暴露。
- **活视图的测试按"有没有真的走同一份存储"来写**：不只看返回值对不对，而是"通过视图写进去，从原容器读出来"，以及反向。视图迭代器还要断言"背着我改原容器必须 fail-fast"。
- **有序范围视图的期望值来自 JDK 实测**：`NavigableSubMap` 的栅栏规则（新栅栏能否放宽窗口、查找是否夹到窗口内、`computeIfPresent` 在窗口外动不动底层映射）在 javadoc 里写得很含糊，靠猜必错。做法是写一个等价的 Java 程序打印这些边界的输出，再把同一组用例翻译成 C++ 断言放进 `testRangeViews.cpp`——`floor(100)` 返回窗口末元素、`[20,40)` 上 `headSet(40,true)` 抛而 `headSet(40,false)` 可以、`computeIfPresent` 窗口外返回 null 且不改动映射，几条都是这样定下来的（`computeIfPresent` 那条直接修掉了一个真 bug，见 §6.8）。探针源文件留在 `tests/jdk/RangeViewProbe.java`，`javac RangeViewProbe.java && java RangeViewProbe` 即可复跑（实测环境 GraalVM 25.2.4）。

- **递减视图靠"对拍整段转录"验证，而不是逐条猜期望值**：M10 里写了两支探针 `tests/jdk/DescendingProbe.java` / `DescendingEdgeProbe.java`，另外各写了一份 Java 与 C++ 的转录程序，把 `NavigableSet` / `NavigableMap` 的**每一个**成员（迭代、`size`、`first` / `last`、四个查找、`headSet` / `tailSet` / `subSet` 的各种开闭组合、`descendingIterator` / `descendingSet`、`navigableKeySet` / `descendingKeySet`、`firstEntry` / `pollLastEntry`……）在一整组不同窗口（全量升序与递减、四种开闭区间、两侧无界窗口、双重取反、以及被接受却为空的 `[k, k)`）上的输出打印出来，然后 `diff` 两份文本。最终 777 行的集合转录与 606 行的映射转录**逐字节相同**。这比"挑几条断言"强得多：`[40, 40)` 这种被接受却为空的窗口、异常消息的用词、`floor` 的夹取方向，全都是这种对拍顺手抓出来的。
- **一次真实死循环就是这么抓到的**：对拍之外的 `testDescendingViews.cpp` 写了 `[20, 40)` 递减视图上的 `headSet(40, false)`——Java 接受它并给出空集，而本库当时把空窗口的原始边界当成了有效区间（`upper_bound(40)` 在 `lower_bound(40)` 之后），`size()` 里的 `std::distance` 于是从 `end()` 继续往前走，测试挂死。修法与回归用例见 §6.8。教训是"被接受的窗口"与"非空窗口"是两件事，Java 只保证前者。
- **`sizeHint` 是可观测行为，不是内部优化**：它被当成正常语义来断言（每个阶段该传、该丢、该夹紧都有用例），因为一个悄悄失传的提示无法用别的方式验证。
- **并行路径按"与顺序版对拍"验证**：并行不改变结果，只改变谁来算，所以 `testParallel.cpp` 里每条路径都写成"顺序版 == 并行版"。但只对拍还不够——一个永远走顺序分支的实现也能全绿，所以另外用 `std::set<std::thread::id>` 断言批次确实散到了多个线程上、用 `CountingSource` 断言无界源没有被预读（`findFirst` 的拉取次数远小于源的长度）、用"mapper 内部再跑一条并行管道"断言池内嵌套不死锁、用 `setParallelism(1)` 断言整体退化路径。

一条 M8 抓到的真 bug：`ArrayDequeIterator::remove()` 最初沿用了 `std::list` 的写法（`erase(it)` 后沿用游标）。`std::deque::erase` 会让**整条容器**的迭代器失效，于是下一次 `hasNext()` 读到悬垂迭代器，在 `std::deque::pop_back` 的断言上炸掉。修法是从 `erase()` 的返回值重建游标（反向迭代器则用 `make_reverse_iterator(后继)`）。`ArrayList` / `LinkedList` 的迭代器没这个问题：一个用下标游标，另一个基于 `std::list`，两者 erase 后只有被删的那个失效。

一条踩过的坑：终结操作标了 `[[nodiscard]]`，而 doctest 的 `CHECK_THROWS_AS(expr, ...)` 会丢弃 `expr` 的值，于是 `-Wunused-result` 在测试里报警。测试里需要写成 `CHECK_THROWS_AS(static_cast<void>(expr), ...)`。库侧不改：`[[nodiscard]]` 对终结操作是正确且值得的约束。

---

## 10. 里程碑

| 阶段 | 内容 | 状态 |
|---|---|---|
| M0 | CMake 骨架、doctest vendor、`.clang-format` / `.clang-tidy`、冒烟测试、CLion 可配置可运行 | ✅ |
| M1 | `RuntimeException` 层级、`Optional<T>`、`Comparator<T>`（`reversed` / `thenComparing` / `nullsFirst` / `nullsLast`） | ✅ |
| M2 | **Stream 内核**：拉取管道、move-only 单次消费、全部中间操作（`filter` / `map` / `flatMap` / `peek` / `distinct` / `sorted` / `reversed` / `limit` / `skip` / `takeWhile` / `dropWhile` / `unordered`）、基础终结操作（`forEach` / `count` / `toArray` / `reduce` / `min` / `max` / `findFirst` / `findAny` / 三种 `match`）、静态工厂（含 `iterate` / `generate` / `range` / `rangeClosed` / `concat`）、`parallel` 占位 | ✅ |
| M3 | `Iterator`（fail-fast）+ `Iterable` / `Collection` / `List` + `ArrayList` + `LinkedList` + `stream()` 桥接 | ✅ |
| M4 | `Collector` 模型 + `collect()` + `toList()` + `sum` / `average` | ✅ |
| M5 | `Collectors` 全量（`groupingBy` / `partitioningBy` / `teeing` / `summarizing*`） | ✅ |
| M6 | `Set` / `Map` + `HashSet` / `TreeSet` / `HashMap` / `TreeMap` + 冻结快照视图（M8 换成活视图） | ✅ |
| M7 | `Lists` / `Sets` / `Maps` 工具类、`examples/`、编译失败测试接入 CTest、README 完善 | ✅ |
| M8 | 活视图（`subList` / `keySet` / `values` / `entrySet`）、`Queue` / `Deque` + `ArrayDeque`、`descending*`、`gather()`、`sizeHint` 快路径与 `std::ranges` 互操作、去掉 RTTI | ✅ |
| M9 | 有序范围视图 `subSet` / `headSet` / `tailSet` / `subMap` / `headMap` / `tailMap`（含开闭区间重载），栅栏规则按 JDK 实测复刻 | ✅ |
| M10 | 补齐非并行流缺口：`descendingSet` / `descendingMap` / `descendingIterator` 改成方向标志驱动的**活视图**、`navigableKeySet` / `descendingKeySet`、`comparatorFrom` 桥接、空窗口的收敛修复、`Stream::mapMulti` | ✅ |
| M11 | **并行流**：`Parallel.h`（共享线程池 + `parallelism()` / `setParallelism()`）、`Budget` 拉取预算、`parallel()` / `sequential()` / `isParallel()` 落地、`map` / `filter` / `peek` 批并行、`flatMap` 的 mapper 并行、`collect` / `reduce` / `min` / `max` / `sum` 分片合并、`forEach` 批并行、三种 `match` 并行短路、`Collection::parallelStream()`、`testParallel.cpp` | ✅ |

排期上做过一次调整：原计划 M3 是"Stream 终结操作"、M4 才是容器。实际做的时候先把**基础**终结操作并在 M2 里交付了，因为惰性和短路是 Stream 的核心主张，没有终结操作就没法验证它们——用 `CountingSource` 断言"一个元素都没拉"比任何文档都有说服力。`collect` / `toList` 留在 M4，因为它们依赖 `Collector` 模型和 `ArrayList`。

执行时又改了两次顺序：**M6 与 M5 互换**（`toSet` / `toMap` / `groupingBy` 的返回类型是 `HashSet` / `HashMap`，先有容器层才不用返工），以及把 `LinkedList` 放到 M7 一起做（它验证的是 `List` 接口是否真的可复用，与工具类同一批测更省时间）。

M8 与 M9 里**并行流明确不做**（第 8 条当时固化为"声明但抛"），其余全部完成。顺序上先做"接口面"（`isSet` 去 RTTI、`descending*`、`Queue` / `Deque`），再做"最难的一处"（活视图），最后做 `gather()` 与 `sizeHint`。这个顺序的好处是每一批都能独立跑通三种配置，活视图那批出了问题时前面几批已经是绿的。这条欠账在 M11 里补上：并行流不是"再挂一个操作"，而是给阶段层加一条执行路径，所以放在所有阶段语义都冻结之后做，才不会边写并行边改语义。

依赖关系：M2 只依赖 M1，可以完全用 `std::vector` 当源来验证，不必等容器层。这是排期的关键松弛点。M5 可与 M6 并行。

---

## 11. 决策记录（ADR）

全部已确认，无遗留开放项。以下为最终口径。

| # | 决策 | 结论 | 影响 |
|---|---|---|---|
| 1 | 容器实现策略 | **包装 std 容器**，不自己实现数据结构 | `ArrayList` 包 `std::vector`，`HashMap` 包 `std::unordered_map`，`TreeSet` 包 `std::set` |
| 2 | 保真 vs 惯用法 | **保真优先** | 方法名、语义、异常行为对齐 Java；37 条偏离逐条记录于 §8 |
| 3 | 容器 null 语义 | **拒绝 null**，抛 `NullPointerException` | 值类型本无 null 概念，只对指针/可空类型做检查 |
| 4 | 并行流 | **做，但执行模型自定**（§7.10） | `parallel()` / `parallelStream()` 是真的；阶段按批上共享线程池，配 `Budget` 拉取预算界定预读。与 Java 的三点差别记在 §8 第 8 条；`Gatherer::combiner` 仍然缺席（`gather` 是屏障） |
| 5 | 测试框架 | **doctest**，单头文件 vendor | configure 阶段不联网；`add_test` 而非 `doctest_discover_tests` |
| 6 | 构建环境 | CLion 2026.2.1 内置 CMake 4.3.1 / Ninja 1.13.2 / GDB + 系统 `g++ 16.2.1` | 见 §4；`cmake_minimum_required(VERSION 3.25)`，对 CMake 4.x 合法 |
| 7 | 命名 | **驼峰**：类/结构体/概念大驼峰，函数/变量/枚举值小驼峰 | private 成员允许尾下划线（`modCount_`）作为撞名消歧后缀 |
| 8 | `Optional<T>` | **自建**，内部持 `std::optional<T>` | Java 方法名全保真（`isPresent` / `get` / `orElseThrow` / `ifPresent`），另有 `unwrap()` / `fromStd()` 双向互转 |
| 9 | 接口返回值 | **用具体类型**（§5.2） | `Stream::toList()` 返回 `ArrayList<T>`；接口只做多态参数与多态持有 |
| 10 | `Iterable::begin()` / `end()` | **加** | 让 range-for 与 `std::ranges` 算法可直接使用（§8 第 18 条） |
| 11 | C++ 惯用法成员 | **不提供 `operator==` / `operator[]` / 容器 `std::hash` 特化** | 只用 Java 风格的 `equals()` / `hashCode()` 虚函数，避免同一语义两套入口 |
| 12 | 项目 / target 名 | CMake `project(CppStream)`，target `cppstream::cppstream` | 目录名与 CMake 无关，重命名不影响构建 |
| 13 | 头文件后缀 | **`.h`** | C++ 标准未规定后缀；纯 C++23 库无 C 撞名风险 |
| 14 | 冻结容器 | **容器自带 `freeze()` 标志**，不引入包装类 | Java 的 `Collections.unmodifiable*` 返回包装视图；值语义下按值返回抽象类不成立（§5.2），只能冻结具体类型。代价是值拷贝保留冻结状态（§8 第 26 条） |
| 15 | `Set` 的继承位置 | **`Set : AbstractCollection`**，`AbstractSet : Set` | 与 `List` 同构的单一继承链；否则 `HashSet` 不是 `Set`，`instanceof Set` 恒假（§6.4） |
| 16 | 元素 `equals` / `hashCode` 的兜底 | **`Elements.h` 的 `elementEquals` / `elementHash`**，无 `==` / `std::hash` 时退化为同一性 | 让 `ArrayList<普通结构体>` 能编译，同时与 Java `Object` 的默认行为一致（§8 第 13 条） |
| 17 | 活视图怎么在值语义下活 | **视图类持容器指针 + `modCount` 快照**，生命周期按"迭代器同款"用文档约束 | Java 的 `SubList` / `KeySet` 是内部类，能直接读根对象的 `modCount`；C++ 的受保护访问规则不允许读"兄弟对象"的成员，所以在 `AbstractCollection` / `Map` 上各开了一个 `modCount()` 只读访问器。视图的失效检测与 Java 一模一样：`modCount` 变了就抛 `ConcurrentModificationException` |
| 18 | `Gatherer` 的状态类型 | **调用点显式给出**（`Gatherers::of<T,A,R>`），顺序组合用两级 `gather()` 表达 | C++ 没有目标类型推断，`R` 无法从 integrator 反推（§8 第 34 条） |
| 19 | `Stream::sizeHint` 的定位 | **估计值，只用于预留**，不参与任何语义 | 一旦用它实现 `count()` 的 O(1) 快路径，估计值就变成正确性依赖；Java 的 `estimateSize()` 同样只做资源提示（§7.8） |
| 20 | 有序范围视图的栅栏模型 | **`std::optional` 的 lo/hi + 各自的开闭标志**，新栅栏不得放宽窗口，规则以 JDK 实测为准 | 树是同一个树，视图只是"树 + 一对栅栏"，所以不需要拷贝也不需要新类型。规则本身照抄 `NavigableSubMap.inRange(key, inclusive)`，包括"查找夹到窗口内"和"`computeIfPresent` 窗口外返回 null 且不改动映射"两条反直觉的行为。视图被 `TreeSet` / `TreeMap` 声明为 `friend` 以直接定位栅栏，但写操作一律走公开 mutator，`modCount` 只在拥有它的那一处递增 |
| 21 | `modCount()` 改为虚函数 | **`AbstractCollection` 与 `Map` 的 `modCount()` 都是虚的**，`ListView` 与两个范围视图都覆写为"回答被看容器的计数" | 视图迭代的是底层存储，所以底层任何结构性改动都必须让它 fail-fast。`KeySetView` 之类是拿着 `const Map<K,V>*` 读 `modCount()` 的，非虚调用只会读到视图自己那个恒为 0 的计数器——把接口改成虚的，一处修改让所有"视图的视图"都正确（§6.8） |
| 22 | `descendingSet` / `descendingMap` 用副本还是视图 | **视图**，方向是 `TreeSetRangeView` / `TreeMapRangeView` 上的一个 `bool descending_`，栅栏仍存**升序**那一对 | Java 的 `DescendingSubMap` 就是同一棵树 + 同一个基类状态，只是把 `subCeiling` 映射到 `absFloor` 之类的"换义"操作。C++ 里若改成副本，`descendingSet().descendingSet()` 就无法回到原窗口，写透也没了。选方向标志后，`descendingSet()` 只是构造一个 `descending_` 取反的同窗口对象，"换义"集中在一处：四个查找先按升序问底层再按方向挑一对，`first` / `last` / `pollFirst` / `pollLast` / `headSet` / `tailSet` 同款；`subSet` 的窗口检查仍按升序那一对做，因为 Java 的 `DescendingSubMap.subMap` 就是把参数颠倒着传给基类构造器的（§6.8） |
| 23 | `comparator()` 的返回类型 | **`Comparator<T>`**（Java 风格），递减视图返回 `base.reversed()`；自由函数 `comparatorFrom<T>(Compare)` 把 std 风格 `Compare` 桥接过来 | 视图的 `Compare` 是 std 风格的 bool 比较器，`std::set` 把它焊进了数据结构，无法"取反后再当 `Compare` 用"。要按 Java 的契约交出"这个视图实际使用的顺序"，返回值必须换成可组合的 `Comparator<T>`；对递减视图返回底层的升序比较器则是**主动错误**——调用者拿它去排序会得到反的结果。Java 的 `reverseOrder(natural)` 同样是一个新对象而不是底层比较器本身（§5.7、§8 第 37 条） |
| 24 | 键集视图怎么持有窗口 | `TreeMapKeySetView<K, V, Compare>` **按值**持有一个 `TreeMapRangeView`，另存一个可写指针 | 范围视图本身就是"根指针 + 一对可选栅栏 + 方向"的把手：拷贝它不拷贝任何元素，也不会因为原临时对象析构而悬垂（这正是"视图对象拷贝后仍有效"的前提，与第 17 条的生命周期约定一致）。按值持有后，键集的每个成员都只是转发给窗口，`navigableKeySet` / `descendingKeySet` 就不需要各自重写一遍栅栏判定 |
| 25 | 并行流怎么在没有可切分源的架构上落地 | **`parallel()` 之后的阶段按批并行**，配套 `Budget` 拉取预算与池内嵌套内联（§7.10） | 备选一：把拉取闭包换成可切分的 `Spliterator` 等价物——重写全部中间操作，且 `filter` / `iterate` 本来就给不出切分点。备选二：在 `parallel()` 边界把上游物化成一个 buffer——直接毁掉惰性，`findFirst()` 会先把源抽干。批次方案把改动关在阶段内部，惰性与遭遇顺序都保住，代价是标志不能回溯、以及有界的预读 |

### 关于第 11 条的补充

不提供 `operator==` 是个有意的取舍。若同时存在 `operator==` 和 `equals()`，`HashSet<MyType>` 的相等语义就会出现两个入口，而 `HashMap` 的键相等又必须走 `std::equal_to`（`operator==`）——两套语义一旦不一致就会产生极难排查的 bug。因此：

- 容器 / 集合语义测试一律走 `equals()` / `hashCode()`。
- `HashSet` / `HashMap` 的键相等走 `std::equal_to` + `std::hash`（由标准库要求驱动），文档明确说明"若你的类型同时定义了 `equals()` 与 `operator==`，请保证二者一致"。
- 这是本库唯一一处"存在两个等价概念"的地方，用文档而非代码去约束。

---

## 12. 进度记录

环境实测：CLion 2026.2.1 自带 CMake **4.3.1** / Ninja 1.13.2 / GDB，系统 `g++ 16.2.1`。CMake 4.x 已移除对 `cmake_minimum_required(VERSION < 3.5)` 的兼容，本项目用 3.25，合法。项目目录名是 `CPPstream`，与 CMake 的 `project(CppStream)` 无关。

### M0 — 骨架（✅）

`CMakeLists.txt` + `cmake/CppStreamWarnings.cmake`；header-only `cppstream::cppstream`；doctest v2.4.12 vendor；`.clang-format` / `.clang-tidy` 落地 §2 命名规范；冒烟测试验证 C++23 特性探针与版本常量。

### M1 — 基础层（✅）

`RuntimeException.h`（9 个异常 + 两处子类继承）、`Optional.h`（Java 全方法名 + 与 `std::optional` 隐式双向转换，已实测无重载歧义）、`Comparator.h`（工厂与组合子齐全 + `operator()` 互操作）。

### M2 — Stream 惰性内核（✅）

`Stream.h`（672 行）实现拉取式管道：`std::move_only_function<std::optional<T>()>` 逐层类型擦除，状态按值捕获在闭包里，无堆上状态对象。全部 12 个中间操作 + 13 个基础终结操作 + 11 个静态工厂。

三层编译期防护已实测拒绝：lvalue 调中间操作、lvalue 调终结操作、拷贝构造。运行期二次消费抛 `IllegalStateException`，报错信息与 Java 逐字一致。

测试 56 个用例 / 186 条断言，三种配置（Debug、`-Werror`、ASan+UBSan）全部通过。

### M3 — 容器层（✅）

`Iterator.h` / `Iterable.h` / `Collection.h` / `AbstractCollection.h` / `List.h` / `ArrayList.h` / `LinkedList.h`。

- fail-fast 的 `modCount_` 与 `checkForComodification` **上提到 `AbstractCollection`**，于是整层（含 `removeAll` / `retainAll` / `clear` / `removeIf`）共享一份实现，不需要每个具体容器各写一遍。
- 迭代器分两条路径：`iterator()` 返回 `T&`（忠实于 Java），`constIterator()` 返回 `const T&`（C++ 的 const 正确性）。`Iterator<const T>` 的 `remove()` 自动落到默认的抛异常实现，零额外代码。
- **踩坑**：`ReadOnlyIterator` 原本用自己的默认构造状态当哨兵，但 `std::sentinel_for` 要求哨兵 `semiregular`（可拷贝），而单趟游标是 move-only，于是 `std::ranges` 与 range-for 都无法工作。拆出空的 `ReadOnlySentinel` 后一次通过（§8 第 28 条）。
- `List::remove(int)` 命名 `removeAt`（§8 第 3 条）；`subList` 当时返回值拷贝，M8 换成了 `ListView` 活视图（§8 第 11 条）。
- **踩坑**：`AbstractCollection::contains` / `equals` / `hashCode` 是虚函数重写，会被**急切实例化**，所以 `ArrayList<普通结构体>` 一开始根本编译不过。引入 `Elements.h` 的 `elementEquals` / `elementHash`（无 `==` / `std::hash` 时退化为 Java `Object` 的同一性）后解决（§8 第 13 条）。

### M4 — `Collector` 模型（✅）

`Collector.h` / `Elements.h`，以及 `Stream::collect` / `toList` / `sum` / `average`。

- `Collector` 用 `std::function`（可拷贝的规格对象），管道用 `move_only_function`（move-only 的单次游标）——这个不对称是有意的。
- `toList()` 的后置定义放在 `ArrayList.h`，`Stream.h` 只声明，按 §7.4 破环。
- `freeze()` / `isFrozen()` 落地：`Stream::toList()` 与 `Collectors::toUnmodifiable*` 返回冻结的具体容器（§8 第 26 条）。

### M5 — `Collectors`（✅）

`Collectors.h`（约 600 行）+ `SummaryStatistics.h`。

覆盖 `toList` / `toSet` / `toMap`×2 / `toUnmodifiable*` / `joining`×3 / `counting` / `summing*` / `averaging*` / `minBy` / `maxBy` / `reducing`×3 / `mapping` / `filtering` / `flatMapping` / `teeing` / `groupingBy`×2 / `partitioningBy`×2 / `summarizing*`。

- `CollectorLike` 用 `&&` 短路：C 不是收集器时后面的嵌套类型不会被构造，所以 `groupingBy` 写错下游只报一条可读错误，而不是几百行模板噪音。
- `groupingBy` 的累加器是 `std::unordered_map<K, Downstream::A>` 而非对外暴露的 `HashMap<K, R>`：分组需要 `operator[]` 形状的访问和"按需装一个下游空累加器"，`finisher` 再对每组跑一次下游收尾。
- `partitioningBy` 的 `finisher` 恒定产出 `false` / `true` 两个键，即使某一侧为空——这是 Java 的保证。
- `joining(delimiter, prefix, suffix)` 用"先写分隔符、收尾时删掉开头那一个"避免引入 `StringJoiner` 形状的累加器；分隔符为空串时该边界情况与 Java 结果一致。

### M6 — `Set` / `Map`（✅）

`Set.h` / `AbstractSet.h` / `HashSet.h` / `TreeSet.h` / `Map.h` / `HashMap.h` / `TreeMap.h`。

- **踩坑**：`Set` 最初声明为 `Set : Collection`，而 `AbstractSet : AbstractCollection`——于是 `HashSet` 根本不是 `Set`，`dynamic_cast<const Set<T>*>` 恒失败，集合相等判定永远是假。改成 `Set : AbstractCollection` + `AbstractSet : Set` 后修复（§6.4、§11 第 15 条）。
- `AbstractCollection::hashCode` 改成 Java `AbstractList` 的 `31*h + e` 有序折叠，`AbstractSet::hashCode` 覆写为无序求和——之前两者共用一个求和实现，`ArrayList` 的 `hashCode` 与 Java 不符。
- `Map` 把 Java 的 `Map` + `AbstractMap` 合并成一个类，靠 `visitEntries` 这一对 const/non-const 原语派生 `equals` / `hashCode`（M8 起 `keySet` / `values` / `entrySet` 由三个视图类承担）；这些派生成员**非虚**，以免强制每个 `TreeMap` 的键可哈希（§8 第 32 条）。
- `Map::put` 返回 `Optional<V>` 而不是抛异常：最初 §6.4 与 §8 第 5 条互相矛盾，实现时统一为"null ↔ `Optional::empty`"，与 `get` 返回指针是同一条规则（§8 第 5 条）。
- `TreeSet` / `TreeMap` 补上 NavigableSet / NavigableMap 的查询（`first` / `last` / `lower` / `floor` / `higher` / `ceiling` / `subSet` 等）；`descending*` 当时按 §8 第 30 条留到 M8，已在 M8 用 `ReversedCompare` 补上。

### M7 — 工具类、示例、编译失败测试（✅）

- `Lists.h` / `Sets.h` / `Maps.h`：Java 接口上的静态工厂搬到工具类（§8 第 6 条），一律返回冻结容器。
- `examples/pipelineTour.cpp`、`examples/wordFrequency.cpp`：可运行，覆盖惰性、短路、无限源、分组、teeing 与容器互操作。
- 5 个编译失败用例接入 CTest（`WILL_FAIL` + `-fsyntax-only`）：lvalue 调中间操作、lvalue 调终结操作、拷贝 `Stream`、收集器元素类型不匹配、`const char*` 不被提升为 `std::string`。

**测试规模（M7 结束时）**：132 个 `TEST_CASE` / 613 条断言 + 6 个 CTest 用例（1 个冒烟 + 5 个编译失败），三种配置（Debug、`-Werror`、ASan+UBSan）全部通过。`-Werror` 配置是真正的守门人：`-Wconversion` / `-Wshadow` / `-Wold-style-cast` 等在这轮里抓出了多处 `size_t` ↔ `int` 隐式转换。

### M8 — 活视图、队列族、gather、ranges（✅）

按批次完成：

1. **接口面**：`Collection::isSet()` 虚判别器顶掉 `AbstractSet::equals` 里的 `dynamic_cast`，全库不再依赖 RTTI；`Comparator.h` 加 `ReversedCompare`，`TreeSet::descendingSet` / `descendingIterator` / `descendingConstIterator` 与 `TreeMap::descendingMap` 用类型别名一次成型。
2. **队列族**：`Queue<T>`（`offer` / `poll` / `element` / `peek` + 抛异常的 `remove`）→ `Deque<T>`（双端 + 栈 + `descendingIterator`）→ `ArrayDeque<T>`（包装 `std::deque`，含三套 fail-fast 迭代器）。这一批抓到并修掉了 `ArrayDequeIterator::remove()` 里 `std::deque::erase` 全量失效迭代器的真 bug（§9）。
3. **活视图**：`ListView<T>`（`subList`）、`KeySetView` / `ValuesView` / `EntrySetView`（`keySet` / `values` / `entrySet`）。视图持容器指针 + `modCount` 快照，写透、fail-fast、const 只读，全部按 §8 第 11、12、35 条记录。
4. **`gather()`**：`Gatherer<T,A,R>` / `Downstream<R>` / `Void` 加上 `Gatherers` 的 `windowFixed` / `windowSliding` / `fold` / `scan` / `of` / `ofSequential`，用队列把推送式 gatherer 接进拉取式管道（§7.7）。
5. **ranges 与快路径**：`Stream<T>::ofRange` 直接吃 `std::ranges::input_range`，`Stream<T>::sizeHint()` 复刻 `Spliterator.estimateSize()` 并让 `sorted` / `reversed` / `toArray` 一次分配到位（§7.8 / §7.9）。

**测试规模（M8 结束时）**：180 个 `TEST_CASE` / 935 条断言 + 6 个 CTest 用例，三种配置（Debug、`-Werror`、ASan+UBSan）全部通过。新增测试单元：`testDeque.cpp`、`testListView.cpp`、`testMapViews.cpp`、`testGatherer.cpp`、`testRanges.cpp`；新增示例 `examples/collectionTour.cpp`。

### M9 — 有序范围视图（✅）

把 `TreeSet` / `TreeMap` 的六个范围方法从"返回拷贝"改成"返回活视图"，即 §8 第 30 条留下的最后一件工程。

1. **两个视图类**：`TreeSetRangeView<T, Compare>`（`AbstractSet<T>`）与 `TreeMapRangeView<K, V, Compare>`（`Map<K, V>`）。前者自带 `iterator()`，后者靠覆写 `visitEntries` 让 `keySet()` / `values()` / `entrySet()` 过滤到窗口（§6.8）。
2. **栅栏规则照 JDK 校准**：先写一个等价 Java 程序打印边界行为，再把同一组期望翻成断言。定下来的六条：窗口归属、越界写抛 `IllegalArgumentException`、越界 `remove` 返回空、新栅栏不得放宽窗口、"等于开区间栅栏且声称包含"被拒、查找夹到窗口内。
3. **一个真 bug**：`computeIfPresent` 最初直接委托底层 map，于是窗口外的 key 会被底层重映射——Java 返回 null 且**不动**映射。JDK 实测抓出，已修（§6.8）。
4. **`const` 只读，且只读会传递**：从 const 容器取到的范围视图，所有 mutator 与 `iterator()` 抛 `UnsupportedOperationException`；map 侧的可写 `V* get(key)` 重载同样抛，只读读取走 const 重载。只读视图上再开子窗口得到的是只读子视图而不是异常，与 Java 的 `unmodifiable*` 链一致——`ListView` 也按同一口径补齐（§8 第 35 条）。
5. **`modCount()` 改为虚函数**：视图回答被看容器的计数，于是"视图的 keySet"在底层 map 变动时照样 fail-fast（§11 第 21 条）。

当时把 `descendingSet` / `descendingMap` 留作"换反向比较器的副本"，并在文档里说明这不是欠账而是取舍（§8 第 30 条）——这个取舍在 M10 被推翻了，见下。

**测试规模（M9 结束时）**：199 个 `TEST_CASE` / 1144 条断言 + 6 个 CTest 用例，三种配置（Debug、`-Werror`、ASan+UBSan）全部通过；`-fno-rtti` 亦可直接构建。新增测试单元 `testRangeViews.cpp`（18 个用例，期望值全部来自 JDK 实测）；`testSet.cpp` / `testMap.cpp` 中原先按"拷贝"写的三处断言改为按视图写；`testListView.cpp` 补了两处——只读视图的 `subList` 仍是只读视图、视图的 `modCount()` 回答被看容器的计数；`examples/collectionTour.cpp` 加了一节范围视图的演示。

### M10 — 把非并行流的缺口补上（✅）

M9 结束时记在案的四处缺口里，除并行流外全部关闭。“保真优先”这条原则在这里起了作用：`descendingSet` 是副本还是视图，不是"能跑就行"的问题——副本下 `descendingSet().descendingSet()` 回不到原窗口，而这在 Java 里是可观察行为。

1. **方向从"换比较器"改成视图上的一个 `bool`**：`TreeSetRangeView` / `TreeMapRangeView` 的栅栏一律存**升序**那一对，`descending_` 只决定遍历方向与"哪个成员换哪个意义"。于是 `descendingSet` / `descendingMap` / `descendingIterator` 在容器上和视图上都成立，第二次 `descendingSet()` 换回原方向（§6.8、§11 第 22 条）。M8 为副本引入的 `ReversedCompare` / `TreeSet::Descending` 类型别名仍然保留为"独立的反向容器"，但不再是 `descendingSet()` 的返回物。
2. **`navigableKeySet` / `descendingKeySet`**：新增 `TreeMapKeySetView<K, V, Compare>`，一个真正可导航的 `NavigableSet<K>`——`floor` / `ceiling` / `subSet` / `headSet` / `tailSet` / `descendingIterator` 全部转发给它按值持有的那个窗口，`remove` 删映射，只读性从 const 容器一路传下来（§11 第 24 条）。
3. **`comparator()` 换成 Java 的返回类型**：`TreeSet` / `TreeMap` / 两个范围视图 / 键集视图的 `comparator()` 现在返回 `Comparator<T>`，递减视图返回 `reversed()`；新增自由函数 `comparatorFrom<T>(Compare)` 完成 std 风格 → Java 风格的桥接（§5.7、§8 第 37 条、§11 第 23 条）。
4. **`Stream::mapMulti`**：建在 `gather()` 上的一行转发，签名 `Stream<R> mapMulti<R>(F) &&`，`F: (const T&, Downstream<R>&) -> void`（§7.1、§7.7）。
5. **一个真死循环**：`testDescendingViews.cpp` 里 `[20, 40)` 递减视图上的 `headSet(40, false)` 挂死测试进程。根因是 Java 允许**被接受却为空**的窗口（开区间栅栏只需落在闭区间内，于是构造出升序的 `[40, 40)`），而本库把这种窗口反过来的原始边界当成了有效区间，`size()` 的 `std::distance` 从 `end()` 继续往前走。修法是在两个视图里先做空判定：候选首元素越过上栅栏就说明窗口为空，`firstInRange()` / `pastLastInRange()` 两端一起收敛到 `end()`（§6.8、§9）。

**测试规模（M10 结束时）**：223 个 `TEST_CASE` / 1402 条断言 + 6 个 CTest 用例，三种配置（Debug、`-Werror`、ASan+UBSan）全部通过；`-fno-rtti` 亦可直接构建。新增测试单元 `testDescendingViews.cpp`（14 个用例，其中一组专测"被接受却为空"的窗口）；`testSet.cpp` / `testMap.cpp` 中原先按"副本"写的断言改为按视图写并补了 `navigableKeySet` / `descendingKeySet` 的用例；`testStream.cpp` 补了三个 `mapMulti` 用例；`tests/jdk/` 新增两支递减探针；`examples/collectionTour.cpp` 补了一节递减视图与键集视图。与 JDK 的对拍转录扩到 777 行（集合）与 606 行（映射），逐字节相同。

### M11 — 并行流（✅）

M10 结束时唯一的记录在案缺口就是并行流，且当时的口径是"用户明确决定不做"。M11 把它做掉的难点不在"起线程"，而在**在不可切分的源上给出可解释的并行语义**：把源换成 `Spliterator` 等价物等于重写全部中间操作，在 `parallel()` 边界物化上游又会毁掉惰性。最后选的是第三方案——`parallel()` 之后的阶段按批上共享池，配一个 `Budget` 拉取预算来界定"能往前读多少"（§7.10、§11 第 25 条）。

1. **`Parallel.h`**：固定大小的线程池单例，`forEach(count, fn)` 用共享游标动态派活、异常在收拢后重抛；`thread_local` 标志让池内发起的并行阶段内联执行，把嵌套并行的死锁换成嵌套降级（ForkJoinPool 用 work-stealing 解决同一问题）。`cppstream::setParallelism(n)` 只改"可用 worker 数"这个 atomic，不建销线程，所以可以在任意时刻调用。
2. **`Budget` 与 `sizeHint` 分家**：`sizeHint` 是"这个阶段会产出多少"（`filter` 丢掉），`Budget` 是"这个阶段能往前读多少"（`filter` 保留）。批次大小取 `min(chunk, Budget 剩余)`，且 `Budget` 只会被收紧；源无界时是 `nullopt`，阶段退化为一次一个元素——`iterate(...).parallel().findFirst()` 因此不会预读挂死，`generate(...).parallel().limit(n)` 也能正常终止而无需任何特殊处理。
3. **一元阶段批并行**：`map` / `filter` / `peek` 共用一个 `mapBatch`（`step` 把元素映射成 `optional<R>`，`nullopt` 表示丢弃，`filter` 就是这么写的），结果按 `out[i] ↔ in[i]` 写回，遭遇顺序天然保持。`flatMap` 用 `flatBatch`：只有外层批次是急切的，内层流仍然惰性按序排空，所以"内层无限 + `limit`"依旧成立。
4. **终结操作分片合并**：`collect` / `reduce`（两种）/ `min` / `max` / `sum` 走同一个 `runParallelReduce`：一批切成 `parallelism()` 片，各片独立累加后**按遭遇顺序**逐个 `combiner` 合并。`Collector::combiner` 与 `Gatherer::combiner` 那个悬念就此落地——前者被调用（它从 M4 起就是构造参数），后者仍然缺席，因为 `gather` 是屏障。合并顺序保证 `toList` / `joining` / `groupingBy` 在并行下与顺序版逐字节相同。
5. **短路与顺序的边界照旧**：三种 `match` 并行求值但一批命中即停止拉取；`findFirst` / `findAny` 仍只拉一个元素（有界并行流不会被它抽干）；`forEachOrdered` / `count` / `toArray` / `toList` / `average` 顺序执行；`sorted` / `reversed` / `distinct` / `mapMulti` / `gather` 是屏障，但并行标志继续往下传。

**测试规模（M11 结束时）**：242 个 `TEST_CASE` / 1491 条断言 + 6 个 CTest 用例，三种配置（Debug、`-Werror`、ASan+UBSan）全部通过；`-fno-rtti` 亦可直接构建。新增测试单元 `testParallel.cpp`（19 个用例）：每条路径都拿顺序版当基准对拍，另有"并行确实用了多个线程"（`set<thread::id>` 计数）、"池内嵌套不死锁"、"无界源不预读"（`CountingSource` 数拉取次数）、"异常穿出并行阶段"、"`setParallelism(1)` 整体退化"、"`collect` 在 `groupingBy` 这类顺序敏感收集器上与顺序版相等"几条专门的用例。`Collection::parallelStream()` 让"先设标志再建阶段"成为默认写法。

### 下一步 — 未做的部分

只剩 `Gatherers::mapConcurrent` 一处：Java 把它定义在虚拟线程上，离开那一层它就是一个 `map`，而 `Stream::map` 已经在了（§7.7）。除此之外无记录在案的缺口——M9 结尾列的三条在 M10 关闭，M10 结尾列的并行流在 M11 关闭。
