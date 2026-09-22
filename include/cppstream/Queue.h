#pragma once

#include <cppstream/AbstractCollection.h>
#include <cppstream/Optional.h>

#include <utility>

namespace cppstream {

/// A port of java.util.Queue.
///
/// Java's Queue pairs each operation with a "throw" form and a "return a sentinel"
/// form: add/offer, remove/poll, element/peek. C++ has no null, so the sentinel
/// forms return Optional<T> -- the same trade-off made throughout the library
/// (§8 items 4 and 5).
///
/// Queue derives from AbstractCollection rather than Collection, like List and
/// Set, so that the whole container layer stays one inheritance chain.
template <class T>
class Queue : public AbstractCollection<T> {
public:
    using valueType = T;

    /// remove() below would otherwise hide Collection::remove(element), exactly as
    /// List::add(index, element) hides Collection::add(element).
    using AbstractCollection<T>::remove;

    /// Java: Queue.offer(element). False when the queue is full; this library's
    /// implementations are unbounded, so they always return true.
    [[nodiscard]] virtual bool offer(const T& element) = 0;
    [[nodiscard]] virtual bool offer(T&& element) = 0;

    /// Java: Queue.remove(). The head leaves the queue; NoSuchElementException
    /// when empty.
    [[nodiscard]] virtual T remove() = 0;

    /// Java: Queue.poll(). Empty when the queue is empty.
    [[nodiscard]] virtual Optional<T> poll() = 0;

    /// Java: Queue.element(). Peek without removing; the reference stays valid as
    /// long as the queue is not structurally modified.
    [[nodiscard]] virtual const T& element() const = 0;

    /// Java: Queue.peek().
    [[nodiscard]] virtual Optional<T> peek() const = 0;
};

}  // namespace cppstream
