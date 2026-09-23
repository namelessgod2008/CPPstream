#pragma once

#include <cppstream/ArrayList.h>
#include <cppstream/Elements.h>
#include <cppstream/List.h>

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace cppstream {

/// A port of java.util.LinkedList, backed by std::list.
///
/// Doubly linked, so every index-based access is a walk: get(0) and get(size-1)
/// are O(1) in Java's implementation and O(n) here, but the *asymptotics of the
/// public API* are unchanged (Java's LinkedList.get is documented as O(n) too).
/// That is why the interface could be reused verbatim -- nothing in List<T>
/// promised random access.
///
/// Java's LinkedList also implements Deque, which is why it carries addFirst /
/// addLast / pollFirst / pollLast. Those are included; the full Queue and Deque
/// interfaces are not, so a LinkedList cannot be passed where a Deque is expected
/// (there is no Deque in this version at all). See DESIGN.md section 8.
template <class T>
class LinkedList : public List<T> {
public:
    using valueType = T;
    using Nodes = std::list<T>;

    // add(int, ...) and addAll(int, ...) are declared here, so the single-argument
    // overloads have to be re-exposed, exactly as in ArrayList.
    using List<T>::add;
    using List<T>::addAll;

    LinkedList() = default;

    /// Java: new LinkedList<>(collection).
    explicit LinkedList(const Collection<T>& source) { this->addAll(source); }

    explicit LinkedList(Nodes nodes) : nodes_(std::move(nodes)) {}

    LinkedList(std::initializer_list<T> values) : nodes_(values) {}

    [[nodiscard]] int size() const override { return static_cast<int>(nodes_.size()); }

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

    void clear() override {
        this->checkNotFrozen();
        ++this->modCount_;
        nodes_.clear();
    }

    /// Java: List.get(index). O(n), as Java documents for LinkedList.
    T& get(int index) override { return *nodeAt(checkedIndex(index)); }

    [[nodiscard]] const T& get(int index) const override { return *nodeAt(checkedIndex(index)); }

    T set(int index, const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        auto position = nodeAt(checkedIndex(index));
        T previous = std::move(*position);
        *position = element;
        return previous;
    }

    T set(int index, T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        auto position = nodeAt(checkedIndex(index));
        T previous = std::move(*position);
        *position = std::move(element);
        return previous;
    }

    void add(int index, const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        nodes_.insert(nodeAt(checkedInsertIndex(index)), element);
        ++this->modCount_;
    }

    void add(int index, T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        nodes_.insert(nodeAt(checkedInsertIndex(index)), std::move(element));
        ++this->modCount_;
    }

    T removeAt(int index) override {
        this->checkNotFrozen();
        auto position = nodeAt(checkedIndex(index));
        T removed = std::move(*position);
        nodes_.erase(position);
        ++this->modCount_;
        return removed;
    }

    [[nodiscard]] int indexOf(const T& element) const override {
        int index = 0;
        for (const T& candidate : nodes_) {
            if (elementEquals(candidate, element)) {
                return index;
            }
            ++index;
        }
        return -1;
    }

    [[nodiscard]] int lastIndexOf(const T& element) const override {
        int found = -1;
        int index = 0;
        for (const T& candidate : nodes_) {
            if (elementEquals(candidate, element)) {
                found = index;
            }
            ++index;
        }
        return found;
    }

    /// Java: List.subList(from, to). A live view onto the nodes, not a copy.
    [[nodiscard]] std::unique_ptr<ListView<T>> subList(int fromIndex, int toIndex) override {
        return std::make_unique<ListView<T>>(*this, fromIndex, toIndex);
    }

    [[nodiscard]] std::unique_ptr<ListView<T>> subList(
        int fromIndex, int toIndex) const override {
        return std::make_unique<ListView<T>>(std::as_const(*this), fromIndex, toIndex);
    }

    bool addAll(int index, const Collection<T>& other) override {
        this->checkNotFrozen();
        auto position = nodeAt(checkedInsertIndex(index));
        bool changed = false;
        for (const T& candidate : other) {
            this->requireNonNull(candidate);
            position = nodes_.insert(position, candidate);
            ++position;
            changed = true;
        }
        if (changed) {
            ++this->modCount_;
        }
        return changed;
    }

    /// Java: List.sort(comparator). std::list::sort is a stable merge sort, which
    /// is what Java promises for List.sort.
    void sort(const Comparator<T>& comparator) override {
        this->checkNotFrozen();
        nodes_.sort([&comparator](const T& left, const T& right) {
            return comparator.compare(left, right) < 0;
        });
        ++this->modCount_;
    }

    [[nodiscard]] std::unique_ptr<Iterator<T>> iterator() override {
        return std::make_unique<LinkedListIterator>(*this, 0);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> constIterator() const override {
        return std::make_unique<LinkedListConstIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<ListIterator<T>> listIterator() override {
        return std::make_unique<LinkedListIterator>(*this, 0);
    }

    [[nodiscard]] std::unique_ptr<ListIterator<T>> listIterator(int index) override {
        return std::make_unique<LinkedListIterator>(*this, checkedInsertIndex(index));
    }

    // --- Deque-shaped convenience operations --------------------------------

    /// Java: Deque.addFirst(element).
    void addFirst(const T& element) {
        this->checkNotFrozen();
        this->requireNonNull(element);
        nodes_.push_front(element);
        ++this->modCount_;
    }

    /// Java: Deque.addLast(element). Same as add(element).
    void addLast(const T& element) { this->add(element); }

    /// Java: Deque.getFirst().
    [[nodiscard]] const T& getFirst() const {
        requireNotEmpty();
        return nodes_.front();
    }

    /// Java: Deque.getLast().
    [[nodiscard]] const T& getLast() const {
        requireNotEmpty();
        return nodes_.back();
    }

    /// Java: Deque.removeFirst().
    T removeFirst() {
        this->checkNotFrozen();
        requireNotEmpty();
        T removed = std::move(nodes_.front());
        nodes_.pop_front();
        ++this->modCount_;
        return removed;
    }

    /// Java: Deque.removeLast().
    T removeLast() {
        this->checkNotFrozen();
        requireNotEmpty();
        T removed = std::move(nodes_.back());
        nodes_.pop_back();
        ++this->modCount_;
        return removed;
    }

    /// Java: Deque.peekFirst(), returning null when empty.
    [[nodiscard]] Optional<T> peekFirst() const {
        if (nodes_.empty()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(nodes_.front());
    }

    /// Java: Deque.peekLast().
    [[nodiscard]] Optional<T> peekLast() const {
        if (nodes_.empty()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(nodes_.back());
    }

    /// Java: Deque.pollFirst(), which removes rather than peeks.
    [[nodiscard]] Optional<T> pollFirst() {
        if (nodes_.empty()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(removeFirst());
    }

    /// Java: Deque.pollLast().
    [[nodiscard]] Optional<T> pollLast() {
        if (nodes_.empty()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(removeLast());
    }

    /// Read-only iterator over the nodes.
    class LinkedListConstIterator final : public Iterator<const T> {
    public:
        explicit LinkedListConstIterator(const LinkedList& owner)
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
        const LinkedList* owner_;
        Nodes::const_iterator current_;
        int expectedModCount_;
    };

    /// Backs both iterator() and listIterator(), mirroring Java's ListItr.
    ///
    /// The index is carried alongside the node iterator rather than recomputed:
    /// Java's LinkedList.ListItr does the same, because std::distance on a list is
    /// O(n) and nextIndex() must be O(1).
    class LinkedListIterator final : public ListIterator<T> {
    public:
        LinkedListIterator(LinkedList& owner, int index)
            : owner_(&owner), current_(std::next(owner.nodes_.begin(), index)), index_(index),
              expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return index_ < owner_->size(); }

        T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (index_ >= owner_->size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = current_;
            lastIndex_ = index_;
            hasLast_ = true;
            ++index_;
            return *current_++;
        }

        [[nodiscard]] bool hasPrevious() const override { return index_ > 0; }

        T& previous() override {
            owner_->checkForComodification(expectedModCount_);
            if (index_ <= 0) {
                throw NoSuchElementException("no previous element");
            }
            current_ = std::prev(current_);
            --index_;
            lastReturned_ = current_;
            lastIndex_ = index_;
            hasLast_ = true;
            return *current_;
        }

        [[nodiscard]] int nextIndex() const override { return index_; }

        [[nodiscard]] int previousIndex() const override { return index_ - 1; }

        void remove() override {
            owner_->checkNotFrozen();
            owner_->checkForComodification(expectedModCount_);
            if (!hasLast_) {
                throw IllegalStateException("remove() called before next() or previous()");
            }
            // Java's ListItr: when the cursor sits on the element being removed the
            // cursor moves to its successor; otherwise only the index shifts back.
            const bool cursorOnLastReturned = current_ == lastReturned_;
            auto successor = std::next(lastReturned_);
            owner_->nodes_.erase(lastReturned_);
            if (cursorOnLastReturned) {
                current_ = successor;
            } else {
                index_ = lastIndex_;
            }
            hasLast_ = false;
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
        }

        void set(const T& element) override {
            owner_->checkNotFrozen();
            LinkedList<T>::requireNonNull(element);
            if (!hasLast_) {
                throw IllegalStateException("set() called before next() or previous()");
            }
            *lastReturned_ = element;
        }

        void set(T&& element) override {
            owner_->checkNotFrozen();
            LinkedList<T>::requireNonNull(element);
            if (!hasLast_) {
                throw IllegalStateException("set() called before next() or previous()");
            }
            *lastReturned_ = std::move(element);
        }

        void add(const T& element) override {
            owner_->checkNotFrozen();
            LinkedList<T>::requireNonNull(element);
            insertAtCursor(element);
        }

        void add(T&& element) override {
            owner_->checkNotFrozen();
            LinkedList<T>::requireNonNull(element);
            insertAtCursor(std::move(element));
        }

    private:
        template <class U>
        void insertAtCursor(U&& element) {
            // insert() leaves the existing cursor node after the new element, so
            // current_ needs no repair; only the index and the fail-fast counter do.
            owner_->nodes_.insert(current_, std::forward<U>(element));
            ++index_;
            hasLast_ = false;
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
        }

        LinkedList* owner_;
        Nodes::iterator current_;
        int index_ = 0;
        Nodes::iterator lastReturned_;
        int lastIndex_ = 0;
        bool hasLast_ = false;
        int expectedModCount_;
    };

private:
    [[nodiscard]] Nodes::iterator nodeAt(int index) { return std::next(nodes_.begin(), index); }

    [[nodiscard]] Nodes::const_iterator nodeAt(int index) const {
        return std::next(nodes_.begin(), index);
    }

    /// Validated indices come back as int, not size_t: a std::list is walked by
    /// std::next, which counts in difference_type, and size() is already an int.
    [[nodiscard]] int checkedIndex(int index) const {
        if (index < 0 || index >= size()) {
            throw IndexOutOfBoundsException("Index " + std::to_string(index) +
                                            " out of bounds for length " + std::to_string(size()));
        }
        return index;
    }

    [[nodiscard]] int checkedInsertIndex(int index) const {
        if (index < 0 || index > size()) {
            throw IndexOutOfBoundsException("Index: " + std::to_string(index) +
                                            ", Size: " + std::to_string(size()));
        }
        return index;
    }

    void requireNotEmpty() const {
        if (nodes_.empty()) {
            throw NoSuchElementException("list is empty");
        }
    }

    Nodes nodes_;
};

}  // namespace cppstream
