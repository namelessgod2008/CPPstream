#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

namespace cppstream {

/// Java: java.util.stream.Collector.Characteristics.
///
/// `concurrent` and `unordered` are carried for source fidelity only: the library
/// has no parallel streams, and a sequential pipeline always preserves encounter
/// order. `identityFinish` is meaningful -- it says the finisher returns its
/// argument unchanged, so a caller may skip it and move the accumulator.
enum class Characteristics { concurrent, unordered, identityFinish };

/// A port of java.util.stream.Collector.
///
/// Unlike every other callable in the library, the four functions here are
/// std::function rather than std::move_only_function. The reason is that a
/// Collector is a *specification object*, not a pipeline stage: Java's
/// Collectors factories hand out stateless singletons and every combinator
/// (groupingBy, teeing, ...) holds its downstream collector by value. Copyable
/// collectors make those compositions plain value copies.
///
/// The asymmetry is deliberate: the pipeline is move-only because it owns a
/// single-use cursor, the collector is copyable because it owns nothing.
///
/// The accumulator takes `T&`, not `const T&`. Java's is
/// BiConsumer<A, super T> and receives the element object itself; a stage hands
/// out a freshly produced value that nothing else aliases, so an accumulator is
/// free to move out of it. Signature-wise `T&` also accepts a `const T&` lambda.
template <class T, class A, class R>
class Collector {
public:
    using elementType = T;
    using accumulatorType = A;
    using resultType = R;

    using Supplier = std::function<A()>;
    using Accumulator = std::function<void(A&, T&)>;
    using Combiner = std::function<A(A&&, A&&)>;
    using Finisher = std::function<R(A&&)>;

    Collector(Supplier supplier, Accumulator accumulator, Combiner combiner, Finisher finisher,
        std::vector<Characteristics> characteristics = {})
        : supplier_(std::move(supplier)),
          accumulator_(std::move(accumulator)),
          combiner_(std::move(combiner)),
          finisher_(std::move(finisher)),
          characteristics_(std::move(characteristics)) {}

    /// Java: Collector.of(...).
    ///
    /// Takes an initializer_list so that call sites read like Java's varargs form,
    /// then widens, because the combinators in Collectors.h forward the
    /// characteristics of the collector they wrap.
    [[nodiscard]] static Collector of(Supplier supplier, Accumulator accumulator,
        Combiner combiner, Finisher finisher,
        std::initializer_list<Characteristics> characteristics = {}) {
        return Collector(std::move(supplier), std::move(accumulator), std::move(combiner),
            std::move(finisher), std::vector<Characteristics>(characteristics));
    }

    [[nodiscard]] const Supplier& supplier() const noexcept { return supplier_; }
    [[nodiscard]] const Accumulator& accumulator() const noexcept { return accumulator_; }
    [[nodiscard]] const Combiner& combiner() const noexcept { return combiner_; }
    [[nodiscard]] const Finisher& finisher() const noexcept { return finisher_; }

    [[nodiscard]] const std::vector<Characteristics>& characteristics() const noexcept {
        return characteristics_;
    }

    /// Java: Collector.characteristics().contains(c).
    [[nodiscard]] bool has(Characteristics characteristic) const noexcept {
        for (Characteristics candidate : characteristics_) {
            if (candidate == characteristic) {
                return true;
            }
        }
        return false;
    }

private:
    Supplier supplier_;
    Accumulator accumulator_;
    Combiner combiner_;
    Finisher finisher_;
    std::vector<Characteristics> characteristics_;
};

/// Names the three nested types a Collector must expose.
template <class C>
concept CollectorType = requires {
    typename C::elementType;
    typename C::accumulatorType;
    typename C::resultType;
};

/// Constrains Stream::collect.
///
/// The conjunction short-circuits, so when C is not a collector at all the nested
/// types below are never formed and the compiler reports one readable error
/// instead of a page of template noise. That is what makes groupingBy usable.
template <class C, class T>
concept CollectorLike = CollectorType<C> && requires(const C& collector) {
    { std::invoke(collector.supplier()) } -> std::convertible_to<typename C::accumulatorType>;
    requires std::invocable<decltype(collector.accumulator()),
        typename C::accumulatorType&, T&>;
    requires std::invocable<decltype(collector.combiner()),
        typename C::accumulatorType, typename C::accumulatorType>;
    requires std::invocable<decltype(collector.finisher()),
        typename C::accumulatorType>;
} && std::same_as<typename C::elementType, T>;

}  // namespace cppstream
