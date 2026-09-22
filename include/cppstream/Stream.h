#pragma once

#include <cppstream/Collector.h>
#include <cppstream/cppstream_fwd.h>
#include <cppstream/Comparator.h>
#include <cppstream/Gatherer.h>
#include <cppstream/Optional.h>
#include <cppstream/RuntimeException.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cppstream {

template <class T>
class Stream;

/// Detects Stream<U> for any U, used to constrain flatMap.
template <class V>
struct IsStream : std::false_type {};

template <class V>
struct IsStream<Stream<V>> : std::true_type {};

template <class V>
inline constexpr bool isStream = IsStream<std::remove_cvref_t<V>>::value;

/// A port of java.util.stream.Stream.
///
/// The pipeline is pull-based. A Stream<T> owns exactly one stage, stored as a
/// type-erased std::move_only_function that yields the next element or an empty
/// optional once the stream is exhausted. Every intermediate operation wraps the
/// previous stage, so nothing is computed until a terminal operation asks for it.
/// Laziness, infinite sources and short-circuiting all follow from that:
/// findFirst() simply stops pulling.
///
/// std::function cannot be used here: it requires a copyable target, and stages
/// hold move-only state (the wrapped upstream stage). std::move_only_function is
/// C++23. Unlike Java there is no need for a heap-allocated state object per
/// stage -- the state lives inside the closure and is moved along with it.
///
/// Single use is enforced in two layers:
///   * every public member is &&-qualified, so `stream.filter(p)` on an lvalue
///     does not compile -- the common mistake becomes a compile error;
///   * terminal operations move the pipeline out and leave the object empty, so
///     any later use throws IllegalStateException, exactly as Java does.
///
/// Consumers handed to peek()/forEach() receive a mutable T&, matching Java's
/// reference semantics. Predicates receive the element too and should take it as
/// const T&.
template <class T>
class Stream {
public:
    static_assert(!std::is_reference_v<T>, "Stream<T> cannot carry a reference type");
    static_assert(!std::is_void_v<T>, "Stream<T> cannot carry void");
    static_assert(!std::is_const_v<T>, "Stream<T> cannot carry a const-qualified type");

    using valueType = T;

    /// Produces the next element, or an empty optional once the stream ends.
    using NextFn = std::move_only_function<std::optional<T>()>;

    /// How many elements the rest of the pipeline is expected to produce, when
    /// that is known. Java carries exactly the same information on
    /// Spliterator.estimateSize(), and uses it for the same thing: a
    /// sorted()/toArray()/toList() over a sized source preallocates its buffer
    /// instead of growing it.
    ///
    /// It is an *estimate*, never a contract. Every consumer must cope with
    /// nullopt, and no operation is allowed to be wrong if the hint is. Stages
    /// that change the element count in a way they cannot predict -- filter,
    /// flatMap, distinct, gather -- simply drop it, which is why the two-argument
    /// constructor below is opt-in rather than something every stage has to think
    /// about.
    using SizeHint = std::optional<std::size_t>;

    explicit Stream(NextFn next, SizeHint sizeHint = std::nullopt)
        : next_(std::move(next)), sizeHint_(sizeHint) {}

    Stream(Stream&&) noexcept = default;
    Stream& operator=(Stream&&) noexcept = default;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    ~Stream() = default;

    /// The size hint this stage was built with. See SizeHint.
    [[nodiscard]] SizeHint sizeHint() const noexcept { return sizeHint_; }

    // -----------------------------------------------------------------------
    // Intermediate operations
    // -----------------------------------------------------------------------

    /// Java: Stream.filter(predicate).
    template <class P>
    Stream<T> filter(P predicate) && {
        NextFn previous = takeNext();
        return Stream<T>(NextFn(
            [previous = std::move(previous), predicate = std::move(predicate)]() mutable
            -> std::optional<T> {
                for (;;) {
                    std::optional<T> candidate = previous();
                    if (!candidate) {
                        return std::nullopt;
                    }
                    if (std::invoke(predicate, *candidate)) {
                        return candidate;
                    }
                }
            }));
    }

