#pragma once

#include <cppstream/Queue.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace cppstream {

template <class T>
class ReversedDequeView;

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

    /// Java 21: SequencedCollection.reversed(), a live view in reverse order.
    /// Returned by pointer for the same reason as List::reversed(): it is a view,
    /// not a value.
    [[nodiscard]] std::unique_ptr<Deque<T>> reversed();
    [[nodiscard]] std::unique_ptr<Deque<T>> reversed() const;
};

/// The live reverse view behind Deque::reversed().
///
/// Every operation is mirrored onto the backing deque: the view's first is the
/// backing's last, the view's addFirst appends to the backing, and so on. The
/// read-only form is built by the const overload of reversed() and rejects every
/// mutator, just as ListView does.
template <class T>
class ReversedDequeView final : public Deque<T> {
public:
    using Deque<T>::add;
    using Deque<T>::remove;

    explicit ReversedDequeView(Deque<T>& backing)
        : backing_(&backing), writableBacking_(&backing) {}

    explicit ReversedDequeView(const Deque<T>& backing) : backing_(&backing) {}

    [[nodiscard]] int size() const override { return backing_->size(); }

    [[nodiscard]] int modCount() const noexcept override { return backing_->modCount(); }

    bool add(const T& element) override {
        writable().addFirst(element);
        return true;
    }

    bool add(T&& element) override {
        writable().addFirst(std::move(element));
        return true;
    }

    bool remove(const T& element) override { return writable().removeLastOccurrence(element); }

    bool offer(const T& element) override { return add(element); }
    bool offer(T&& element) override { return add(std::move(element)); }

    [[nodiscard]] T remove() override { return removeFirst(); }
    [[nodiscard]] Optional<T> poll() override { return pollFirst(); }
    [[nodiscard]] const T& element() const override { return getFirst(); }
    [[nodiscard]] Optional<T> peek() const override { return peekFirst(); }

    void addFirst(const T& element) override { writable().addLast(element); }
    void addFirst(T&& element) override { writable().addLast(std::move(element)); }
    void addLast(const T& element) override { static_cast<void>(add(element)); }
    void addLast(T&& element) override { static_cast<void>(add(std::move(element))); }

    [[nodiscard]] bool offerFirst(const T& element) override {
        return writable().offerLast(element);
    }
    [[nodiscard]] bool offerFirst(T&& element) override {
        return writable().offerLast(std::move(element));
    }
    [[nodiscard]] bool offerLast(const T& element) override { return add(element); }
    [[nodiscard]] bool offerLast(T&& element) override { return add(std::move(element)); }

    [[nodiscard]] T removeFirst() override { return writable().removeLast(); }
    [[nodiscard]] T removeLast() override { return writable().removeFirst(); }
    [[nodiscard]] Optional<T> pollFirst() override { return writable().pollLast(); }
    [[nodiscard]] Optional<T> pollLast() override { return writable().pollFirst(); }

    [[nodiscard]] bool removeFirstOccurrence(const T& element) override {
        return writable().removeLastOccurrence(element);
    }
    [[nodiscard]] bool removeLastOccurrence(const T& element) override {
        return writable().removeFirstOccurrence(element);
    }

    [[nodiscard]] const T& getFirst() const override { return backing_->getLast(); }
    [[nodiscard]] const T& getLast() const override { return backing_->getFirst(); }
    [[nodiscard]] Optional<T> peekFirst() const override { return backing_->peekLast(); }
    [[nodiscard]] Optional<T> peekLast() const override { return backing_->peekFirst(); }

    void push(const T& element) override { addFirst(element); }
    void push(T&& element) override { addFirst(std::move(element)); }
    [[nodiscard]] T pop() override { return removeFirst(); }

    [[nodiscard]] std::unique_ptr<Iterator<T>> iterator() override {
        return writable().descendingIterator();
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> constIterator() const override {
        std::vector<T> snapshot;
        snapshot.reserve(static_cast<std::size_t>(backing_->size()));
        std::unique_ptr<Iterator<const T>> source = backing_->constIterator();
        while (source->hasNext()) {
            snapshot.push_back(source->next());
        }
        std::reverse(snapshot.begin(), snapshot.end());
        return std::make_unique<ReversedDequeConstIterator>(this, std::move(snapshot),
                                                            backing_->modCount());
    }

    [[nodiscard]] std::unique_ptr<Iterator<T>> descendingIterator() override {
        return writable().iterator();
    }

    class ReversedDequeConstIterator final : public Iterator<const T> {
    public:
        ReversedDequeConstIterator(const ReversedDequeView* owner, std::vector<T> values,
                                   int expectedModCount)
            : owner_(owner), values_(std::move(values)), expectedModCount_(expectedModCount) {}

        [[nodiscard]] bool hasNext() const override { return position_ < values_.size(); }

        const T& next() override {
            if (owner_->backing_->modCount() != expectedModCount_) {
                throw ConcurrentModificationException("backing deque modified during iteration");
            }
            if (position_ >= values_.size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            return values_[position_++];
        }

    private:
        const ReversedDequeView* owner_;
        std::vector<T> values_;
        std::size_t position_ = 0;
        int expectedModCount_;
    };

private:
    [[nodiscard]] Deque<T>& writable() const {
        this->checkNotFrozen();
        if (writableBacking_ == nullptr) {
            throw UnsupportedOperationException(
                "this deque view is read-only: it was taken from a const deque");
        }
        return *writableBacking_;
    }

    const Deque<T>* backing_;
    Deque<T>* writableBacking_ = nullptr;
};

template <class T>
std::unique_ptr<Deque<T>> Deque<T>::reversed() {
    return std::make_unique<ReversedDequeView<T>>(*this);
}

template <class T>
std::unique_ptr<Deque<T>> Deque<T>::reversed() const {
    return std::make_unique<ReversedDequeView<T>>(static_cast<const Deque<T>&>(*this));
}

}  // namespace cppstream
