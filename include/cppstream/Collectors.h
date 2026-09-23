#pragma once

#include <cppstream/ArrayList.h>
#include <cppstream/Collector.h>
#include <cppstream/Comparator.h>
#include <cppstream/HashMap.h>
#include <cppstream/HashSet.h>
#include <cppstream/Map.h>
#include <cppstream/Optional.h>
#include <cppstream/SummaryStatistics.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cppstream {

/// A port of java.util.stream.Collectors.
///
/// Java infers the element type from the target type of the expression
/// (`List<String> xs = stream.collect(toList())`). C++ has no target typing, so
/// every factory takes the element type as its first explicit template argument:
/// `stream.collect(Collectors::toList<std::string>())`. Where a factory also
/// needs a mapper's result type (groupingBy's key, toMap's key and value), that is
/// deduced from the mapper lambda exactly as Java would.
///
/// No `toUnmodifiable*` wrapper types exist: those collectors return the same
/// concrete containers, frozen (see AbstractCollection::freeze and divergence 26).
class Collectors {
public:
    /// The accumulator of the averaging collectors: a running sum and a count.
    /// Java hides this behind `Collector<?, ?, Double>`; C++ has to name it in
    /// the return type, so it is public but documented as an implementation detail.
    struct AveragingState {
        double sum = 0.0;
        std::int64_t count = 0;
    };

    /// The key type a mapper/classifier produces for an element of type T.
    template <class T, class F>
    using MapKeyOf = std::remove_cvref_t<std::invoke_result_t<F&, T&>>;

    /// The value type a mapper produces for an element of type T.
    template <class T, class F>
    using MapValueOf = std::remove_cvref_t<std::invoke_result_t<F&, T&>>;

    // ---------------------------------------------------------------------
    // Containers
    // ---------------------------------------------------------------------