    /// Java: Stream.map(mapper).
    template <class F>
        requires(!std::is_void_v<std::invoke_result_t<F&, T&>>)
    auto map(F mapper) && -> Stream<std::remove_cvref_t<std::invoke_result_t<F&, T&>>> {
        using R = std::remove_cvref_t<std::invoke_result_t<F&, T&>>;
        NextFn previous = takeNext();
        // map is one-for-one, so whatever the source promised still holds.
        const SizeHint promised = sizeHint_;
        return Stream<R>(typename Stream<R>::NextFn(
            [previous = std::move(previous), mapper = std::move(mapper)]() mutable
            -> std::optional<R> {
                std::optional<T> value = previous();
                if (!value) {
                    return std::nullopt;
                }
                return std::optional<R>(std::in_place, std::invoke(mapper, *value));
            }), promised);
    }

    /// Java: Stream.flatMap(mapper), where the mapper returns a Stream per
    /// element. Each inner stream is drained before the next outer element is
    /// pulled, so an infinite outer stream works as long as inner streams end,
    /// and an inner stream may itself be infinite.
    template <class F>
        requires(isStream<std::invoke_result_t<F&, T&>> &&
                 !std::is_reference_v<std::invoke_result_t<F&, T&>>)
    auto flatMap(F mapper) && -> std::remove_cvref_t<std::invoke_result_t<F&, T&>> {
        using Inner = std::remove_cvref_t<std::invoke_result_t<F&, T&>>;
        using R = typename Inner::valueType;

        NextFn previous = takeNext();
        return Inner([previous = std::move(previous), mapper = std::move(mapper),
                         innerNext = std::optional<typename Inner::NextFn>{}]() mutable
                     -> std::optional<R> {
            for (;;) {
                if (innerNext) {
                    std::optional<R> value = (*innerNext)();
                    if (value) {
                        return value;
                    }
                    innerNext.reset();
                }

                std::optional<T> outer = previous();
                if (!outer) {
                    return std::nullopt;
                }
                Inner inner = std::invoke(mapper, *outer);
                innerNext = std::move(inner).takeNext();
            }
        });
    }

    /// Java 16: Stream.mapMulti(mapper), the imperative counterpart of flatMap.
    ///
    /// The mapper is handed the element and a sink, and pushes zero, one or many
    /// results into it, instead of returning an inner stream. Java spells the sink
    /// java.util.function.Consumer; here it is the Downstream<R> that gather()
    /// already uses, so push() is Consumer.accept().
    ///
    /// Two C++ spellings of Java's `<R> Stream<R> mapMulti(BiConsumer<...>)`:
    ///
    ///   * R is named at the call site -- `mapMulti<int>(...)` -- because C++ has
    ///     no target typing to infer it from the mapper's parameter, the same
    ///     reason Gatherer's state and element types are named (divergence 34).
    ///   * The element arrives as `const T&`, matching the value a Java BiConsumer
    ///     receives.
    ///
    /// Built on gather(): "one gatherer that emits many elements per input" is
    /// exactly this operation, so the pull/push bridge lives in one place, and
    /// mapMulti's size hint is dropped for the same reason gather's is.
    template <class R, class F>
        requires std::is_invocable_v<F&, const T&, Downstream<R>&>
    Stream<R> mapMulti(F mapper) && {
        return std::move(*this).gather(Gatherer<T, Void, R>(
            [mapper = std::move(mapper)](Void&, const T& element, Downstream<R>& downstream) -> bool {
                std::invoke(mapper, element, downstream);
                return true;
            }));
    }

