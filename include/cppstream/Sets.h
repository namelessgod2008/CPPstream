#pragma once

#include <cppstream/AbstractSet.h>
#include <cppstream/Collection.h>
#include <cppstream/HashSet.h>

#include <initializer_list>
#include <type_traits>
#include <utility>

namespace cppstream {

/// Java: java.util.Set's static factories, relocated for the same cycle reason as
/// Lists. Every factory returns a frozen set.
class Sets {
public:
    /// Java: Set.of().
    template <class T>
    [[nodiscard]] static HashSet<T> empty() {
        HashSet<T> result;
        result.freeze();
        return result;
    }

    /// Java: Set.of(values...). Duplicates are a runtime error in Java; here they
    /// are silently collapsed, because a compile-time check would need consteval
    /// machinery that buys nothing.
    template <class T, class U, class... Rest>
        requires std::same_as<std::remove_cvref_t<U>, T> &&
                 (std::same_as<std::remove_cvref_t<Rest>, T> && ...)
    [[nodiscard]] static HashSet<T> of(U&& first, Rest&&... rest) {
        HashSet<T> result;
        result.add(std::forward<U>(first));
        (result.add(std::forward<Rest>(rest)), ...);
        result.freeze();
        return result;
    }

    /// Java: Set.copyOf(collection). An immutable copy.
    template <class T>
    [[nodiscard]] static HashSet<T> copyOf(const Collection<T>& source) {
        HashSet<T> result(source);
        result.freeze();
        return result;
    }

    /// Java: Collections.unmodifiableSet(set).
    template <class T>
    [[nodiscard]] static HashSet<T> unmodifiableSet(HashSet<T> set) {
        set.freeze();
        return set;
    }
};

}  // namespace cppstream
