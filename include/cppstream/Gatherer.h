#pragma once

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
/// Three differences from Java, all forced by the language or by scope:
///
///   * No `combiner`. In Java it exists solely to merge partial states across a
///     parallel stream; parallel streams are out of scope here (divergence 8), so
///     it would be a required parameter that is never called.
///   * No `andThen`. In a sequential pipeline `s.gather(a.andThen(b))` and
///     `s.gather(a).gather(b)` are the same thing -- the second gatherer's input
///     is the first's output, and both finishers run in the same order -- so the
///     composition is already available at the pipeline level. Java needs it for
///     named, reusable gatherer constants.
///   * State type `A` is named at the call site rather than inferred from the
///     integrator's parameters, because C++ has no target typing: the return type
///     cannot tell `Gatherer<T, ?, R>` what R is. `Gatherers::of` and
///     `Gatherers::ofSequential` spell the Java factories out; see DESIGN.md
///     divergence 34.
template <class T, class A, class R>
class Gatherer {
public:
    using inputType = T;
    using stateType = A;
    using outputType = R;

    using Initializer = std::function<A()>;
    using Integrator = std::function<bool(A&, const T&, Downstream<R>&)>;
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
        : initializer_(std::move(initializer)), integrator_(std::move(integrator)),
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
    bool integrate(A& state, const T& element, Downstream<R>& downstream) {
        return std::invoke(integrator_, state, element, downstream);
    }

    /// Java: Gatherer.finisher(), folded with "there is no finisher".
    void finish(A& state, Downstream<R>& downstream) {
        if (finisher_) {
            std::invoke(finisher_, state, downstream);
        }
    }

private:
    Initializer initializer_;
    Integrator integrator_;
    Finisher finisher_;
};

}  // namespace cppstream