    /// Java 22: Stream.gather(gatherer).
    ///
    /// A gatherer is push-based (`downstream.push(element)`) while this pipeline
    /// is pull-based, so the two are bridged with a buffer: the integrator is run
    /// until it either emits something or the upstream runs dry, and everything it
    /// emitted is queued for the following pulls.
    ///
    /// A, R are deduced from the gatherer's type, which is why Gatherer is
    /// parameterised on both: C++ cannot infer them from the integrator's
    /// parameters the way Java's target typing does.
    template <class A, class R>
    Stream<R> gather(Gatherer<T, A, R> gatherer) && {
        NextFn previous = takeNext();

        // Downstream only ever holds a reference to this buffer, so the two
        // closures that need it -- the sink and the pull stage -- share ownership.
        auto pending = std::make_shared<std::deque<R>>();
        Downstream<R> downstream([pending](R element) {
            pending->push_back(std::move(element));
            return true;
        });

        // initialState() must be read before the gatherer is moved into the
        // capture below; capture initializers are evaluated in written order, but
        // relying on that for correctness is a trap.
        A accumulated = gatherer.initialState();

        return Stream<R>(typename Stream<R>::NextFn(
            [previous = std::move(previous), gatherer = std::move(gatherer),
                accumulated = std::move(accumulated), downstream = std::move(downstream),
                pending, done = false]() mutable -> std::optional<R> {
                for (;;) {
                    if (!pending->empty()) {
                        R front = std::move(pending->front());
                        pending->pop_front();
                        return std::optional<R>(std::in_place, std::move(front));
                    }
                    if (done) {
                        return std::nullopt;
                    }
                    std::optional<T> element = previous();
                    const bool keepGoing =
                        element.has_value() && gatherer.integrate(accumulated, *element, downstream);
                    if (!element.has_value() || !keepGoing) {
                        done = true;
                        gatherer.finish(accumulated, downstream);
                    }
                }
            }));
    }

    /// Java: Stream.peek(action). A pure side-effect hook; elements pass through
    /// untouched.
    template <class F>
    Stream<T> peek(F action) && {
        NextFn previous = takeNext();
        const SizeHint promised = sizeHint_;
        return Stream<T>(NextFn(
            [previous = std::move(previous), action = std::move(action)]() mutable
            -> std::optional<T> {
                std::optional<T> value = previous();
                if (value) {
                    std::invoke(action, *value);
                }
                return value;
            }), promised);
    }

    /// Java: Stream.distinct(). Keeps the first occurrence of each element and
    /// therefore preserves encounter order.
    Stream<T> distinct() && {
        NextFn previous = takeNext();
        return Stream<T>(NextFn(
            [previous = std::move(previous), seen = std::unordered_set<T>{}]() mutable
            -> std::optional<T> {
                for (;;) {
                    std::optional<T> value = previous();
                    if (!value) {
                        return std::nullopt;
                    }
                    if (seen.insert(*value).second) {
                        return value;
                    }
                }
            }));
    }

    /// Java: Stream.sorted(). Requires operator< on T.
    Stream<T> sorted() && { return std::move(*this).sorted(Comparator<T>::naturalOrder()); }

    /// Java: Stream.sorted(comparator). Buffers the whole stream on the first
    /// pull, so it never terminates on an infinite stream -- in Java either.
    Stream<T> sorted(const Comparator<T>& comparator) && {
        NextFn previous = takeNext();
        // Reordering preserves the element count, and the hint is what makes the
        // one buffer this stage needs a single allocation.
        const SizeHint promised = sizeHint_;
        return Stream<T>(NextFn(
            [previous = std::move(previous), comparator, promised,
                buffer = std::optional<std::vector<T>>{},
                index = std::size_t{0}]() mutable -> std::optional<T> {
                if (!buffer) {
                    std::vector<T> collected;
                    collected.reserve(promised.value_or(0));
                    while (std::optional<T> value = previous()) {
                        collected.push_back(std::move(*value));
                    }
                    std::sort(collected.begin(), collected.end(),
                        [&comparator](const T& left, const T& right) {
                            return comparator.compare(left, right) < 0;
                        });
                    buffer = std::move(collected);
                }
                if (index >= buffer->size()) {
                    return std::nullopt;
                }
                return std::optional<T>(std::in_place, std::move((*buffer)[index++]));
            }), promised);
    }

    /// Not a Java Stream method: mirrors SequencedCollection.reversed()
    /// (Java 21), which Java exposes on collections only. Buffers like sorted().
    Stream<T> reversed() && {
        NextFn previous = takeNext();
        const SizeHint promised = sizeHint_;
        return Stream<T>(NextFn(
            [previous = std::move(previous), promised,
                buffer = std::optional<std::vector<T>>{},
                index = std::size_t{0}]() mutable -> std::optional<T> {
                if (!buffer) {
                    std::vector<T> collected;
                    collected.reserve(promised.value_or(0));
                    while (std::optional<T> value = previous()) {
                        collected.push_back(std::move(*value));
                    }
                    buffer = std::move(collected);
                }
                if (index >= buffer->size()) {
                    return std::nullopt;
                }
                const std::size_t position = buffer->size() - 1 - index;
                ++index;
                return std::optional<T>(std::in_place, std::move((*buffer)[position]));
            }), promised);
    }

