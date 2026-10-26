#pragma once

#include <cppstream/RuntimeException.h>

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace cppstream {

/// The C++ stand-in for java.lang.Void: a type with exactly one value.
///
/// Java's Void has one value, null, and Gatherer uses it as the state type of a
/// gatherer that carries no state. std::monostate is the same idea with a usable
/// value, so `Void` is spelled as an alias rather than as a new type.
using Void = std::monostate;

/// A port of Gatherer.Downstream, the sink a gatherer pushes its results into.
///
/// Java's push() returns false once the downstream has stopped accepting elements
/// -- a short-circuiting operation such as limit() -- and isRejecting() lets a
/// gatherer notice without trying. This library's pipeline is pull-based, so there
/// is no back pressure to report: push always succeeds and isRejecting is always
/// false. A gatherer that wants to stop early returns false from its integrator
/// instead, which is the same signal and is honoured immediately. See DESIGN.md
/// divergence 33.
template <class R>
class Downstream {
public:
    /// Takes the element by value: the gatherer is done with it either way.
    using Sink = std::function<bool(R)>;

    explicit Downstream(Sink sink) : sink_(std::move(sink)) {}

    /// Java: Downstream.push(element).
    bool push(const R& element) { return std::invoke(sink_, element); }
    bool push(R&& element) { return std::invoke(sink_, std::move(element)); }

    /// Java: Downstream.isRejecting().
    [[nodiscard]] bool isRejecting() const noexcept { return false; }

private:
    Sink sink_;
};

/// A port of java.util.stream.Gatherer.
///
/// A gatherer is the imperative counterpart of a Collector: where a Collector
/// says "how do I turn this whole stream into one value", a Gatherer says "what
/// do I do with each element, and what do I emit". One gatherer can emit zero,
/// one or many elements per input element, which is what makes windowing and
/// scanning expressible without materialising the stream.
///
/// Two differences from Java, both forced by the language:
///
///   * State type `A` is named at the call site rather than inferred from the
///     integrator's parameters, because C++ has no target typing: the return type
///     cannot tell `Gatherer<T, ?, R>` what R is. `Gatherers::of` and
///     `Gatherers::ofSequential` spell the Java factories out; see DESIGN.md
///     divergence 34.
///
/// `combiner` is present for API fidelity and for `andThen`, but the current
/// gather() bridge is a barrier: it integrates one element at a time even in a
/// parallel pipeline, so it never has two partial states to merge. The accessor
/// is useful to composed gatherers and to callers that manage their own parallel
/// execution.
template <class T, class A, class R>
class Gatherer {
public:
    using inputType = T;
    using stateType = A;
    using outputType = R;

    using Initializer = std::function<A()>;
    using Integrator = std::function<bool(A&, const T&, Downstream<R>&)>;
    using Combiner = std::function<A(A&&, A&&)>;
    using Finisher = std::function<void(A&, Downstream<R>&)>;

    /// Java: Gatherer.of(integrator), the stateless form. A is Void.
    template <class IntegratorFn>
        requires std::is_invocable_r_v<bool, IntegratorFn&, A&, const T&, Downstream<R>&>
    explicit Gatherer(IntegratorFn integrator) : integrator_(std::move(integrator)) {}

    /// Java: Gatherer.ofSequential(initializer, integrator).
    template <class InitializerFn, class IntegratorFn>
        requires std::is_invocable_r_v<A, InitializerFn&> &&
                     std::is_invocable_r_v<bool, IntegratorFn&, A&, const T&, Downstream<R>&>
    Gatherer(InitializerFn initializer, IntegratorFn integrator)
        : initializer_(std::move(initializer)), integrator_(std::move(integrator)) {}

    /// Java: Gatherer.of(initializer, integrator, combiner, finisher) minus the
    /// combiner. An empty finisher is the identity, so a gatherer that emits
    /// nothing at the end can simply omit it.
    template <class InitializerFn, class IntegratorFn, class FinisherFn>
        requires std::is_invocable_r_v<A, InitializerFn&> &&
                     std::is_invocable_r_v<bool, IntegratorFn&, A&, const T&, Downstream<R>&> &&
                     std::is_invocable_r_v<void, FinisherFn&, A&, Downstream<R>&>
    Gatherer(InitializerFn initializer, IntegratorFn integrator, FinisherFn finisher)
        : initializer_(std::move(initializer)),
          integrator_(std::move(integrator)),
          finisher_(std::move(finisher)) {}

    /// Java: Gatherer.of(initializer, integrator, combiner, finisher).
    template <class InitializerFn, class IntegratorFn, class CombinerFn, class FinisherFn>
        requires std::is_invocable_r_v<A, InitializerFn&> &&
                     std::is_invocable_r_v<bool, IntegratorFn&, A&, const T&, Downstream<R>&> &&
                     std::is_invocable_r_v<A, CombinerFn&, A&&, A&&> &&
                     std::is_invocable_r_v<void, FinisherFn&, A&, Downstream<R>&>
    Gatherer(InitializerFn initializer, IntegratorFn integrator, CombinerFn combiner,
             FinisherFn finisher)
        : initializer_(std::move(initializer)),
          integrator_(std::move(integrator)),
          combiner_(std::move(combiner)),
          finisher_(std::move(finisher)) {}

    /// The state a fresh traversal starts from. Java's initializer() is never null
    /// there because the default is `() -> null`; here the default is a
    /// default-constructed A, which is why A must be default constructible.
    [[nodiscard]] A initialState() const {
        if (initializer_) {
            return std::invoke(initializer_);
        }
        return A{};
    }

    /// Java: Integrator.integrate. False means "consume no more input"; the
    /// finisher still runs, as it does in Java.
    bool integrate(A& state, const T& element, Downstream<R>& downstream) const {
        return std::invoke(integrator_, state, element, downstream);
    }

    /// Java: Gatherer.finisher(), folded with "there is no finisher".
    void finish(A& state, Downstream<R>& downstream) const {
        if (finisher_) {
            std::invoke(finisher_, state, downstream);
        }
    }

    /// Java: Gatherer.combiner(). Empty when this gatherer has no combiner.
    [[nodiscard]] const Combiner& combiner() const noexcept { return combiner_; }

    [[nodiscard]] bool hasCombiner() const noexcept { return static_cast<bool>(combiner_); }

    /// Java: Gatherer.combiner().apply(left, right).
    [[nodiscard]] A combine(A&& left, A&& right) const {
        if (!combiner_) {
            throw UnsupportedOperationException("this gatherer has no combiner");
        }
        return std::invoke(combiner_, std::move(left), std::move(right));
    }

    /// Java: Gatherer.andThen(next). The first gatherer's output becomes the
    /// second's input; both finishers run in order.
    template <class A2, class R2>
    [[nodiscard]] Gatherer<T, std::pair<A, A2>, R2> andThen(Gatherer<R, A2, R2> next) const {
        using State = std::pair<A, A2>;
        auto initializer = [self = *this, next] {
            return State(self.initialState(), next.initialState());
        };
        auto integrator = [self = *this, next](State& state, const T& element,
                                               Downstream<R2>& downstream) {
            Downstream<R> bridge([&state, &next, &downstream](R output) {
                return next.integrate(state.second, output, downstream);
            });
            return self.integrate(state.first, element, bridge);
        };
        auto finisher = [self = *this, next](State& state, Downstream<R2>& downstream) {
            Downstream<R> bridge([&state, &next, &downstream](R output) {
                return next.integrate(state.second, output, downstream);
            });
            self.finish(state.first, bridge);
            next.finish(state.second, downstream);
        };
        if (hasCombiner() && next.hasCombiner()) {
            return Gatherer<T, State, R2>(
                initializer, integrator,
                [self = *this, next](State&& left, State&& right) {
                    return State(self.combine(std::move(left.first), std::move(right.first)),
                                 next.combine(std::move(left.second), std::move(right.second)));
                },
                finisher);
        }
        return Gatherer<T, State, R2>(initializer, integrator, finisher);
    }

private:
    Initializer initializer_;
    Integrator integrator_;
    Combiner combiner_;
    Finisher finisher_;
};

}  // namespace cppstream
