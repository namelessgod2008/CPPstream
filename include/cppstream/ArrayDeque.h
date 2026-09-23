#pragma once

#include <cppstream/Deque.h>
#include <cppstream/Elements.h>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <utility>

namespace cppstream {

/// A port of java.util.ArrayDeque, backed by std::deque.
///
/// Java's ArrayDeque is a circular buffer; std::deque is a deque of blocks. The
/// public contract is what matters here, and it is identical: amortised O(1) at
/// both ends, no capacity limit, no nulls.
///
/// Java's ArrayDeque is a Deque and a Collection but *not* a List, so this does not
/// offer get(index). Reaching the middle of a deque is indexOf/remove style work.
template <class T>
class ArrayDeque : public Deque<T> {
public:
    using valueType = T;
    using Nodes = std::deque<T>;

    using Deque<T>::add;
    using Deque<T>::remove;

    ArrayDeque() = default;

    /// Java: new ArrayDeque<>(collection). Java grows the buffer to fit; std::deque
    /// has no reserve, so this simply appends.
    explicit ArrayDeque(const Collection<T>& source) { this->addAll(source); }

    explicit ArrayDeque(Nodes nodes) : nodes_(std::move(nodes)) {}

    ArrayDeque(std::initializer_list<T> values) : nodes_(values) {}

    [[nodiscard]] int size() const override { return static_cast<int>(nodes_.size()); }

    [[nodiscard]] bool contains(const T& element) const override {
        return std::find_if(nodes_.begin(), nodes_.end(), [&element](const T& candidate) {
            return elementEquals(candidate, element);
        }) != nodes_.end();
    }

    // --- Collection / Queue -------------------------------------------------

    /// Java: Collection.add(element) / Queue.add(element). Unbounded, so this
    /// always succeeds; Java would throw IllegalStateException on a bounded queue.
    bool add(const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        nodes_.push_back(element);
        ++this->modCount_;
        return true;
    }

    bool add(T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        nodes_.push_back(std::move(element));
        ++this->modCount_;
        return true;
    }

    bool remove(const T& element) override {
        this->checkNotFrozen();
        auto position = std::find_if(nodes_.begin(), nodes_.end(), [&element](const T& candidate) {
            return elementEquals(candidate, element);
        });
        if (position == nodes_.end()) {
            return false;
        }
        nodes_.erase(position);
        ++this->modCount_;
        return true;
    }

    void clear() override {
        this->checkNotFrozen();
        if (!nodes_.empty()) {
            ++this->modCount_;
        }
        nodes_.clear();
    }

    [[nodiscard]] bool offer(const T& element) override { return this->add(element); }
    [[nodiscard]] bool offer(T&& element) override { return this->add(std::move(element)); }

    [[nodiscard]] T remove() override { return removeFirst(); }

    [[nodiscard]] Optional<T> poll() override { return pollFirst(); }

    [[nodiscard]] const T& element() const override { return getFirst(); }

    [[nodiscard]] Optional<T> peek() const override { return peekFirst(); }

    // --- Deque: adding ------------------------------------------------------

    void addFirst(const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        nodes_.push_front(element);
        ++this->modCount_;
    }

    void addFirst(T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        nodes_.push_front(std::move(element));
        ++this->modCount_;
    }

    void addLast(const T& element) override { static_cast<void>(this->add(element)); }
    void addLast(T&& element) override { static_cast<void>(this->add(std::move(element))); }

    [[nodiscard]] bool offerFirst(const T& element) override {
        addFirst(element);
        return true;
    }

    [[nodiscard]] bool offerFirst(T&& element) override {
        addFirst(std::move(element));
        return true;
    }

    [[nodiscard]] bool offerLast(const T& element) override { return this->add(element); }
    [[nodiscard]] bool offerLast(T&& element) override { return this->add(std::move(element)); }

    // --- Deque: removing ----------------------------------------------------

    [[nodiscard]] T removeFirst() override {
        this->checkNotFrozen();
        requireNotEmpty();
        T removed = std::move(nodes_.front());
        nodes_.pop_front();
        ++this->modCount_;
        return removed;
    }

    [[nodiscard]] T removeLast() override {
        this->checkNotFrozen();
        requireNotEmpty();
        T removed = std::move(nodes_.back());
        nodes_.pop_back();
        ++this->modCount_;
        return removed;
    }