    /// Java: Stream.limit(maxSize).
    Stream<T> limit(std::int64_t maxSize) && {
        if (maxSize < 0) {
            throw IllegalArgumentException("maxSize must be non-negative: " +
                                           std::to_string(maxSize));
        }
        NextFn previous = takeNext();
        const SizeHint promised = limited(sizeHint_, maxSize);
        return Stream<T>(NextFn([previous = std::move(previous),
                                    remaining = maxSize]() mutable -> std::optional<T> {
            if (remaining <= 0) {
                return std::nullopt;
            }
            std::optional<T> value = previous();
            if (!value) {
                remaining = 0;
                return std::nullopt;
            }
            --remaining;
            return value;
        }), promised);
    }

    /// Java: Stream.skip(n).
    Stream<T> skip(std::int64_t n) && {
        if (n < 0) {
            throw IllegalArgumentException("n must be non-negative: " + std::to_string(n));
        }
        NextFn previous = takeNext();
        const SizeHint promised = skipped(sizeHint_, n);
        return Stream<T>(NextFn([previous = std::move(previous),
                                    remaining = n]() mutable -> std::optional<T> {
            while (remaining > 0) {
                if (!previous()) {
                    remaining = 0;
                    return std::nullopt;
                }
                --remaining;
            }
            return previous();
        }), promised);
    }

    /// Java 9: Stream.takeWhile(predicate).
    template <class P>
    Stream<T> takeWhile(P predicate) && {
        NextFn previous = takeNext();
        return Stream<T>(NextFn([previous = std::move(previous),
                                    predicate = std::move(predicate),
                                    stopped = false]() mutable -> std::optional<T> {
            if (stopped) {
                return std::nullopt;
            }
            std::optional<T> value = previous();
            if (!value || !std::invoke(predicate, *value)) {
                stopped = true;
                return std::nullopt;
            }
            return value;
        }));
    }

    /// Java 9: Stream.dropWhile(predicate).
    template <class P>
    Stream<T> dropWhile(P predicate) && {
        NextFn previous = takeNext();
        return Stream<T>(NextFn([previous = std::move(previous),
                                    predicate = std::move(predicate),
                                    dropping = true]() mutable -> std::optional<T> {
            while (dropping) {
                std::optional<T> value = previous();
                if (!value) {
                    dropping = false;
                    return std::nullopt;
                }
                if (!std::invoke(predicate, *value)) {
                    dropping = false;
                    return value;
                }
            }
            return previous();
        }));
    }

    /// Java: Stream.unordered(). A no-op here, and permitted to be one: Java
    /// documents it as a hint that lets the implementation drop encounter order,
    /// and a sequential stream is free to keep it.
    Stream<T> unordered() && { return std::move(*this); }

    // -----------------------------------------------------------------------
    // Terminal operations
    // -----------------------------------------------------------------------

    /// Java: Stream.forEach(action). The action receives a mutable reference.
    template <class F>
    void forEach(F action) && {
        NextFn next = takeNext();
        while (std::optional<T> value = next()) {
            std::invoke(action, *value);
        }
    }

    /// Java: Stream.forEachOrdered(action). Identical to forEach without parallel
    /// streams, which is exactly what Java promises for a sequential stream.
    template <class F>
    void forEachOrdered(F action) && {
        std::move(*this).forEach(std::move(action));
    }

    /// Java: Stream.count().
    [[nodiscard]] std::int64_t count() && {
        NextFn next = takeNext();
        std::int64_t total = 0;
        while (next()) {
            ++total;
        }
        return total;
    }

    /// Java: Stream.toArray().
    [[nodiscard]] std::vector<T> toArray() && {
        NextFn next = takeNext();
        std::vector<T> result;
        // The one place the hint pays for itself visibly: a single allocation for
        // the common "collect a sized stream" case.
        result.reserve(sizeHint_.value_or(0));
        while (std::optional<T> value = next()) {
            result.push_back(std::move(*value));
        }
        return result;
    }

