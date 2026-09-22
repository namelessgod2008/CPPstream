#pragma once

#include <cppstream/Queue.h>

#include <memory>
#include <utility>

namespace cppstream {

/// A port of java.util.Deque.
///
/// Both ends are first class: every operation comes in a First and a Last
/// flavour, so the same type serves as a queue (addLast/pollFirst), a stack
/// (push/pop), or a deque proper.
///
/// Note what is *not* here: LinkedList does not implement Deque, even though
/// java.util.LinkedList does. Java can afford `implements List, Deque` because
/// interfaces carry no state; in C++ both are abstract classes descending from
/// Collection, so a class deriving from both would inherit Collection twice and
/// need virtual inheritance through the entire hierarchy. ArrayDeque is the
/// Deque implementation instead, and LinkedList keeps the same operations as
/// ordinary members (§8 item 29).
template <class T>
class Deque : public Queue<T> {
public:
    using valueType = T;
    using Queue<T>::remove;

    // --- Adding -------------------------------------------------------------

    /// Java: Deque.addFirst(element).
    virtual void addFirst(const T& element) = 0;
    virtual void addFirst(T&& element) = 0;

    /// Java: Deque.addLast(element). Equivalent to Collection::add.
    virtual void addLast(const T& element) = 0;
    virtual void addLast(T&& element) = 0;

    /// Java: Deque.offerFirst(element).
    [[nodiscard]] virtual bool offerFirst(const T& element) = 0;
    [[nodiscard]] virtual bool offerFirst(T&& element) = 0;

    /// Java: Deque.offerLast(element). Equivalent to Queue::offer.
    [[nodiscard]] virtual bool offerLast(const T& element) = 0;
    [[nodiscard]] virtual bool offerLast(T&& element) = 0;

    // --- Removing -----------------------------------------------------------

    /// Java: Deque.removeFirst().
    [[nodiscard]] virtual T removeFirst() = 0;

    /// Java: Deque.removeLast().
    [[nodiscard]] virtual T removeLast() = 0;

    /// Java: Deque.pollFirst().
    [[nodiscard]] virtual Optional<T> pollFirst() = 0;

    /// Java: Deque.pollLast().
    [[nodiscard]] virtual Optional<T> pollLast() = 0;

    /// Java: Deque.removeFirstOccurrence(element). Equivalent to
    /// Collection::remove(element), spelled out for source compatibility.
    [[nodiscard]] virtual bool removeFirstOccurrence(const T& element) = 0;

    /// Java: Deque.removeLastOccurrence(element).
    [[nodiscard]] virtual bool removeLastOccurrence(const T& element) = 0;

    // --- Inspecting ---------------------------------------------------------

    /// Java: Deque.getFirst().
    [[nodiscard]] virtual const T& getFirst() const = 0;

    /// Java: Deque.getLast().
    [[nodiscard]] virtual const T& getLast() const = 0;

    /// Java: Deque.peekFirst().
    [[nodiscard]] virtual Optional<T> peekFirst() const = 0;

    /// Java: Deque.peekLast().
    [[nodiscard]] virtual Optional<T> peekLast() const = 0;

    // --- Stack operations ---------------------------------------------------

    /// Java: Deque.push(element). Pushes onto the head, so it is addFirst.
    virtual void push(const T& element) = 0;
    virtual void push(T&& element) = 0;

    /// Java: Deque.pop(). Pops the head, so it is removeFirst.
    [[nodiscard]] virtual T pop() = 0;

    // --- Traversal ----------------------------------------------------------

    /// Java: Deque.descendingIterator(), from tail to head.
    [[nodiscard]] virtual std::unique_ptr<Iterator<T>> descendingIterator() = 0;
};

}  // namespace cppstream
