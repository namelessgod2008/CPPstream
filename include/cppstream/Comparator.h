#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace cppstream {

/// A port of java.util.Comparator.
///
/// Java's Comparator is an interface with default methods. The C++ translation
/// is a class holding one type-erased comparison function, which is what makes
/// reversed() and the thenComparing() family composable without extra template
/// machinery at every call site.
///
/// The comparison contract is Java's, not C++'s: compare() returns a negative
/// int, zero, or a positive int, exactly as Comparable.compareTo does. The
/// operator() overload is a deliberate C++ extension so that the same object can
/// be handed to std::sort, std::ranges algorithms, std::map and friends; a
/// comparator that standard algorithms cannot accept would be useless.
///
/// Deliberate divergence from Java: Comparator.equals is not provided. Java's
/// default implementation is reference identity, which std::function cannot
/// replicate meaningfully. Use compare() on sample values instead.
template <class T>
class Comparator {
public:
    using CompareFn = std::function<int(const T&, const T&)>;

    /// Java's Comparator has no public constructor; an instance always comes
    /// from a lambda or from one of the factories below.
    explicit Comparator(CompareFn compareFn) : compareFn_(std::move(compareFn)) {}

    /// Java: Comparator.compare(left, right).
    [[nodiscard]] int compare(const T& left, const T& right) const {
        return compareFn_(left, right);
    }

    /// Strict weak ordering, for std::sort and the standard algorithms.
    [[nodiscard]] bool operator()(const T& left, const T& right) const {
        return compareFn_(left, right) < 0;
    }

    /// Java: Comparator.reversed().
    [[nodiscard]] Comparator reversed() const {
        return Comparator([fn = compareFn_](const T& left, const T& right) {
            return fn(right, left);
        });
    }

    /// Java: Comparator.thenComparing(other).
    [[nodiscard]] Comparator thenComparing(const Comparator& other) const {
        return Comparator([self = *this, other](const T& left, const T& right) {
            const int result = self.compare(left, right);
            return result != 0 ? result : other.compare(left, right);
        });
    }

    /// Java: Comparator.thenComparing(keyExtractor), using the key's natural
    /// order.
    template <class F>
    [[nodiscard]] Comparator thenComparing(F keyExtractor) const {
        using U = std::remove_cvref_t<std::invoke_result_t<F, const T&>>;
        return thenComparing(std::move(keyExtractor), Comparator<U>::naturalOrder());
    }

    /// Java: Comparator.thenComparing(keyExtractor, keyComparator).
    template <class F, class U>
    [[nodiscard]] Comparator thenComparing(F keyExtractor, const Comparator<U>& keyComparator) const {
        return Comparator([self = *this, keyExtractor = std::move(keyExtractor), keyComparator](
                              const T& left, const T& right) {
            const int result = self.compare(left, right);
            if (result != 0) {
                return result;
            }
            return keyComparator.compare(
                std::invoke(keyExtractor, left), std::invoke(keyExtractor, right));
        });
    }

    // --- Factories ---------------------------------------------------------

    /// Java: Comparator.naturalOrder(). Defined via operator< rather than
    /// std::compare_three_way so that it also accepts types that predate the
    /// spaceship operator.
    static Comparator naturalOrder() {
        return Comparator([](const T& left, const T& right) {
            if (left < right) {
                return -1;
            }
            if (right < left) {
                return 1;
            }
            return 0;
        });
    }

    /// Java: Comparator.reverseOrder().
    static Comparator reverseOrder() { return naturalOrder().reversed(); }

    /// Java: Comparator.comparing(keyExtractor).
    template <class F>
    static Comparator comparing(F keyExtractor) {
        using U = std::remove_cvref_t<std::invoke_result_t<F, const T&>>;
        return comparing(std::move(keyExtractor), Comparator<U>::naturalOrder());
    }

    /// Java: Comparator.comparing(keyExtractor, keyComparator).
    template <class F, class U>
    static Comparator comparing(F keyExtractor, const Comparator<U>& keyComparator) {
        return Comparator([keyExtractor = std::move(keyExtractor), keyComparator](
                              const T& left, const T& right) {
            return keyComparator.compare(
                std::invoke(keyExtractor, left), std::invoke(keyExtractor, right));
        });
    }

    /// Java: Comparator.nullsFirst(comparator). Only meaningful for pointer
    /// payloads, which are the only place C++ can spell "null".
    static Comparator nullsFirst(Comparator comparator)
        requires std::is_pointer_v<T>
    {
        return Comparator([comparator = std::move(comparator)](const T& left, const T& right) {
            if (left == nullptr) {
                return right == nullptr ? 0 : -1;
            }
            if (right == nullptr) {
                return 1;
            }
            return comparator.compare(left, right);
        });
    }

    /// Java: Comparator.nullsLast(comparator).
    static Comparator nullsLast(Comparator comparator)
        requires std::is_pointer_v<T>
    {
        return Comparator([comparator = std::move(comparator)](const T& left, const T& right) {
            if (left == nullptr) {
                return right == nullptr ? 0 : 1;
            }
            if (right == nullptr) {
                return -1;
            }
            return comparator.compare(left, right);
        });
    }

private:
    CompareFn compareFn_;
};

/// Bridges a std-style ordering -- the kind std::set and std::map demand, and the
/// kind this library's containers are parameterised on -- to the Comparator<T>
/// whose compare() contract is Java's: negative, zero or positive.
///
/// C++-only, and public because TreeSet/TreeMap and their views all need it:
/// SortedSet.comparator() and SortedSet.descendingSet().comparator() differ only in
/// whether the ordering is reversed on the way in.
template <class T, class Compare>
[[nodiscard]] Comparator<T> comparatorFrom(const Compare& compare) {
    return Comparator<T>([compare](const T& left, const T& right) {
        if (compare(left, right)) {
            return -1;
        }
        if (compare(right, left)) {
            return 1;
        }
        return 0;
    });
}

/// Reverses a std-style ordering.
///
/// The containers are parameterised on a std-style comparer (`std::less` by
/// default) rather than on Comparator<T>, because that is what std::set and
/// std::map demand. `descendingSet()` / `descendingMap()` therefore need a way to
/// *name* the type of the reversed ordering; this is that name.
///
/// Note the two conventions side by side: Comparator<T>::compare answers "where
/// does left sit relative to right", ReversedCompare answers the same question
/// with the operands swapped.
template <class T, class Compare>
struct ReversedCompare {
    Compare underlying{};

    [[nodiscard]] bool operator()(const T& left, const T& right) const {
        return underlying(right, left);
    }
};

}  // namespace cppstream