    /// Java: Stream.collect(collector).
    ///
    /// The reduce-all form: supplier -> accumulate every element -> finish. The
    /// combiner is never called here, because a sequential pipeline has nothing to
    /// merge -- it exists for parallel streams and for the collectors that
    /// partitioningBy/groupingBy build out of it.
    ///
    /// Constrained by CollectorLike so that a mistyped collector fails here, at
    /// the call site, rather than somewhere inside the pipeline's type erasure.
    template <class C>
        requires CollectorLike<C, T>
    [[nodiscard]] auto collect(C collector) && -> typename C::resultType {
        NextFn next = takeNext();
        typename C::accumulatorType accumulator = std::invoke(collector.supplier());
        const typename C::Accumulator& accumulate = collector.accumulator();
        while (std::optional<T> value = next()) {
            std::invoke(accumulate, accumulator, *value);
        }
        return std::invoke(collector.finisher(), std::move(accumulator));
    }

    /// Java: Stream.toList(), which returns an unmodifiable List.
    ///
    /// Returns a concrete ArrayList because a value-returning function cannot
    /// return the abstract List (DESIGN.md section 5.2). Defined out of line, in
    /// ArrayList.h, to break the Stream <-> ArrayList include cycle (section 7.4).
    [[nodiscard]] ArrayList<T> toList() &&;

    /// Java: IntStream/LongStream/DoubleStream.sum(), folded onto Stream<T>.
    ///
    /// C++ templates have no boxing cost, so the three primitive stream types have
    /// no reason to exist; the numeric operations become concept-constrained
    /// members. This is the one place the port is *smaller* than Java.
    [[nodiscard]] T sum() && requires(std::integral<T> || std::floating_point<T>) {
        NextFn next = takeNext();
        T total{};
        while (std::optional<T> value = next()) {
            total += *value;
        }
        return total;
    }

    /// Java: IntStream/LongStream/DoubleStream.average(). Always returns double,
    /// exactly as Java's IntStream.average() returns OptionalDouble.
    [[nodiscard]] Optional<double> average() &&
        requires(std::integral<T> || std::floating_point<T>) {
        NextFn next = takeNext();
        double total = 0.0;
        std::int64_t seen = 0;
        while (std::optional<T> value = next()) {
            total += static_cast<double>(*value);
            ++seen;
        }
        if (seen == 0) {
            return Optional<double>::empty();
        }
        return Optional<double>::of(total / static_cast<double>(seen));
    }

    /// Java: Stream.findFirst(). Short-circuits: exactly one element is pulled.
    [[nodiscard]] Optional<T> findFirst() && {
        NextFn next = takeNext();
        std::optional<T> value = next();
        if (!value) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(std::move(*value));
    }

    /// Java: Stream.findAny(). A sequential stream has no freedom to choose, so
    /// this is findFirst(), which is what Java does sequentially too.
    [[nodiscard]] Optional<T> findAny() && { return std::move(*this).findFirst(); }

    /// Java: Stream.anyMatch(predicate). Short-circuits on the first match.
    template <class P>
    [[nodiscard]] bool anyMatch(P predicate) && {
        NextFn next = takeNext();
        while (std::optional<T> value = next()) {
            if (std::invoke(predicate, *value)) {
                return true;
            }
        }
        return false;
    }

    /// Java: Stream.allMatch(predicate).
    template <class P>
    [[nodiscard]] bool allMatch(P predicate) && {
        NextFn next = takeNext();
        while (std::optional<T> value = next()) {
            if (!std::invoke(predicate, *value)) {
                return false;
            }
        }
        return true;
    }

    /// Java: Stream.noneMatch(predicate).
    template <class P>
    [[nodiscard]] bool noneMatch(P predicate) && {
        NextFn next = takeNext();
        while (std::optional<T> value = next()) {
            if (std::invoke(predicate, *value)) {
                return false;
            }
        }
        return true;
    }

    /// Java: Stream.reduce(accumulator). Empty in, empty out.
    template <class F>
    [[nodiscard]] Optional<T> reduce(F accumulator) && {
        NextFn next = takeNext();
        std::optional<T> seed = next();
        if (!seed) {
            return Optional<T>::empty();
        }
        T result = std::move(*seed);
        while (std::optional<T> value = next()) {
            result = std::invoke(accumulator, std::move(result), std::move(*value));
        }
        return Optional<T>::of(std::move(result));
    }

