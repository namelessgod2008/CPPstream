#pragma once

#include <cppstream/ArrayList.h>
#include <cppstream/Gatherer.h>
#include <cppstream/RuntimeException.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace cppstream {

/// A port of java.util.stream.Gatherers: the gatherers that ship with the JDK.
///
/// Java hangs these off the Gatherer interface as static methods; this library
/// keeps static factories on a plural utility class instead, the same way
/// List.of became Lists::of (divergence 6).
///
/// Missing: mapConcurrent, which is defined entirely in terms of a virtual
/// thread per element and has no meaning without one. Parallel streams are out of
/// scope (divergence 8), and a sequential mapConcurrent is just Stream::map.
class Gatherers {
public:

/// Java: Gatherers.windowFixed(windowSize), a non-overlapping partition.
///
/// The last window may be short: Java emits it from the finisher, and so does
/// this. Windowing is the canonical example of a gatherer that emits many
/// elements for many inputs and one more when the input ends.
template <class T>
[[nodiscard]] static Gatherer<T, std::vector<T>, ArrayList<T>> windowFixed(int windowSize)
{
    if (windowSize <= 0) {
        throw IllegalArgumentException(
            "windowSize must be greater than zero: " + std::to_string(windowSize));
    }
    return Gatherer<T, std::vector<T>, ArrayList<T>>(
        [] { return std::vector<T>{}; },
        [windowSize](std::vector<T>& window, const T& element, Downstream<ArrayList<T>>& downstream) {
            window.push_back(element);
            if (static_cast<int>(window.size()) == windowSize) {
                downstream.push(ArrayList<T>(std::move(window)));
                window.clear();
            }
            return true;
        },
        [](std::vector<T>& window, Downstream<ArrayList<T>>& downstream) {
            if (!window.empty()) {
                downstream.push(ArrayList<T>(std::move(window)));
            }
        });
}

/// Java: Gatherers.windowSliding(windowSize), a partition that overlaps by
/// windowSize - 1 elements.
///
/// Only complete windows are emitted: a stream shorter than windowSize produces
/// nothing, and the tail is dropped, exactly as in Java.
template <class T>
[[nodiscard]] static Gatherer<T, std::deque<T>, ArrayList<T>> windowSliding(int windowSize)
{
    if (windowSize <= 0) {
        throw IllegalArgumentException(
            "windowSize must be greater than zero: " + std::to_string(windowSize));
    }
    return Gatherer<T, std::deque<T>, ArrayList<T>>(
        [] { return std::deque<T>{}; },
        [windowSize](std::deque<T>& window, const T& element, Downstream<ArrayList<T>>& downstream) {
            window.push_back(element);
            if (static_cast<int>(window.size()) == windowSize) {
                downstream.push(ArrayList<T>(std::vector<T>(window.begin(), window.end())));
                window.pop_front();
            }
            return true;
        });
}

/// Java: Gatherers.fold(initial, folder). One element out, emitted by the
/// finisher, no matter how many went in -- including none.
template <class T, class R, class F>
[[nodiscard]] static Gatherer<T, R, R> fold(R initial, F folder)
{
    return Gatherer<T, R, R>(
        [initial = std::move(initial)] { return initial; },
        [folder = std::move(folder)](R& state, const T& element, Downstream<R>&) {
            state = std::invoke(folder, R(std::move(state)), element);
            return true;
        },
        [](R& state, Downstream<R>& downstream) { downstream.push(std::move(state)); });
}

/// Java: Gatherers.scan(initial, scanner). The prefix sums of a stream: the
/// initial value first, then one element per input.
///
/// On an empty stream the initial value is still emitted, which is what the
/// finisher is for here.
template <class T, class R, class F>
[[nodiscard]] static Gatherer<T, R, R> scan(R initial, F scanner)
{
    return Gatherer<T, R, R>(
        [initial = std::move(initial)] { return initial; },
        [scanner = std::move(scanner)](R& state, const T& element, Downstream<R>& downstream) {
            downstream.push(state);
            state = std::invoke(scanner, R(std::move(state)), element);
            return true;
        },
        [](R& state, Downstream<R>& downstream) { downstream.push(std::move(state)); });
}

/// Java: Gatherer.of(integrator), the stateless form.
///
/// T and R have to be given: Java infers them from the integrator's declared
/// type, C++ has nothing to infer them from (divergence 34).
template <class T, class R, class F>
[[nodiscard]] static Gatherer<T, Void, R> of(F integrator)
{
    return Gatherer<T, Void, R>(std::move(integrator));
}

/// Java: Gatherer.ofSequential(initializer, integrator).
template <class T, class A, class R, class InitializerFn, class IntegratorFn>
[[nodiscard]] static Gatherer<T, A, R> ofSequential(
    InitializerFn initializer, IntegratorFn integrator)
{
    return Gatherer<T, A, R>(std::move(initializer), std::move(integrator));
}

};

}  // namespace cppstream