    [[nodiscard]] Optional<T> pollFirst() override {
        if (nodes_.empty()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(removeFirst());
    }

    [[nodiscard]] Optional<T> pollLast() override {
        if (nodes_.empty()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(removeLast());
    }

    [[nodiscard]] bool removeFirstOccurrence(const T& element) override {
        return this->remove(element);
    }

    /// Java: Deque.removeLastOccurrence(element). Scans from the tail.
    [[nodiscard]] bool removeLastOccurrence(const T& element) override {
        this->checkNotFrozen();
        auto position = std::find_if(nodes_.rbegin(), nodes_.rend(), [&element](const T& candidate) {
            return elementEquals(candidate, element);
        });
        if (position == nodes_.rend()) {
            return false;
        }
        nodes_.erase(std::next(position).base());
        ++this->modCount_;
        return true;
    }

    // --- Deque: inspecting --------------------------------------------------

    [[nodiscard]] const T& getFirst() const override {
        requireNotEmpty();
        return nodes_.front();
    }

    [[nodiscard]] const T& getLast() const override {
        requireNotEmpty();
        return nodes_.back();
    }

    [[nodiscard]] Optional<T> peekFirst() const override {
        if (nodes_.empty()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(nodes_.front());
    }

    [[nodiscard]] Optional<T> peekLast() const override {
        if (nodes_.empty()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(nodes_.back());
    }

    // --- Deque: stack -------------------------------------------------------

    void push(const T& element) override { addFirst(element); }
    void push(T&& element) override { addFirst(std::move(element)); }
    [[nodiscard]] T pop() override { return removeFirst(); }

    // --- Traversal ----------------------------------------------------------

    [[nodiscard]] std::unique_ptr<Iterator<T>> iterator() override {
        return std::make_unique<ArrayDequeIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> constIterator() const override {
        return std::make_unique<ArrayDequeConstIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<Iterator<T>> descendingIterator() override {
        return std::make_unique<ArrayDequeDescendingIterator>(*this);
    }

    class ArrayDequeConstIterator final : public Iterator<const T> {
    public:
        explicit ArrayDequeConstIterator(const ArrayDeque& owner)
            : owner_(&owner), current_(owner.nodes_.begin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->nodes_.end(); }

        const T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->nodes_.end()) {
                throw NoSuchElementException("iterator exhausted");
            }
            return *current_++;
        }

    private:
        const ArrayDeque* owner_;
        Nodes::const_iterator current_;
        int expectedModCount_;
    };

    class ArrayDequeIterator final : public Iterator<T> {
    public:
        explicit ArrayDequeIterator(ArrayDeque& owner)
            : owner_(&owner), current_(owner.nodes_.begin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->nodes_.end(); }

        T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->nodes_.end()) {
                throw NoSuchElementException("iterator exhausted");
            }
            last_ = current_;
            hasLast_ = true;
            return *current_++;
        }

        void remove() override {
            owner_->checkNotFrozen();
            owner_->checkForComodification(expectedModCount_);
            if (!hasLast_) {
                throw IllegalStateException("remove() called before next()");
            }
            // std::deque::erase invalidates *every* iterator into the container,
            // unlike std::list's. The cursor therefore has to be rebuilt from
            // erase()'s return value (the element that followed the removed one,
            // which is exactly where the cursor already was) instead of being
            // left dangling. Iterators over a std::deque are otherwise unusable
            // through remove().
            current_ = owner_->nodes_.erase(last_);
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
            hasLast_ = false;
        }

    private:
        ArrayDeque* owner_;
        Nodes::iterator current_;
        Nodes::iterator last_;
        bool hasLast_ = false;
        int expectedModCount_;
    };

    class ArrayDequeDescendingIterator final : public Iterator<T> {
    public:
        explicit ArrayDequeDescendingIterator(ArrayDeque& owner)
            : owner_(&owner), current_(owner.nodes_.rbegin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->nodes_.rend(); }

        T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->nodes_.rend()) {
                throw NoSuchElementException("iterator exhausted");
            }
            last_ = current_;
            hasLast_ = true;
            return *current_++;
        }

        void remove() override {
            owner_->checkNotFrozen();
            owner_->checkForComodification(expectedModCount_);
            if (!hasLast_) {
                throw IllegalStateException("remove() called before next()");
            }
            // See the forward iterator: the whole container's iterators die with
            // the erase, so the reverse cursor is rebuilt from the successor of
            // the removed element. make_reverse_iterator(successor) points back at
            // the element *before* the removed one, which is the next one to visit.
            const auto successor = owner_->nodes_.erase(std::prev(last_.base()));
            current_ = typename Nodes::reverse_iterator(successor);
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
            hasLast_ = false;
        }

    private:
        ArrayDeque* owner_;
        Nodes::reverse_iterator current_;
        Nodes::reverse_iterator last_;
        bool hasLast_ = false;
        int expectedModCount_;
    };

private:
    void requireNotEmpty() const {
        if (nodes_.empty()) {
            throw NoSuchElementException("deque is empty");
        }
    }

    Nodes nodes_;
};

}  // namespace cppstream