    /// Java: Stream.reduce(identity, accumulator).
    template <class F>
    [[nodiscard]] T reduce(T identity, F accumulator) && {
        NextFn next = takeNext();
        T result = std::move(identity);
        while (std::optional<T> value = next()) {
            result = std::invoke(accumulator, std::move(result), std::move(*value));
        }
        return result;
    }

    /// Java: Stream.min(comparator).
    [[nodiscard]] Optional<T> min(const Comparator<T>& comparator) && {
        NextFn next = takeNext();
        std::optional<T> seed = next();
        if (!seed) {
            return Optional<T>::empty();
        }
        T best = std::move(*seed);
        while (std::optional<T> value = next()) {
            if (comparator.compare(*value, best) < 0) {
                best = std::move(*value);
            }
        }
        return Optional<T>::of(std::move(best));
    }

    /// Java: Stream.max(comparator).
    [[nodiscard]] Optional<T> max(const Comparator<T>& comparator) && {
        NextFn next = takeNext();
        std::optional<T> seed = next();
        if (!seed) {
            return Optional<T>::empty();
        }
        T best = std::move(*seed);
        while (std::optional<T> value = next()) {
            if (comparator.compare(*value, best) > 0) {
                best = std::move(*value);
            }
        }
        return Optional<T>::of(std::move(best));
    }

    // -----------------------------------------------------------------------
    // Parallelism: declared for source compatibility, not implemented
    // -----------------------------------------------------------------------

    /// Java: Stream.parallel(). Not implemented in this version. Declared so
    /// that ported code fails loudly at the call site instead of silently
    /// running sequentially.
    Stream<T> parallel() && {
        throw UnsupportedOperationException(
            "parallel streams are not implemented in this version");
    }

    /// Java: Stream.sequential(). A no-op, since every stream is sequential.
    Stream<T> sequential() && { return std::move(*this); }

    /// Java: Stream.isParallel().
    [[nodiscard]] bool isParallel() const noexcept { return false; }

    // -----------------------------------------------------------------------
    // Factories. Java hangs these off Stream / IntStream static methods; in C++
    // the element type comes from the class template argument.
    // -----------------------------------------------------------------------

    /// Java: Stream.empty().
    static Stream<T> empty() {
        return Stream<T>(NextFn([]() -> std::optional<T> { return std::nullopt; }), 0);
    }

    /// Java: Stream.of(values...). Every argument must already be exactly T: C++
    /// will not coerce `const char*` to std::string the way Java's inference
    /// does, so pass std::string explicitly for strings.
    template <class U, class... Rest>
        requires std::same_as<std::remove_cvref_t<U>, T> &&
                 (std::same_as<std::remove_cvref_t<Rest>, T> && ...)
    static Stream<T> of(U&& first, Rest&&... rest) {
        std::vector<T> values;
        values.reserve(1 + sizeof...(Rest));
        values.push_back(std::forward<U>(first));
        (values.push_back(std::forward<Rest>(rest)), ...);
        return of(std::move(values));
    }

    /// Java: Stream.of(array) / Stream.of(collection) in spirit; a vector is the
    /// C++ spelling of "a sequence of T".
    static Stream<T> of(std::vector<T> values) {
        const std::size_t promised = values.size();
        return Stream<T>(NextFn([values = std::move(values),
                                    index = std::size_t{0}]() mutable -> std::optional<T> {
            if (index >= values.size()) {
                return std::nullopt;
            }
            return std::optional<T>(std::in_place, std::move(values[index++]));
        }), promised);
    }

    static Stream<T> of(std::initializer_list<T> values) {
        return of(std::vector<T>(values));
    }

    /// C++-only: builds a stream from any std::ranges range, without wrapping it
    /// in an Iterable first. This is the interop half of the ranges work; the
    /// size hint, taken from std::ranges::sized_range, is the fast-path half.
    ///
    /// An lvalue range is borrowed -- the stream reads it where it stands -- so it
    /// must outlive the stream, exactly as with Iterable::begin(). An rvalue range
    /// is moved into the stage and owned from then on.
    template <std::ranges::input_range Rg>
    static Stream<std::ranges::range_value_t<Rg>> ofRange(Rg& range) {
        using R = std::ranges::range_value_t<Rg>;
        return Stream<R>(typename Stream<R>::NextFn(
            [it = std::ranges::begin(range), last = std::ranges::end(range)]() mutable
            -> std::optional<R> {
                if (it == last) {
                    return std::nullopt;
                }
                R value = *it;
                ++it;
                return std::optional<R>(std::in_place, std::move(value));
            }), hintFor(range));
    }