    /// Java: Collectors.toList().
    template <class T>
    static Collector<T, ArrayList<T>, ArrayList<T>> toList() {
        return Collector<T, ArrayList<T>, ArrayList<T>>(
            [] { return ArrayList<T>(); },
            [](ArrayList<T>& accumulator, T& element) { accumulator.add(std::move(element)); },
            [](ArrayList<T>&& left, ArrayList<T>&& right) {
                left.addAll(right);
                return std::move(left);
            },
            [](ArrayList<T>&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.toUnmodifiableList(). The concrete list comes back frozen.
    template <class T>
    static Collector<T, ArrayList<T>, ArrayList<T>> toUnmodifiableList() {
        return Collector<T, ArrayList<T>, ArrayList<T>>(
            [] { return ArrayList<T>(); },
            [](ArrayList<T>& accumulator, T& element) { accumulator.add(std::move(element)); },
            [](ArrayList<T>&& left, ArrayList<T>&& right) {
                left.addAll(right);
                return std::move(left);
            },
            [](ArrayList<T>&& accumulator) {
                accumulator.freeze();
                return std::move(accumulator);
            });
    }

    /// Java: Collectors.toSet().
    template <class T>
    static Collector<T, HashSet<T>, HashSet<T>> toSet() {
        return Collector<T, HashSet<T>, HashSet<T>>(
            [] { return HashSet<T>(); },
            [](HashSet<T>& accumulator, T& element) { accumulator.add(std::move(element)); },
            [](HashSet<T>&& left, HashSet<T>&& right) {
                left.addAll(right);
                return std::move(left);
            },
            [](HashSet<T>&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.toUnmodifiableSet().
    template <class T>
    static Collector<T, HashSet<T>, HashSet<T>> toUnmodifiableSet() {
        return Collector<T, HashSet<T>, HashSet<T>>(
            [] { return HashSet<T>(); },
            [](HashSet<T>& accumulator, T& element) { accumulator.add(std::move(element)); },
            [](HashSet<T>&& left, HashSet<T>&& right) {
                left.addAll(right);
                return std::move(left);
            },
            [](HashSet<T>&& accumulator) {
                accumulator.freeze();
                return std::move(accumulator);
            });
    }

    /// Java: Collectors.toMap(keyMapper, valueMapper), which throws
    /// IllegalStateException on a duplicate key rather than silently overwriting.
    template <class T, class KF, class VF, class K = MapKeyOf<T, KF>, class U = MapValueOf<T, VF>>
    static Collector<T, HashMap<K, U>, HashMap<K, U>> toMap(KF keyMapper, VF valueMapper) {
        return Collector<T, HashMap<K, U>, HashMap<K, U>>(
            [] { return HashMap<K, U>(); },
            [keyMapper, valueMapper](HashMap<K, U>& accumulator, T& element) {
                K key = std::invoke(keyMapper, element);
                if (accumulator.containsKey(key)) {
                    throw IllegalStateException("Duplicate key");
                }
                accumulator.put(std::move(key), std::invoke(valueMapper, element));
            },
            [](HashMap<K, U>&& left, HashMap<K, U>&& right) {
                left.putAll(right);
                return std::move(left);
            },
            [](HashMap<K, U>&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.toMap(keyMapper, valueMapper, mergeFunction).
    template <class T, class KF, class VF, class MF, class K = MapKeyOf<T, KF>,
        class U = MapValueOf<T, VF>>
    static Collector<T, HashMap<K, U>, HashMap<K, U>> toMap(
        KF keyMapper, VF valueMapper, MF mergeFunction) {
        return Collector<T, HashMap<K, U>, HashMap<K, U>>(
            [] { return HashMap<K, U>(); },
            [keyMapper, valueMapper, mergeFunction](HashMap<K, U>& accumulator, T& element) {
                K key = std::invoke(keyMapper, element);
                U value = std::invoke(valueMapper, element);
                const U* existing = accumulator.get(key);
                if (existing == nullptr) {
                    accumulator.put(std::move(key), std::move(value));
                    return;
                }
                accumulator.put(key, std::invoke(mergeFunction, *existing, value));
            },
            [](HashMap<K, U>&& left, HashMap<K, U>&& right) {
                left.putAll(right);
                return std::move(left);
            },
            [](HashMap<K, U>&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.toUnmodifiableMap(keyMapper, valueMapper).
    template <class T, class KF, class VF, class K = MapKeyOf<T, KF>, class U = MapValueOf<T, VF>>
    static Collector<T, HashMap<K, U>, HashMap<K, U>> toUnmodifiableMap(KF keyMapper, VF valueMapper) {
        return Collector<T, HashMap<K, U>, HashMap<K, U>>(
            [] { return HashMap<K, U>(); },
            [keyMapper, valueMapper](HashMap<K, U>& accumulator, T& element) {
                K key = std::invoke(keyMapper, element);
                if (accumulator.containsKey(key)) {
                    throw IllegalStateException("Duplicate key");
                }
                accumulator.put(std::move(key), std::invoke(valueMapper, element));
            },
            [](HashMap<K, U>&& left, HashMap<K, U>&& right) {
                left.putAll(right);
                return std::move(left);
            },
            [](HashMap<K, U>&& accumulator) {
                accumulator.freeze();
                return std::move(accumulator);
            });
    }

    // ---------------------------------------------------------------------
    // Strings
    // ---------------------------------------------------------------------

    /// Java: Collectors.joining(). T must be appendable to std::string, which is
    /// Java's CharSequence bound spelled with the standard library.
    template <class T>
    static Collector<T, std::string, std::string> joining() {
        return Collector<T, std::string, std::string>(
            [] { return std::string(); },
            [](std::string& accumulator, T& element) { accumulator += element; },
            [](std::string&& left, std::string&& right) {
                left += right;
                return std::move(left);
            },
            [](std::string&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.joining(delimiter).
    template <class T>
    static Collector<T, std::string, std::string> joining(const std::string& delimiter) {
        return joining<T>(delimiter, std::string(), std::string());
    }

    /// Java: Collectors.joining(delimiter, prefix, suffix).
    ///
    /// The delimiter is written *before* each element and the leading one is
    /// erased at the end, which is how the "am I the first element?" test is
    /// avoided without a StringJoiner-shaped accumulator.
    template <class T>
    static Collector<T, std::string, std::string> joining(
        const std::string& delimiter, const std::string& prefix, const std::string& suffix) {
        return Collector<T, std::string, std::string>(
            [] { return std::string(); },
            [delimiter](std::string& accumulator, T& element) {
                accumulator += delimiter;
                accumulator += element;
            },
            [](std::string&& left, std::string&& right) {
                left += right;
                return std::move(left);
            },
            [delimiter, prefix, suffix](std::string&& accumulator) {
                std::string body = std::move(accumulator);
                if (!body.empty()) {
                    body.erase(0, delimiter.size());
                }
                return prefix + body + suffix;
            });
    }

    // ---------------------------------------------------------------------
    // Arithmetic
    // ---------------------------------------------------------------------

    /// Java: Collectors.counting().
    template <class T>
    static Collector<T, std::int64_t, std::int64_t> counting() {
        return Collector<T, std::int64_t, std::int64_t>(
            [] { return std::int64_t{0}; },
            [](std::int64_t& accumulator, T& /*element*/) { ++accumulator; },
            [](std::int64_t&& left, std::int64_t&& right) { return left + right; },
            [](std::int64_t&& accumulator) { return accumulator; },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.summingInt(mapper). Accumulates wide and narrows at the
    /// end, but still hands back an int, so the 32-bit wrap is observable exactly
    /// as Java's is.
    template <class T, class F>
    static Collector<T, std::int64_t, int> summingInt(F mapper) {
        return Collector<T, std::int64_t, int>(
            [] { return std::int64_t{0}; },
            [mapper](std::int64_t& accumulator, T& element) {
                accumulator += static_cast<std::int64_t>(std::invoke(mapper, element));
            },
            [](std::int64_t&& left, std::int64_t&& right) { return left + right; },
            [](std::int64_t&& accumulator) { return static_cast<int>(accumulator); });
    }

    /// Java: Collectors.summingLong(mapper).
    template <class T, class F>
    static Collector<T, std::int64_t, std::int64_t> summingLong(F mapper) {
        return Collector<T, std::int64_t, std::int64_t>(
            [] { return std::int64_t{0}; },
            [mapper](std::int64_t& accumulator, T& element) {
                accumulator += static_cast<std::int64_t>(std::invoke(mapper, element));
            },
            [](std::int64_t&& left, std::int64_t&& right) { return left + right; },
            [](std::int64_t&& accumulator) { return accumulator; },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.summingDouble(mapper).
    template <class T, class F>
    static Collector<T, double, double> summingDouble(F mapper) {
        return Collector<T, double, double>(
            [] { return 0.0; },
            [mapper](double& accumulator, T& element) {
                accumulator += static_cast<double>(std::invoke(mapper, element));
            },
            [](double&& left, double&& right) { return left + right; },
            [](double&& accumulator) { return accumulator; }, {Characteristics::identityFinish});
    }

    /// Java: Collectors.averagingInt(mapper). An empty stream yields 0.0, not NaN.
    template <class T, class F>
    static Collector<T, AveragingState, double> averagingInt(F mapper) {
        return Collector<T, AveragingState, double>(
            [] { return AveragingState(); },
            [mapper](AveragingState& accumulator, T& element) {
                accumulator.sum += static_cast<double>(std::invoke(mapper, element));
                ++accumulator.count;
            },
            [](AveragingState&& left, AveragingState&& right) {
                return AveragingState{
                    .sum = left.sum + right.sum,
                    .count = left.count + right.count,
                };
            },
            [](AveragingState&& accumulator) {
                return accumulator.count == 0
                           ? 0.0
                           : accumulator.sum / static_cast<double>(accumulator.count);
            });
    }

    /// Java: Collectors.averagingLong(mapper).
    template <class T, class F>
    static Collector<T, AveragingState, double> averagingLong(F mapper) {
        return Collector<T, AveragingState, double>(
            [] { return AveragingState(); },
            [mapper](AveragingState& accumulator, T& element) {
                accumulator.sum += static_cast<double>(std::invoke(mapper, element));
                ++accumulator.count;
            },
            [](AveragingState&& left, AveragingState&& right) {
                return AveragingState{
                    .sum = left.sum + right.sum,
                    .count = left.count + right.count,
                };
            },
            [](AveragingState&& accumulator) {
                return accumulator.count == 0
                           ? 0.0
                           : accumulator.sum / static_cast<double>(accumulator.count);
            });
    }

    /// Java: Collectors.averagingDouble(mapper).
    template <class T, class F>
    static Collector<T, AveragingState, double> averagingDouble(F mapper) {
        return averagingLong<T>(std::move(mapper));
    }

    // ---------------------------------------------------------------------
    // Reductions
    // ---------------------------------------------------------------------

    /// Java: Collectors.minBy(comparator). Ties keep the earlier element, as
    /// Java's `compare(a, b) <= 0 ? a : b` does.
    template <class T>
    static Collector<T, Optional<T>, Optional<T>> minBy(const Comparator<T>& comparator) {
        return Collector<T, Optional<T>, Optional<T>>(
            [] { return Optional<T>::empty(); },
            [comparator](Optional<T>& accumulator, T& element) {
                if (accumulator.isEmpty() || comparator.compare(accumulator.get(), element) > 0) {
                    accumulator = Optional<T>::of(element);
                }
            },
            [comparator](Optional<T>&& left, Optional<T>&& right) {
                if (left.isEmpty()) {
                    return std::move(right);
                }
                if (right.isEmpty()) {
                    return std::move(left);
                }
                return comparator.compare(left.get(), right.get()) <= 0 ? std::move(left) : std::move(right);
            },
            [](Optional<T>&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.maxBy(comparator).
    template <class T>
    static Collector<T, Optional<T>, Optional<T>> maxBy(const Comparator<T>& comparator) {
        return Collector<T, Optional<T>, Optional<T>>(
            [] { return Optional<T>::empty(); },
            [comparator](Optional<T>& accumulator, T& element) {
                if (accumulator.isEmpty() || comparator.compare(accumulator.get(), element) < 0) {
                    accumulator = Optional<T>::of(element);
                }
            },
            [comparator](Optional<T>&& left, Optional<T>&& right) {
                if (left.isEmpty()) {
                    return std::move(right);
                }
                if (right.isEmpty()) {
                    return std::move(left);
                }
                return comparator.compare(left.get(), right.get()) >= 0 ? std::move(left) : std::move(right);
            },
            [](Optional<T>&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.reducing(identity, op).
    template <class T, class F>
    static Collector<T, T, T> reducing(T identity, F reducer) {
        return Collector<T, T, T>(
            [identity] { return identity; },
            [reducer](T& accumulator, T& element) {
                accumulator = std::invoke(reducer, std::move(accumulator), element);
            },
            [reducer](T&& left, T&& right) {
                return std::invoke(reducer, std::move(left), std::move(right));
            },
            [](T&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.reducing(op). Empty in, empty out.
    template <class T, class F>
    static Collector<T, Optional<T>, Optional<T>> reducing(F reducer) {
        return Collector<T, Optional<T>, Optional<T>>(
            [] { return Optional<T>::empty(); },
            [reducer](Optional<T>& accumulator, T& element) {
                if (accumulator.isEmpty()) {
                    accumulator = Optional<T>::of(element);
                    return;
                }
                accumulator = Optional<T>::of(std::invoke(reducer, std::move(accumulator.get()), element));
            },
            [reducer](Optional<T>&& left, Optional<T>&& right) {
                if (left.isEmpty()) {
                    return std::move(right);
                }
                if (right.isEmpty()) {
                    return std::move(left);
                }
                return Optional<T>::of(
                    std::invoke(reducer, std::move(left.get()), std::move(right.get())));
            },
            [](Optional<T>&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    /// Java: Collectors.reducing(identity, mapper, op).
    template <class T, class U, class MF, class F>
    static Collector<T, U, U> reducing(U identity, MF mapper, F reducer) {
        return Collector<T, U, U>(
            [identity] { return identity; },
            [mapper, reducer](U& accumulator, T& element) {
                accumulator = std::invoke(reducer, std::move(accumulator), std::invoke(mapper, element));
            },
            [reducer](U&& left, U&& right) {
                return std::invoke(reducer, std::move(left), std::move(right));
            },
            [](U&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }

    // ---------------------------------------------------------------------
    // Downstream combinators
    // ---------------------------------------------------------------------

    /// Java: Collectors.mapping(mapper, downstream).
    template <class T, class F, class D>
    static Collector<T, typename D::accumulatorType, typename D::resultType> mapping(
        F mapper, const D& downstream) {
        using A = D::accumulatorType;
        return Collector<T, A, typename D::resultType>(downstream.supplier(),
            [mapper, downstream](A& accumulator, T& element) {
                auto mapped = std::invoke(mapper, element);
                std::invoke(downstream.accumulator(), accumulator, mapped);
            },
            downstream.combiner(), downstream.finisher(), downstream.characteristics());
    }

    /// Java 9: Collectors.filtering(predicate, downstream).
    template <class T, class P, class D>
    static Collector<T, typename D::accumulatorType, typename D::resultType> filtering(
        P predicate, const D& downstream) {
        using A = D::accumulatorType;
        return Collector<T, A, typename D::resultType>(downstream.supplier(),
            [predicate, downstream](A& accumulator, T& element) {
                if (std::invoke(predicate, element)) {
                    std::invoke(downstream.accumulator(), accumulator, element);
                }
            },
            downstream.combiner(), downstream.finisher(), downstream.characteristics());
    }

    /// Java 9: Collectors.flatMapping(mapper, downstream), where mapper returns a
    /// Stream. The inner stream is drained through forEach, so laziness and
    /// short-circuiting inside it are the same as anywhere else.
    template <class T, class F, class D>
    static Collector<T, typename D::accumulatorType, typename D::resultType> flatMapping(
        F mapper, D downstream) {
        using A = D::accumulatorType;
        return Collector<T, A, typename D::resultType>(downstream.supplier(),
            [mapper, downstream](A& accumulator, T& element) {
                std::invoke(mapper, element).forEach([&accumulator, &downstream](auto& mapped) {
                    std::invoke(downstream.accumulator(), accumulator, mapped);
                });
            },
            downstream.combiner(), downstream.finisher(), downstream.characteristics());
    }

    /// Java 12: Collectors.teeing(first, second, merger).
    template <class T, class D1, class D2, class M>
    static Collector<T, std::pair<typename D1::accumulatorType, typename D2::accumulatorType>,
                     std::remove_cvref_t<std::invoke_result_t<M&, typename D1::resultType,
                                                              typename D2::resultType>>>
    teeing(const D1& first, const D2& second, M merger) {
        using A1 = D1::accumulatorType;
        using A2 = D2::accumulatorType;
        using State = std::pair<A1, A2>;
        using R = std::remove_cvref_t<std::invoke_result_t<M&, typename D1::resultType,
            typename D2::resultType>>;
        return Collector<T, State, R>(
            [first, second] {
                return State(std::invoke(first.supplier()), std::invoke(second.supplier()));
            },
            [first, second](State& state, T& element) {
                std::invoke(first.accumulator(), state.first, element);
                std::invoke(second.accumulator(), state.second, element);
            },
            [first, second](State&& left, State&& right) {
                return State(
                    std::invoke(first.combiner(), std::move(left.first), std::move(right.first)),
                    std::invoke(second.combiner(), std::move(left.second), std::move(right.second)));
            },
            [merger, first, second](State&& state) {
                return std::invoke(merger, std::invoke(first.finisher(), std::move(state.first)),
                    std::invoke(second.finisher(), std::move(state.second)));
            });
    }

    // ---------------------------------------------------------------------
    // Grouping
    // ---------------------------------------------------------------------

    /// Java: Collectors.groupingBy(classifier).
    template <class T, class F, class K = MapKeyOf<T, F>>
    static Collector<T, std::unordered_map<K, ArrayList<T>>, HashMap<K, ArrayList<T>>> groupingBy(
        F classifier) {
        return groupingBy<T>(std::move(classifier), toList<T>());
    }

    /// Java: Collectors.groupingBy(classifier, downstream).
    ///
    /// The accumulator is a std::unordered_map<K, Downstream::A> rather than the
    /// HashMap the caller sees, because grouping wants operator[]-shaped access and
    /// a cheap "insert the downstream's empty accumulator" step; the finisher then
    /// runs the downstream finisher once per group.
    template <class T, class F, class D, class K = MapKeyOf<T, F>>
    static Collector<T, std::unordered_map<K, typename D::accumulatorType>,
                     HashMap<K, typename D::resultType>>
    groupingBy(F classifier, const D& downstream) {
        using A = D::accumulatorType;
        using R = D::resultType;
        using Accumulator = std::unordered_map<K, A>;
        return Collector<T, Accumulator, HashMap<K, R>>(
            [] { return Accumulator(); },
            [classifier, downstream](Accumulator& accumulator, T& element) {
                K key = std::invoke(classifier, element);
                auto position = accumulator.find(key);
                if (position == accumulator.end()) {
                    position = accumulator.emplace(key, std::invoke(downstream.supplier())).first;
                }
                std::invoke(downstream.accumulator(), position->second, element);
            },
            [downstream](Accumulator&& left, Accumulator&& right) {
                for (auto& entry : right) {
                    auto position = left.find(entry.first);
                    if (position == left.end()) {
                        left.emplace(std::move(entry.first), std::move(entry.second));
                    } else {
                        position->second = std::invoke(
                            downstream.combiner(), std::move(position->second), std::move(entry.second));
                    }
                }
                return std::move(left);
            },
            [downstream](Accumulator&& accumulator) {
                std::unordered_map<K, R> finished;
                finished.reserve(accumulator.size());
                for (auto& entry : accumulator) {
                    finished.emplace(
                        entry.first, std::invoke(downstream.finisher(), std::move(entry.second)));
                }
                return HashMap<K, R>(std::move(finished));
            });
    }

    /// Java: Collectors.groupingBy(classifier, mapFactory, downstream) is not
    /// provided yet; HashMap is the only map factory this version ships.
    template <class T, class P>
    static Collector<T, std::unordered_map<bool, ArrayList<T>>, HashMap<bool, ArrayList<T>>>
    partitioningBy(P predicate) {
        return partitioningBy<T>(std::move(predicate), toList<T>());
    }

    /// Java: Collectors.partitioningBy(predicate, downstream).
    ///
    /// Java guarantees the result always has exactly two entries, false first, even
    /// when a partition is empty. The finisher below preserves that.
    template <class T, class P, class D>
    static Collector<T, std::unordered_map<bool, typename D::accumulatorType>,
                     HashMap<bool, typename D::resultType>>
    partitioningBy(P predicate, const D& downstream) {
        using A = D::accumulatorType;
        using R = D::resultType;
        using Accumulator = std::unordered_map<bool, A>;
        return Collector<T, Accumulator, HashMap<bool, R>>(
            [] { return Accumulator(); },
            [predicate, downstream](Accumulator& accumulator, T& element) {
                const bool key = std::invoke(predicate, element);
                auto position = accumulator.find(key);
                if (position == accumulator.end()) {
                    position = accumulator.emplace(key, std::invoke(downstream.supplier())).first;
                }
                std::invoke(downstream.accumulator(), position->second, element);
            },
            [downstream](Accumulator&& left, Accumulator&& right) {
                for (auto& entry : right) {
                    auto position = left.find(entry.first);
                    if (position == left.end()) {
                        left.emplace(std::move(entry.first), std::move(entry.second));
                    } else {
                        position->second = std::invoke(
                            downstream.combiner(), std::move(position->second), std::move(entry.second));
                    }
                }
                return std::move(left);
            },
            [downstream](Accumulator&& accumulator) {
                std::unordered_map<bool, R> finished;
                for (const bool key : {false, true}) {
                    auto position = accumulator.find(key);
                    A partition = position == accumulator.end()
                                      ? std::invoke(downstream.supplier())
                                      : std::move(position->second);
                    finished.emplace(key, std::invoke(downstream.finisher(), std::move(partition)));
                }
                return HashMap<bool, R>(std::move(finished));
            });
    }

    // ---------------------------------------------------------------------
    // Statistics
    // ---------------------------------------------------------------------

    /// Java: Collectors.summarizingInt(mapper).
    template <class T, class F>
    static Collector<T, IntSummaryStatistics, IntSummaryStatistics> summarizingInt(F mapper) {
        return summarizing<T, IntSummaryStatistics>(
            [mapper](IntSummaryStatistics& statistics, T& element) {
                statistics.accept(std::invoke(mapper, element));
            });
    }

    /// Java: Collectors.summarizingLong(mapper).
    template <class T, class F>
    static Collector<T, LongSummaryStatistics, LongSummaryStatistics> summarizingLong(F mapper) {
        return summarizing<T, LongSummaryStatistics>(
            [mapper](LongSummaryStatistics& statistics, T& element) {
                statistics.accept(std::invoke(mapper, element));
            });
    }

    /// Java: Collectors.summarizingDouble(mapper).
    template <class T, class F>
    static Collector<T, DoubleSummaryStatistics, DoubleSummaryStatistics> summarizingDouble(
        F mapper) {
        return summarizing<T, DoubleSummaryStatistics>(
            [mapper](DoubleSummaryStatistics& statistics, T& element) {
                statistics.accept(std::invoke(mapper, element));
            });
    }

private:
    /// The shared shape of the three summarizing collectors: an accumulator that
    /// is its own result, and a combiner that combines in place.
    template <class T, class Statistics, class Accept>
    static Collector<T, Statistics, Statistics> summarizing(Accept accept) {
        return Collector<T, Statistics, Statistics>(
            [] { return Statistics(); },
            [accept](Statistics& accumulator, T& element) {
                std::invoke(accept, accumulator, element);
            },
            [](Statistics&& left, Statistics&& right) {
                left.combine(right);
                return std::move(left);
            },
            [](Statistics&& accumulator) { return std::move(accumulator); },
            {Characteristics::identityFinish});
    }
};

}  // namespace cppstream
