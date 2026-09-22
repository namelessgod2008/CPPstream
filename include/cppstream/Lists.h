#pragma once

#include <cppstream/ArrayList.h>
#include <cppstream/Collection.h>

#include <functional>
#include <type_traits>
#include <utility>

namespace cppstream {

/// Java: java.util.List's static factories, relocated into a utility class.
///
/// Java 9 hangs of()/copyOf() off the List interface. Here that would be a cycle:
/// List<T>::of must return ArrayList<T>, and ArrayList.h already includes List.h
/// (DESIGN.md divergence 6). Hence a separate class, which is also where Java
/// keeps Collections.unmodifiableList.
///
/// Every factory returns a *frozen* list, matching Java's contract that List.of and
/// List.copyOf are immutable. To get a mutable list back, construct one:
/// `ArrayList<T> mutable(List<T> copy)`.
class Lists {
public:
    /// Java: List.of().
    template <class T>
    [[nodiscard]] static ArrayList<T> empty() {
        ArrayList<T> result;
        result.freeze();
        return result;
    }

    /// Java: List.of(values...). Every argument must already be exactly T for the
    /// same reason as Stream::of (divergence 25).
    template <class T, class U, class... Rest>
        requires std::same_as<std::remove_cvref_t<U>, T> &&
                 (std::same_as<std::remove_cvref_t<Rest>, T> && ...)
    [[nodiscard]] static ArrayList<T> of(U&& first, Rest&&... rest) {
        ArrayList<T> result;
        result.add(std::forward<U>(first));
        (result.add(std::forward<Rest>(rest)), ...);
        result.freeze();
        return result;
    }

    /// Java: List.copyOf(collection). An immutable copy.
    template <class T>
    [[nodiscard]] static ArrayList<T> copyOf(const Collection<T>& source) {
        ArrayList<T> result(source);
        result.freeze();
        return result;
    }

    /// Java: Collections.unmodifiableList(list). Takes the list by value so that
    /// `unmodifiableList(std::move(list))` freezes without copying; Java's version
    /// is a live view, which this version does not have (divergence 12).
    template <class T>
    [[nodiscard]] static ArrayList<T> unmodifiableList(ArrayList<T> list) {
        list.freeze();
        return list;
    }

    /// Java 21: List.reversed(). Java returns a view; this returns a copy for the
    /// same lifetime reason as subList (divergence 11).
    template <class T>
    [[nodiscard]] static ArrayList<T> reversed(const ArrayList<T>& list) {
        ArrayList<T> result;
        for (int index = list.size() - 1; index >= 0; --index) {
            result.add(list.get(index));
        }
        return result;
    }
};

}  // namespace cppstream