    template <std::ranges::input_range Rg>
        requires(!std::is_lvalue_reference_v<Rg>)
    static Stream<std::ranges::range_value_t<Rg>> ofRange(Rg&& range) {
        using R = std::ranges::range_value_t<Rg>;
        using Cursor = std::ranges::iterator_t<Rg>;
        const SizeHint promised = hintFor(range);
        return Stream<R>(typename Stream<R>::NextFn(
            [owned = std::forward<Rg>(range),
                cursor = std::optional<Cursor>{}]() mutable -> std::optional<R> {
                // Built on the first pull, once the closure has stopped moving, so
                // that the cursor really does point into `owned` as it now stands.
                if (!cursor.has_value()) {
                    cursor.emplace(std::ranges::begin(owned));
                }
                if (*cursor == std::ranges::end(owned)) {
                    return std::nullopt;
                }
                R value = **cursor;
                ++(*cursor);
                return std::optional<R>(std::in_place, std::move(value));
            }), promised);
    }

    /// Java: Stream.ofNullable(value).
    static Stream<T> ofNullable(T value) {
        if constexpr (std::is_pointer_v<T>) {
            if (value == nullptr) {
                return empty();
            }
        }
        return of(std::move(value));
    }

    /// Java: Stream.iterate(seed, next). Infinite.
    template <class F>
        requires std::is_invocable_v<F&, const T&> &&
                 std::convertible_to<std::invoke_result_t<F&, const T&>, T>
    static Stream<T> iterate(T seed, F next) {
        return Stream<T>(NextFn([value = std::optional<T>(std::in_place, std::move(seed)),
                                    next = std::move(next)]() mutable -> std::optional<T> {
            if (!value) {
                return std::nullopt;
            }
            T result = std::move(*value);
            value = std::invoke(next, result);
            return std::optional<T>(std::in_place, std::move(result));
        }));
    }

    /// Java 9: Stream.iterate(seed, hasNext, next). Produces the same sequence
    /// as the equivalent for-loop: the predicate is checked before each
    /// emission, so a seed that fails hasNext yields an empty stream.
    template <class P, class F>
        requires std::is_invocable_v<P&, const T&> &&
                 std::convertible_to<std::invoke_result_t<P&, const T&>, bool> &&
                 std::is_invocable_v<F&, const T&> &&
                 std::convertible_to<std::invoke_result_t<F&, const T&>, T>
    static Stream<T> iterate(T seed, P hasNext, F next) {
        return Stream<T>(NextFn([value = std::optional<T>(std::in_place, std::move(seed)),
                                    hasNext = std::move(hasNext),
                                    next = std::move(next)]() mutable -> std::optional<T> {
            if (!value || !std::invoke(hasNext, *value)) {
                return std::nullopt;
            }
            T result = std::move(*value);
            value = std::invoke(next, result);
            return std::optional<T>(std::in_place, std::move(result));
        }));
    }

    /// Java: Stream.generate(supplier). Infinite.
    template <class S>
        requires std::convertible_to<std::invoke_result_t<S&>, T>
    static Stream<T> generate(S supplier) {
        return Stream<T>(NextFn([supplier = std::move(supplier)]() mutable -> std::optional<T> {
            return std::optional<T>(std::in_place, std::invoke(supplier));
        }));
    }

    /// Java: Stream.concat(first, second). Lazy: second is untouched until first
    /// is exhausted, so an infinite first stream is fine on its own.
    static Stream<T> concat(Stream<T> first, Stream<T> second) {
        const SizeHint promised = combined(first.sizeHint(), second.sizeHint());
        NextFn firstNext = std::move(first).takeNext();
        NextFn secondNext = std::move(second).takeNext();
        return Stream<T>(NextFn([firstNext = std::move(firstNext),
                                    secondNext = std::move(secondNext),
                                    drainingFirst = true]() mutable -> std::optional<T> {
            if (drainingFirst) {
                std::optional<T> value = firstNext();
                if (value) {
                    return value;
                }
                drainingFirst = false;
            }
            return secondNext();
        }), promised);
    }

