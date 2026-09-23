#pragma once

#include <cppstream/Collector.h>
#include <cppstream/Comparator.h>
#include <cppstream/Gatherer.h>
#include <cppstream/Optional.h>
#include <cppstream/Parallel.h>
#include <cppstream/RuntimeException.h>
#include <cppstream/cppstream_fwd.h>

#include <algorithm>
#include <atomic>
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

    /// An upper bound on how many elements the stage upstream of this one can still
    /// produce, or nullopt when that is not known.
    ///
    /// This is *not* SizeHint. SizeHint is the number of elements this stage is
    /// expected to yield, so filter() and flatMap() correctly drop it; Budget is the
    /// number of elements this stage is allowed to *pull*, so both of them keep it.
    /// That distinction is what lets a parallel stage batch its work: batching reads
    /// ahead, and reading ahead is only safe while the input is known to end.
    ///
    /// Every producer of a Budget guarantees two things: it is never an
    /// underestimate (so hitting zero means the upstream really is exhausted), and
    /// no stage ever raises it. Streams whose source cannot be bounded -- iterate,
    /// generate, and anything downstream of a flatMap -- carry nullopt, and the
    /// parallel stages then run one element at a time. See DESIGN.md divergence 8.
    using Budget = std::optional<std::size_t>;

    /// `parallel` is the flag set by parallel(); `budget` is a live upper bound on
    /// how many elements this stage's upstream can still produce (see Budget).
    /// Both are carried by every stage so that a pipeline built from parallel()
    /// onward knows whether it may batch, and how far ahead it may read.
    ///
    /// sizeHint and budget are the same type and so are trivially swappable, which
    /// is why this is the one place clang-tidy's swappable-parameter check has to
    /// be silenced: every caller is in this file and passes both by name, and a
    /// mix-up is caught by the parallel tests rather than by the type system.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit Stream(NextFn next, SizeHint sizeHint = std::nullopt, Budget budget = std::nullopt,
                    bool parallel = false)
        : next_(std::move(next)), sizeHint_(sizeHint), budget_(budget), parallel_(parallel) {}

    Stream(Stream&&) noexcept = default;
    Stream& operator=(Stream&&) noexcept = default;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    ~Stream() = default;

    /// The size hint this stage was built with. See SizeHint.
    [[nodiscard]] SizeHint sizeHint() const noexcept { return sizeHint_; }

    /// The pull budget this stage was built with. See Budget.
    [[nodiscard]] Budget budget() const noexcept { return budget_; }

    // -----------------------------------------------------------------------
    // Intermediate operations
    // -----------------------------------------------------------------------

    /// Java: Stream.filter(predicate).
    template <class P>
    Stream<T> filter(P predicate) && {
        NextFn previous = takeNext();
        // filter is one-for-one or one-for-none, so the pull budget carries through
        // untouched. The size hint does not: how many elements survive is unknown.
        return Stream<T>(
            mapBatch<T>(std::move(previous), batchingBudget(), parallelChunk,
                        [predicate = std::move(predicate)](T& value) -> std::optional<T> {
                            if (std::invoke(predicate, value)) {
                                return std::optional<T>(std::in_place, std::move(value));
                            }
                            return std::nullopt;
                        }),
            std::nullopt, budget_, parallel_);
    }

    /// Java: Stream.map(mapper).
    template <class F>
        requires(!std::is_void_v<std::invoke_result_t<F&, T&>>)
    auto map(F mapper) && -> Stream<std::remove_cvref_t<std::invoke_result_t<F&, T&>>> {
        using R = std::remove_cvref_t<std::invoke_result_t<F&, T&>>;
        NextFn previous = takeNext();
        // map is one-for-one, so whatever the source promised still holds.
        const SizeHint promised = sizeHint_;
        return Stream<R>(Stream<T>::template mapBatch<R>(
                             std::move(previous), batchingBudget(), parallelChunk,
                             [mapper = std::move(mapper)](T& value) -> std::optional<R> {
                                 return std::optional<R>(std::in_place, std::invoke(mapper, value));
                             }),
                         promised, budget_, parallel_);
    }

    /// Java: Stream.flatMap(mapper), where the mapper returns a Stream per
    /// element. Each inner stream is drained before the next outer element is
    /// pulled, so an infinite outer stream works as long as inner streams end,
    /// and an inner stream may itself be infinite.
    ///
    /// When the stream is parallel the mappers run on the pool, one per outer
    /// element of a batch, but the inner streams are still drained lazily and in
    /// order -- only the outer batch is eager. An unbounded outer stream takes the
    /// one-element path and behaves exactly as it does sequentially.
    template <class F>
        requires(isStream<std::invoke_result_t<F&, T&>> &&
                 !std::is_reference_v<std::invoke_result_t<F&, T&>>)
    auto flatMap(F mapper) && -> std::remove_cvref_t<std::invoke_result_t<F&, T&>> {
        using Inner = std::remove_cvref_t<std::invoke_result_t<F&, T&>>;

        NextFn previous = takeNext();
        return Inner(Stream<T>::template flatBatch<Inner>(
                         std::move(previous), batchingBudget(), parallelChunk,
                         [mapper = std::move(mapper)](T& value) -> Inner {
                             return std::invoke(mapper, value);
                         }),
                     std::nullopt, std::nullopt, parallel_);
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
    ///
    /// It inherits gather()'s barrier behaviour: parallel() does not speed up
    /// mapMulti itself, though it still applies to the stages after it.
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
    ///
    /// A gatherer may emit any number of elements per input, so this stage is a
    /// barrier: it integrates one element at a time even in a parallel pipeline, and
    /// the stages after it carry no pull budget again.
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

        return Stream<R>(
            typename Stream<R>::NextFn([previous = std::move(previous),
                                        gatherer = std::move(gatherer),
                                        accumulated = std::move(accumulated),
                                        downstream = std::move(downstream), pending,
                                        done = false] mutable -> std::optional<R> {
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
            }),
            std::nullopt, std::nullopt, parallel_);
    }

    /// Java: Stream.peek(action). A pure side-effect hook; elements pass through
    /// untouched.
    ///
    /// In a parallel pipeline the action still runs once per element, but the
    /// actions of one batch overlap, so they no longer happen in encounter order.
    /// That is exactly Java's caveat for peek() on a parallel stream.
    template <class F>
    Stream<T> peek(F action) && {
        NextFn previous = takeNext();
        const SizeHint promised = sizeHint_;
        return Stream<T>(mapBatch<T>(std::move(previous), batchingBudget(), parallelChunk,
                                     [action = std::move(action)](T& value) -> std::optional<T> {
                                         std::invoke(action, value);
                                         return std::optional<T>(std::in_place, std::move(value));
                                     }),
                         promised, budget_, parallel_);
    }

    /// Java: Stream.distinct(). Keeps the first occurrence of each element and
    /// therefore preserves encounter order.
    ///
    /// Being stateful it is a barrier: batching cannot preserve "first occurrence"
    /// order without buffering the whole stream, so it pulls one element at a time
    /// even in a parallel pipeline. It still passes the parallel flag on, so the
    /// stages after it are batched as usual.
    Stream<T> distinct() && {
        NextFn previous = takeNext();
        return Stream<T>(NextFn([previous = std::move(previous),
                                 seen = std::unordered_set<T>{}] mutable -> std::optional<T> {
                             for (;;) {
                                 std::optional<T> value = previous();
                                 if (!value) {
                                     return std::nullopt;
                                 }
                                 if (seen.insert(*value).second) {
                                     return value;
                                 }
                             }
                         }),
                         std::nullopt, budget_, parallel_);
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
        return Stream<T>(
            NextFn([previous = std::move(previous), comparator, promised,
                    buffer = std::optional<std::vector<T>>{},
                    index = std::size_t{0}] mutable -> std::optional<T> {
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
            }),
            promised, budget_, parallel_);
    }

    /// Not a Java Stream method: mirrors SequencedCollection.reversed()
    /// (Java 21), which Java exposes on collections only. Buffers like sorted().
    Stream<T> reversed() && {
        NextFn previous = takeNext();
        const SizeHint promised = sizeHint_;
        return Stream<T>(NextFn([previous = std::move(previous), promised,
                                 buffer = std::optional<std::vector<T>>{},
                                 index = std::size_t{0}] mutable -> std::optional<T> {
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
                         }),
                         promised, budget_, parallel_);
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
                                 remaining = maxSize] mutable -> std::optional<T> {
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
                         }),
                         promised, limited(budget_, maxSize), parallel_);
    }

    /// Java: Stream.skip(n).
    Stream<T> skip(std::int64_t n) && {
        if (n < 0) {
            throw IllegalArgumentException("n must be non-negative: " + std::to_string(n));
        }
        NextFn previous = takeNext();
        const SizeHint promised = skipped(sizeHint_, n);
        return Stream<T>(
            NextFn([previous = std::move(previous), remaining = n] mutable -> std::optional<T> {
                while (remaining > 0) {
                    if (!previous()) {
                        remaining = 0;
                        return std::nullopt;
                    }
                    --remaining;
                }
                return previous();
            }),
            promised, skipped(budget_, n), parallel_);
    }

    /// Java 9: Stream.takeWhile(predicate).
    template <class P>
    Stream<T> takeWhile(P predicate) && {
        NextFn previous = takeNext();
        return Stream<T>(NextFn([previous = std::move(previous), predicate = std::move(predicate),
                                 stopped = false] mutable -> std::optional<T> {
                             if (stopped) {
                                 return std::nullopt;
                             }
                             std::optional<T> value = previous();
                             if (!value || !std::invoke(predicate, *value)) {
                                 stopped = true;
                                 return std::nullopt;
                             }
                             return value;
                         }),
                         std::nullopt, budget_, parallel_);
    }

    /// Java 9: Stream.dropWhile(predicate).
    template <class P>
    Stream<T> dropWhile(P predicate) && {
        NextFn previous = takeNext();
        return Stream<T>(NextFn([previous = std::move(previous), predicate = std::move(predicate),
                                 dropping = true] mutable -> std::optional<T> {
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
                         }),
                         std::nullopt, budget_, parallel_);
    }

    /// Java: Stream.unordered(). A no-op here, and permitted to be one: Java
    /// documents it as a hint that lets the implementation drop encounter order,
    /// and this implementation preserves encounter order everywhere -- batched
    /// parallel stages included -- so there is nothing to drop.
    Stream<T> unordered() && { return std::move(*this); }

    // -----------------------------------------------------------------------
    // Terminal operations
    // -----------------------------------------------------------------------

    /// Java: Stream.forEach(action). The action receives a mutable reference.
    ///
    /// In a parallel pipeline each batch's actions run on the pool, so the actions
    /// of one batch overlap and no longer finish in encounter order. Elements still
    /// *arrive* in encounter order -- that is this library's guarantee, see
    /// divergence 8 -- but an action with side effects must be thread-safe, which is
    /// Java's caveat for a parallel forEach too.
    template <class F>
    void forEach(F action) && {
        NextFn next = takeNext();
        if (!canBatchTerminal()) {
            while (std::optional<T> value = next()) {
                std::invoke(action, *value);
            }
            return;
        }
        Budget remaining = budget_;
        std::vector<T> batch;
        for (;;) {
            if (!fillBatch(next, batchRequest(*remaining, parallelChunk * parallelism()), batch)) {
                return;
            }
            chargeBudget(remaining, batch.size());
            parallelPool().forEach(batch.size(), [&batch, &action](std::size_t index) {
                std::invoke(action, batch[index]);
            });
        }
    }

    /// Java: Stream.forEachOrdered(action).
    ///
    /// Stays sequential even in a parallel pipeline, because the order *is* the
    /// operation: element n's action has to finish before element n + 1's starts.
    /// The stages upstream still batch, so this is a correctness guarantee rather
    /// than a lost optimisation.
    template <class F>
    void forEachOrdered(F action) && {
        NextFn next = takeNext();
        while (std::optional<T> value = next()) {
            std::invoke(action, *value);
        }
    }

    /// Java: Stream.count().
    ///
    /// Counting itself is not worth parallelising: the work is in the stages
    /// upstream, and those batch regardless of what is pulling them.
    [[nodiscard]] std::int64_t count() && {
        NextFn next = takeNext();
        std::int64_t total = 0;
        while (next()) {
            ++total;
        }
        return total;
    }

    /// Java: Stream.toArray().
    ///
    /// Like count(), this pulls sequentially and lets the batched stages upstream
    /// do the parallel work.
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
    /// combiner is never called in a sequential pipeline, because there is nothing
    /// to merge -- it exists for parallel streams and for the collectors that
    /// partitioningBy/groupingBy build out of it.
    ///
    /// When the stream is parallel and the upstream size is known, a batch is split
    /// into one slice per worker, each slice accumulates into its own accumulator,
    /// and the slices are merged pairwise *in encounter order*. That is what makes
    /// order-sensitive collectors -- toList, joining, groupingBy -- return exactly
    /// the sequential answer, and it is the one place divergence 8's "the combiner
    /// is a hard requirement" comes true.
    ///
    /// An unbounded upstream keeps the sequential path, so an infinite stream is
    /// never drained into partial accumulators.
    ///
    /// Constrained by CollectorLike so that a mistyped collector fails here, at
    /// the call site, rather than somewhere inside the pipeline's type erasure.
    template <class C>
        requires CollectorLike<C, T>
    [[nodiscard]] auto collect(const C& collector) && -> C::resultType {
        NextFn next = takeNext();
        using Accumulator = C::accumulatorType;
        const typename C::Supplier& supplier = collector.supplier();
        const typename C::Accumulator& accumulate = collector.accumulator();
        if (!canBatchTerminal()) {
            Accumulator accumulator = std::invoke(supplier);
            while (std::optional<T> value = next()) {
                std::invoke(accumulate, accumulator, *value);
            }
            return std::invoke(collector.finisher(), std::move(accumulator));
        }
        Accumulator merged = std::invoke(supplier);
        runParallelReduce<Accumulator>(next, budget_, supplier, accumulate, collector.combiner(),
                                       merged);
        return std::invoke(collector.finisher(), std::move(merged));
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
    ///
    /// Expressed as reduce(0, +), which is what makes it batched in a parallel
    /// pipeline. Addition is associative for integers and not for floating point, so
    /// a parallel sum of doubles may differ in the last bit from the sequential one
    /// -- Java's DoubleStream.sum() has the same property.
    [[nodiscard]] T sum() && requires(std::integral<T> || std::floating_point<T>) {
        return std::move(*this).reduce(T{}, [](T left, T right) { return left + right; });
    }

    /// Java: IntStream/LongStream/DoubleStream.average(). Always returns double,
    /// exactly as Java's IntStream.average() returns OptionalDouble.
    ///
    /// Left sequential: a parallel average has to merge (sum, count) pairs, and the
    /// double rounding that would introduce is not worth it for an operation whose
    /// per-element cost is one addition. Batched stages upstream are unaffected.
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
    ///
    /// Stays a one-element pull even when the stream is parallel. The batched stages
    /// it pulls from may have computed a whole batch by then, but this terminal never
    /// asks for one, so `findFirst()` on a bounded parallel stream stops after the
    /// first batch rather than draining the source.
    [[nodiscard]] Optional<T> findFirst() && {
        NextFn next = takeNext();
        std::optional<T> value = next();
        if (!value) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(std::move(*value));
    }

    /// Java: Stream.findAny(). This implementation always preserves encounter order,
    /// parallel or not, so it is findFirst() -- a stronger guarantee than Java makes
    /// (which is allowed to return any element), never a weaker one.
    [[nodiscard]] Optional<T> findAny() && { return std::move(*this).findFirst(); }

    /// Java: Stream.anyMatch(predicate). Short-circuits on the first match, and in a
    /// parallel pipeline once one batch has found a witness the remaining batches are
    /// never pulled.
    template <class P>
    [[nodiscard]] bool anyMatch(P predicate) && {
        NextFn next = takeNext();
        if (!canBatchTerminal()) {
            while (std::optional<T> value = next()) {
                if (std::invoke(predicate, *value)) {
                    return true;
                }
            }
            return false;
        }
        return parallelWitness(next, budget_, predicate);
    }

    /// Java: Stream.allMatch(predicate).
    ///
    /// allMatch(p) is noneMatch(not p), which is also how a parallel pipeline gets
    /// to short-circuit on the first counterexample.
    template <class P>
    [[nodiscard]] bool allMatch(P predicate) && {
        NextFn next = takeNext();
        if (!canBatchTerminal()) {
            while (std::optional<T> value = next()) {
                if (!std::invoke(predicate, *value)) {
                    return false;
                }
            }
            return true;
        }
        return !parallelWitness(next, budget_,
                                [&predicate](T& value) { return !std::invoke(predicate, value); });
    }

    /// Java: Stream.noneMatch(predicate).
    template <class P>
    [[nodiscard]] bool noneMatch(P predicate) && {
        NextFn next = takeNext();
        if (!canBatchTerminal()) {
            while (std::optional<T> value = next()) {
                if (std::invoke(predicate, *value)) {
                    return false;
                }
            }
            return true;
        }
        return !parallelWitness(next, budget_, predicate);
    }

    /// Java: Stream.reduce(accumulator). Empty in, empty out.
    ///
    /// A parallel reduction folds each slice independently and merges the slices in
    /// encounter order, which is only guaranteed to agree with the sequential answer
    /// when `accumulator` is associative and stateless. Subtracting, say, would give
    /// a different wrong-by-design answer here than sequentially -- exactly the
    /// contract Java documents for a parallel Stream.reduce.
    template <class F>
    [[nodiscard]] Optional<T> reduce(F accumulator) && {
        NextFn next = takeNext();
        if (!canBatchTerminal()) {
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
        return reduceParallelElements(next, budget_, accumulator);
    }

    /// Java: Stream.reduce(identity, accumulator).
    ///
    /// The elements are reduced on their own and `identity` is folded in front of the
    /// result, rather than seeding every slice with it: that is the same value for an
    /// associative accumulator (which is what a parallel reduction requires anyway)
    /// and it does not force T to be copy-constructible.
    template <class F>
    [[nodiscard]] T reduce(T identity, F accumulator) && {
        NextFn next = takeNext();
        if (!canBatchTerminal()) {
            T result = std::move(identity);
            while (std::optional<T> value = next()) {
                result = std::invoke(accumulator, std::move(result), std::move(*value));
            }
            return result;
        }
        Optional<T> reduced = reduceParallelElements(next, budget_, accumulator);
        if (reduced.isEmpty()) {
            return std::move(identity);
        }
        return std::invoke(accumulator, std::move(identity), std::move(reduced.get()));
    }

    /// Java: Stream.min(comparator). Ties keep the first minimal element, in a
    /// parallel pipeline as well: the merge prefers its left-hand side.
    [[nodiscard]] Optional<T> min(const Comparator<T>& comparator) && {
        NextFn next = takeNext();
        if (!canBatchTerminal()) {
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
        Optional<T> merged = Optional<T>::empty();
        runParallelReduce<Optional<T>>(
            next, budget_, [] { return Optional<T>::empty(); },
            [&comparator](Optional<T>& run, T& element) {
                if (run.isEmpty() || comparator.compare(element, run.get()) < 0) {
                    run = Optional<T>::of(std::move(element));
                }
            },
            [&comparator](Optional<T>&& left, Optional<T>&& right) -> Optional<T> {
                if (left.isEmpty()) {
                    return std::move(right);
                }
                if (right.isEmpty()) {
                    return std::move(left);
                }
                return comparator.compare(right.get(), left.get()) < 0 ? std::move(right)
                                                                       : std::move(left);
            },
            merged);
        return merged;
    }

    /// Java: Stream.max(comparator). Ties keep the first maximal element.
    [[nodiscard]] Optional<T> max(const Comparator<T>& comparator) && {
        NextFn next = takeNext();
        if (!canBatchTerminal()) {
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
        Optional<T> merged = Optional<T>::empty();
        runParallelReduce<Optional<T>>(
            next, budget_, [] { return Optional<T>::empty(); },
            [&comparator](Optional<T>& run, T& element) {
                if (run.isEmpty() || comparator.compare(element, run.get()) > 0) {
                    run = Optional<T>::of(std::move(element));
                }
            },
            [&comparator](Optional<T>&& left, Optional<T>&& right) -> Optional<T> {
                if (left.isEmpty()) {
                    return std::move(right);
                }
                if (right.isEmpty()) {
                    return std::move(left);
                }
                return comparator.compare(right.get(), left.get()) > 0 ? std::move(right)
                                                                       : std::move(left);
            },
            merged);
        return merged;
    }

    // -----------------------------------------------------------------------
    // Parallelism
    // -----------------------------------------------------------------------

    /// Java: Stream.parallel().
    ///
    /// Divergence 8: parallel() applies from the call onwards, not to the whole
    /// pipeline. Stages are already built by the time it is reached, and a pull
    /// stage can only parallelise the work it does itself, so nothing upstream of
    /// this point changes. The idiomatic spellings -- `collection.parallelStream()`
    /// and `Stream.of(...).parallel().map(...)` -- put the flag before the heavy
    /// stages, so ported code lands on the parallel path either way.
    ///
    /// What becomes parallel: map, filter and peek batch their elements and run
    /// them on the shared pool, and so do collect, reduce, min, max, sum, forEach
    /// and the three match terminals. What does not: distinct, sorted, reversed,
    /// flatMap's inner streams, mapMulti and gather, which are stateful or 1:N and
    /// therefore keep a sequential barrier. See DESIGN.md divergence 8 and section
    /// 7.10 for the full table.
    Stream<T> parallel() && { return Stream<T>(takeNext(), sizeHint_, budget_, true); }

    /// Java: Stream.sequential(). Turns batching off again from this point on;
    /// the stages already built keep whatever they were built with.
    Stream<T> sequential() && { return Stream<T>(takeNext(), sizeHint_, budget_, false); }

    /// Java: Stream.isParallel().
    [[nodiscard]] bool isParallel() const noexcept { return parallel_; }

    // -----------------------------------------------------------------------
    // Factories. Java hangs these off Stream / IntStream static methods; in C++
    // the element type comes from the class template argument.
    // -----------------------------------------------------------------------

    /// Java: Stream.empty().
    static Stream<T> empty() {
        return Stream<T>(NextFn([] -> std::optional<T> { return std::nullopt; }), std::size_t{0},
                         std::size_t{0});
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
                                 index = std::size_t{0}] mutable -> std::optional<T> {
                             if (index >= values.size()) {
                                 return std::nullopt;
                             }
                             return std::optional<T>(std::in_place, std::move(values[index++]));
                         }),
                         promised, promised);
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
        const SizeHint promised = hintFor(range);
        return Stream<R>(typename Stream<R>::NextFn(
                             [it = std::ranges::begin(range),
                              last = std::ranges::end(range)] mutable -> std::optional<R> {
                                 if (it == last) {
                                     return std::nullopt;
                                 }
                                 R value = *it;
                                 ++it;
                                 return std::optional<R>(std::in_place, std::move(value));
                             }),
                         promised, promised);
    }

    template <std::ranges::input_range Rg>
        requires(!std::is_lvalue_reference_v<Rg>)
    static Stream<std::ranges::range_value_t<Rg>> ofRange(Rg&& range) {
        using R = std::ranges::range_value_t<Rg>;
        using Cursor = std::ranges::iterator_t<Rg>;
        const SizeHint promised = hintFor(range);
        return Stream<R>(typename Stream<R>::NextFn(
                             [owned = std::forward<Rg>(range),
                              cursor = std::optional<Cursor>{}] mutable -> std::optional<R> {
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
                             }),
                         promised, promised);
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
                                 next = std::move(next)] mutable -> std::optional<T> {
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
                                 next = std::move(next)] mutable -> std::optional<T> {
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
        return Stream<T>(NextFn([supplier = std::move(supplier)] mutable -> std::optional<T> {
            return std::optional<T>(std::in_place, std::invoke(supplier));
        }));
    }

    /// Java: Stream.concat(first, second). Lazy: second is untouched until first
    /// is exhausted, so an infinite first stream is fine on its own.
    static Stream<T> concat(Stream<T> first, Stream<T> second) {
        const SizeHint promised = combined(first.sizeHint(), second.sizeHint());
        const Budget budget = combined(first.budget(), second.budget());
        NextFn firstNext = std::move(first).takeNext();
        NextFn secondNext = std::move(second).takeNext();
        return Stream<T>(
            NextFn([firstNext = std::move(firstNext), secondNext = std::move(secondNext),
                    drainingFirst = true] mutable -> std::optional<T> {
                if (drainingFirst) {
                    std::optional<T> value = firstNext();
                    if (value) {
                        return value;
                    }
                    drainingFirst = false;
                }
                return secondNext();
            }),
            promised, budget);
    }

    /// Java: IntStream.range(startInclusive, endExclusive), generalised over any
    /// integral T. An empty stream results when start >= end.
    static Stream<T> range(T startInclusive, T endExclusive)
        requires std::integral<T>
    {
        const SizeHint promised = spanHint(startInclusive, endExclusive, 0);
        return Stream<T>(
            NextFn([current = startInclusive, end = endExclusive] mutable -> std::optional<T> {
                if (current >= end) {
                    return std::nullopt;
                }
                return std::optional<T>(std::in_place, current++);
            }),
            promised, promised);
    }

    /// Java: IntStream.rangeClosed(startInclusive, endInclusive). Uses an
    /// exhausted sentinel rather than comparing after an increment, so it also
    /// terminates when endInclusive is the maximum value of T.
    static Stream<T> rangeClosed(T startInclusive, T endInclusive)
        requires std::integral<T>
    {
        const SizeHint promised = spanHint(startInclusive, endInclusive, 1);
        return Stream<T>(NextFn([current = startInclusive, end = endInclusive,
                                 exhausted = false] mutable -> std::optional<T> {
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
                         }),
                         promised, promised);
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
        const auto dropped = static_cast<std::size_t>(n);
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

    // -----------------------------------------------------------------------
    // Parallel helpers
    // -----------------------------------------------------------------------

    /// How many elements a batched stage pulls before handing them to the pool.
    /// Large enough to amortise a task hand-off, small enough that a
    /// short-circuiting terminal does not overshoot by much. Terminal operations
    /// multiply it by the worker count so that every worker gets a slice.
    static constexpr std::size_t parallelChunk = 1024;

    /// Whether this stream's parallel flag should be honoured at all. A pool of one
    /// -- or a caller who capped parallelism at 1 -- means everything runs
    /// sequentially, and the results are exactly the sequential ones.
    [[nodiscard]] bool canParallelize() const noexcept { return parallel_ && parallelism() > 1; }

    /// The pull budget a batched stage may use. Nullopt means "run one element at a
    /// time", which is what both a sequential stage and an unbounded source want.
    [[nodiscard]] Budget batchingBudget() const noexcept {
        return canParallelize() ? budget_ : Budget{};
    }

    /// The next batch may never ask for more than the chunk, never for zero, and
    /// never for more than the budget says is left.
    [[nodiscard]] static std::size_t batchRequest(std::size_t remaining, std::size_t chunk) {
        return std::min(chunk, std::max<std::size_t>(remaining, 1));
    }

    /// Fills `inputs` with up to `request` elements, clearing it first, and reports
    /// whether anything was pulled.
    [[nodiscard]] static bool fillBatch(NextFn& previous, std::size_t request,
                                        std::vector<T>& inputs) {
        inputs.clear();
        inputs.reserve(request);
        while (inputs.size() < request) {
            std::optional<T> value = previous();
            if (!value) {
                break;
            }
            inputs.push_back(std::move(*value));
        }
        return !inputs.empty();
    }

    /// Lowers a live budget by what was just pulled. The guarantee is an upper
    /// bound, so this never clamps on a correct estimate; the clamp is there so that
    /// an over-eager estimate cannot wrap around and turn into a huge one.
    static void chargeBudget(Budget& budget, std::size_t pulled) noexcept {
        if (budget) {
            *budget = *budget > pulled ? *budget - pulled : std::size_t{0};
        }
    }

    /// Wraps `previous` so that elements are pulled in batches and `step` is applied
    /// to every element of a batch on the pool before the first of them is handed
    /// downstream. Slot i always holds the result of the batch's i-th element, so
    /// encounter order survives batching.
    ///
    /// `step` maps one element to one optional result and nullopt drops it, which is
    /// how filter is spelled. `budget` is the live upper bound on the upstream; with
    /// a nullopt budget the stage runs one element at a time, exactly as the
    /// sequential stage does, because a parallel pipeline must never prefetch past a
    /// size it cannot bound -- findFirst() on an infinite stream has to stop.
    template <class R, class Step>
    [[nodiscard]] static Stream<R>::NextFn mapBatch(NextFn previous, Budget budget,
                                                    std::size_t chunk, Step step) {
        return typename Stream<R>::NextFn([previous = std::move(previous), step = std::move(step),
                                           budget, chunk, buffer = std::vector<std::optional<R>>{},
                                           cursor = std::size_t{0}] mutable -> std::optional<R> {
            for (;;) {
                if (cursor < buffer.size()) {
                    std::optional<R> result = std::move(buffer[cursor]);
                    ++cursor;
                    if (result) {
                        return result;
                    }
                    continue;
                }
                buffer.clear();
                cursor = 0;
                if (!budget) {
                    std::optional<T> value = previous();
                    if (!value) {
                        return std::nullopt;
                    }
                    std::optional<R> result = std::invoke(step, *value);
                    if (result) {
                        return result;
                    }
                    // The step dropped this element (filter's "one for none"); keep
                    // pulling rather than ending the stream.
                    continue;
                }
                std::vector<T> inputs;
                if (!fillBatch(previous, batchRequest(*budget, chunk), inputs)) {
                    return std::nullopt;
                }
                chargeBudget(budget, inputs.size());
                buffer.resize(inputs.size());
                if (inputs.size() == 1) {
                    buffer[0] = std::invoke(step, inputs[0]);
                } else {
                    parallelPool().forEach(inputs.size(),
                                           [&inputs, &buffer, &step](std::size_t index) {
                                               buffer[index] = std::invoke(step, inputs[index]);
                                           });
                }
            }
        });
    }

    /// The 1:N sibling of mapBatch, used by flatMap: `step` turns one element into a
    /// whole inner stream, and the stage drains those inner streams in encounter
    /// order. Only the outer batch is eager -- the inner streams are still pulled
    /// lazily, so an inner stream may be infinite and a batch's inner streams need
    /// never all be held in memory at once.
    template <class Inner, class Step>
    [[nodiscard]] static Inner::NextFn flatBatch(NextFn previous, Budget budget, std::size_t chunk,
                                                 Step step) {
        using R = Inner::valueType;
        return typename Inner::NextFn([previous = std::move(previous), step = std::move(step),
                                       budget, chunk, inners = std::vector<std::optional<Inner>>{},
                                       innerNext = std::optional<typename Inner::NextFn>{},
                                       outer = std::size_t{0}] mutable -> std::optional<R> {
            for (;;) {
                if (innerNext) {
                    std::optional<R> value = (*innerNext)();
                    if (value) {
                        return value;
                    }
                    innerNext.reset();
                }
                if (outer < inners.size()) {
                    innerNext = std::move(*inners[outer]).takeNext();
                    ++outer;
                    continue;
                }
                inners.clear();
                outer = 0;
                if (!budget) {
                    std::optional<T> value = previous();
                    if (!value) {
                        return std::nullopt;
                    }
                    inners.push_back(std::invoke(step, *value));
                    continue;
                }
                std::vector<T> inputs;
                if (!fillBatch(previous, batchRequest(*budget, chunk), inputs)) {
                    return std::nullopt;
                }
                chargeBudget(budget, inputs.size());
                inners.resize(inputs.size());
                if (inputs.size() == 1) {
                    inners[0].emplace(std::invoke(step, inputs[0]));
                } else {
                    parallelPool().forEach(
                        inputs.size(), [&inputs, &inners, &step](std::size_t index) {
                            inners[index].emplace(std::invoke(step, inputs[index]));
                        });
                }
            }
        });
    }

    /// The shared body of the parallel reduce-family terminals: pull a batch, fold
    /// each of its slices into its own accumulator on the pool, then merge the
    /// slices into `merged` in encounter order. `seed` builds one accumulator,
    /// `accumulate` folds one element into it and `combine` merges two accumulators
    /// -- the three functions a Collector exposes, which is why collect() is
    /// expressed through the same routine.
    ///
    /// Merging in encounter order is what makes order-sensitive collectors
    /// (toList, joining, groupingBy) produce the sequential answer; it is also why
    /// `combine` must be associative for the arithmetic ones, exactly as Java
    /// requires of a parallel reduction.
    template <class A, class Seed, class Accumulate, class Combine>
    static void runParallelReduce(NextFn& next, Budget remaining, const Seed& seed,
                                  const Accumulate& accumulate, const Combine& combine, A& merged) {
        std::vector<T> batch;
        std::vector<std::optional<A>> slices;
        for (;;) {
            const std::size_t request = batchRequest(*remaining, parallelChunk * parallelism());
            if (!fillBatch(next, request, batch)) {
                return;
            }
            chargeBudget(remaining, batch.size());
            const std::size_t workerCount = std::min<std::size_t>(parallelism(), batch.size());
            const std::size_t sliceSize = (batch.size() + workerCount - 1) / workerCount;
            const std::size_t sliceCount = (batch.size() + sliceSize - 1) / sliceSize;
            slices.clear();
            slices.resize(sliceCount);
            parallelPool().forEach(sliceCount, [&](std::size_t index) {
                const std::size_t begin = index * sliceSize;
                const std::size_t end = std::min(begin + sliceSize, batch.size());
                A local = std::invoke(seed);
                for (std::size_t position = begin; position < end; ++position) {
                    std::invoke(accumulate, local, batch[position]);
                }
                slices[index].emplace(std::move(local));
            });
            for (std::optional<A>& slice : slices) {
                merged = std::invoke(combine, std::move(merged), std::move(*slice));
            }
        }
    }

    /// True when a terminal operation may batch, which needs the same two things a
    /// stage does: the parallel flag and a bound on the upstream.
    [[nodiscard]] bool canBatchTerminal() const noexcept {
        return canParallelize() && budget_.has_value();
    }

    /// True when any element satisfies `witness`, computed a batch at a time. Once a
    /// batch has produced a witness the answer is known and the remaining batches are
    /// never pulled, which is how the three match terminals keep short-circuiting in
    /// a parallel pipeline. The flag is a plain relaxed atomic: workers only ever
    /// look at it to skip work, so a torn read is not possible and a late one only
    /// costs a little redundant work.
    template <class Witness>
    [[nodiscard]] static bool parallelWitness(NextFn& next, Budget remaining, Witness witness) {
        std::atomic<bool> found{false};
        std::vector<T> batch;
        for (;;) {
            if (!fillBatch(next, batchRequest(*remaining, parallelChunk * parallelism()), batch)) {
                return false;
            }
            chargeBudget(remaining, batch.size());
            parallelPool().forEach(batch.size(), [&batch, &witness, &found](std::size_t index) {
                if (!found.load(std::memory_order_relaxed) && std::invoke(witness, batch[index])) {
                    found.store(true, std::memory_order_relaxed);
                }
            });
            if (found.load(std::memory_order_relaxed)) {
                return true;
            }
        }
    }

    /// The element-only half of reduce(): folds the elements into an Optional, in
    /// encounter order and without an identity. reduce(accumulator) returns it as is;
    /// reduce(identity, accumulator) folds the identity in front of it.
    template <class F>
    [[nodiscard]] static Optional<T> reduceParallelElements(NextFn& next, Budget remaining,
                                                            F& accumulator) {
        Optional<T> merged = Optional<T>::empty();
        runParallelReduce<Optional<T>>(
            next, remaining, [] { return Optional<T>::empty(); },
            [&accumulator](Optional<T>& run, T& element) {
                if (run.isEmpty()) {
                    run = Optional<T>::of(std::move(element));
                } else {
                    run = Optional<T>::of(
                        std::invoke(accumulator, std::move(run.get()), std::move(element)));
                }
            },
            [&accumulator](Optional<T>&& left, Optional<T>&& right) -> Optional<T> {
                if (left.isEmpty()) {
                    return std::move(right);
                }
                if (right.isEmpty()) {
                    return std::move(left);
                }
                return Optional<T>::of(
                    std::invoke(accumulator, std::move(left.get()), std::move(right.get())));
            },
            merged);
        return merged;
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

    /// See Budget: a live upper bound on what this stage's upstream can still
    /// produce, decremented by the stage that does the pulling. Nullopt means the
    /// upstream may be infinite, which switches the stage to one element at a time.
    Budget budget_;

    /// Set by parallel() and carried by every stage built from it onward.
    bool parallel_ = false;

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
