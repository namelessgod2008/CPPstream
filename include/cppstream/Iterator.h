#pragma once

#include <cppstream/RuntimeException.h>

#include <functional>
#include <utility>

namespace cppstream {

/// A port of java.util.Iterator.
///
/// next() hands out a mutable reference, matching Java's reference semantics.
/// Instantiating this template with `const T` produces the read-only variant used
/// by the const access path: `Iterator<const T>::next()` returns `const T&`, and
/// remove() falls through to the default throwing implementation below without a
/// single extra line. That is how the container layer keeps const-correctness
/// without a second iterator hierarchy.
template <class T>
class Iterator {
public:
    virtual ~Iterator() = default;

    /// Java: Iterator.hasNext().
    [[nodiscard]] virtual bool hasNext() const = 0;

    /// Java: Iterator.next(). Throws NoSuchElementException when exhausted.
    virtual T& next() = 0;

    /// Java: Iterator.remove(). An optional operation: the default throws, and
    /// only iterators over mutable containers override it.
    virtual void remove();

    /// Java: Iterator.forEachRemaining(action).
    ///
    /// Java takes a functional interface, which C++ cannot name in a virtual
    /// signature; std::move_only_function is the closest transliteration. Unlike
    /// Java's Consumer<? super E> this must be written exactly, so callers pass a
    /// lambda that already accepts `T&`.
    virtual void forEachRemaining(std::move_only_function<void(T&)> action);
};

template <class T>
void Iterator<T>::remove() {
    throw UnsupportedOperationException("remove");
}

template <class T>
void Iterator<T>::forEachRemaining(std::move_only_function<void(T&)> action) {
    while (hasNext()) {
        std::invoke(action, next());
    }
}

}  // namespace cppstream