    /// Java: IntStream.range(startInclusive, endExclusive), generalised over any
    /// integral T. An empty stream results when start >= end.
    static Stream<T> range(T startInclusive, T endExclusive)
        requires std::integral<T>
    {
        return Stream<T>(NextFn([current = startInclusive,
                                    end = endExclusive]() mutable -> std::optional<T> {
            if (current >= end) {
                return std::nullopt;
            }
            return std::optional<T>(std::in_place, current++);
        }), spanHint(startInclusive, endExclusive, 0));
    }

    /// Java: IntStream.rangeClosed(startInclusive, endInclusive). Uses an
    /// exhausted sentinel rather than comparing after an increment, so it also
    /// terminates when endInclusive is the maximum value of T.
    static Stream<T> rangeClosed(T startInclusive, T endInclusive)
        requires std::integral<T>
    {
        return Stream<T>(NextFn([current = startInclusive, end = endInclusive,
                                    exhausted = false]() mutable -> std::optional<T> {
            if (exhausted) {
                return std::nullopt;
            }
            const T result = current;
            if (current == end) {
                exhausted = true;
            } else {
                ++current;
            }
            return std::optional<T>(std::in_place, result);
        }), spanHint(startInclusive, endInclusive, 1));
    }

private:
    /// The size hint of a std::ranges range, when the range can answer that
    /// question in constant time. An input_range that is not sized simply has no
    /// hint, which is the same answer a filter gives.
    template <class Rg>
    [[nodiscard]] static SizeHint hintFor(Rg& range) {
        if constexpr (std::ranges::sized_range<Rg>) {
            return static_cast<std::size_t>(std::ranges::size(range));
        } else {
            (void)range;
            return std::nullopt;
        }
    }

    /// limit() cannot produce more than the source promised, nor more than the cap.
    [[nodiscard]] static SizeHint limited(SizeHint hint, std::int64_t cap) {
        if (!hint) {
            return std::nullopt;
        }
        const std::size_t ceiling =
            cap < 0 ? 0 : static_cast<std::size_t>(cap);
        return std::min(*hint, ceiling);
    }

    /// skip() takes up to n elements off the front.
    [[nodiscard]] static SizeHint skipped(SizeHint hint, std::int64_t n) {
        if (!hint || n <= 0) {
            return hint;
        }
        const std::size_t dropped = static_cast<std::size_t>(n);
        return *hint > dropped ? SizeHint(*hint - dropped) : SizeHint(0);
    }

    /// concat() knows the total only when both halves do.
    [[nodiscard]] static SizeHint combined(SizeHint left, SizeHint right) {
        if (!left || !right) {
            return std::nullopt;
        }
        return *left + *right;
    }

    /// The length of [start, end] (inclusiveOffset 1) as a hint, saturating at
    /// zero. Computed in the unsigned domain so that an end below start cannot
    /// wrap around.
    static SizeHint spanHint(T start, T end, T inclusiveOffset)
        requires std::integral<T>
    {
        if (end < start) {
            return std::size_t{0};
        }
        const auto length = static_cast<std::size_t>(end - start) +
                            static_cast<std::size_t>(inclusiveOffset);
        return length;
    }

    /// Moves the pipeline out and leaves the stream empty. Terminal operations
    /// call this first, which is what makes a second terminal operation throw
    /// rather than silently return an empty result.
    [[nodiscard]] NextFn takeNext() {
        if (!next_) {
            throw IllegalStateException("stream has already been operated upon or closed");
        }
        NextFn taken = std::move(next_);
        next_ = nullptr;
        return taken;
    }

    NextFn next_;

    /// See SizeHint. Never read by anything that would be wrong without it.
    SizeHint sizeHint_;

    /// flatMap drives an inner stream of a different instantiation, so every
    /// instantiation must be able to reach takeNext().
    template <class U>
    friend class Stream;
};

/// Java: Optional.stream(), yielding a zero- or one-element stream.
template <class T>
Stream<T> Optional<T>::stream() const {
    if (isEmpty()) {
        return Stream<T>::empty();
    }
    return Stream<T>::of(get());
}

}  // namespace cppstream
