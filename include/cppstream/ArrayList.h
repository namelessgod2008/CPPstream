#pragma once

#include <cppstream/Elements.h>
#include <cppstream/List.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cppstream {

/// A port of java.util.ArrayList, backed by std::vector.
///
/// Only size/add/iterator/constIterator are needed for the inherited algorithms to
/// work; everything else is overridden purely for efficiency or for Java's exact
/// error messages.
///
/// Braced initialisation follows std::vector's convention, not Java's:
/// `ArrayList<int> a{5}` is a one-element list, while `ArrayList<int> a(5)` is a
/// capacity hint. Java avoids the ambiguity because `new ArrayList<>(5)` resolves
/// to the int constructor; in C++ an initializer_list constructor wins braced
/// initialisation.
template <class T>
class ArrayList : public List<T> {
public:
    using valueType = T;

    // add(int, ...) and addAll(int, ...) are declared here too, so the
    // single-argument overloads have to be re-exposed explicitly.
    using List<T>::add;
    using List<T>::addAll;

    ArrayList() = default;

    /// Java: new ArrayList<>(initialCapacity).
    explicit ArrayList(int initialCapacity) {
        if (initialCapacity < 0) {
            throw IllegalArgumentException("Illegal Capacity: " + std::to_string(initialCapacity));
        }
        values_.reserve(static_cast<std::size_t>(initialCapacity));
    }

    /// Java: new ArrayList<>(collection).
    explicit ArrayList(const Collection<T>& source) { this->addAll(source); }

    explicit ArrayList(std::vector<T> values) : values_(std::move(values)) {}

    ArrayList(std::initializer_list<T> values) : values_(values) {}

    [[nodiscard]] int size() const override { return static_cast<int>(values_.size()); }

    bool add(const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        values_.push_back(element);
        ++this->modCount_;
        return true;
    }

    bool add(T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        values_.push_back(std::move(element));
        ++this->modCount_;
        return true;
    }

    void clear() override {
        this->checkNotFrozen();
        ++this->modCount_;
        values_.clear();
    }

    T& get(int index) override { return values_[checkedIndex(index)]; }

    [[nodiscard]] const T& get(int index) const override { return values_[checkedIndex(index)]; }

    T set(int index, const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        T& slot = values_[checkedIndex(index)];
        T previous = std::move(slot);
        slot = element;
        return previous;
    }

    T set(int index, T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        T& slot = values_[checkedIndex(index)];
        T previous = std::move(slot);
        slot = std::move(element);
        return previous;
    }

    void add(int index, const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        const std::size_t position = checkedInsertIndex(index);
        values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(position), element);
        ++this->modCount_;
    }

    void add(int index, T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        const std::size_t position = checkedInsertIndex(index);
        values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(position), std::move(element));
        ++this->modCount_;
    }

    T removeAt(int index) override {
        this->checkNotFrozen();
        const std::size_t position = checkedIndex(index);
        T removed = std::move(values_[position]);
        values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(position));
        ++this->modCount_;
        return removed;
    }

    [[nodiscard]] int indexOf(const T& element) const override {
        for (std::size_t position = 0; position < values_.size(); ++position) {
            if (elementEquals(values_[position], element)) {
                return static_cast<int>(position);
            }
        }
        return -1;
    }

    [[nodiscard]] int lastIndexOf(const T& element) const override {
        for (std::size_t position = values_.size(); position > 0; --position) {
            if (elementEquals(values_[position - 1], element)) {
                return static_cast<int>(position - 1);
            }
        }
        return -1;
    }

    /// Java: List.subList(from, to). A live view, not a copy (see ListView).
    [[nodiscard]] std::unique_ptr<ListView<T>> subList(int fromIndex, int toIndex) override {
        return std::make_unique<ListView<T>>(*this, fromIndex, toIndex);
    }

    /// The const overload: a read-only view, because there is nothing to write
    /// through to.
    [[nodiscard]] std::unique_ptr<ListView<T>> subList(
        int fromIndex, int toIndex) const override {
        return std::make_unique<ListView<T>>(std::as_const(*this), fromIndex, toIndex);
    }

    bool addAll(int index, const Collection<T>& other) override {
        const std::size_t position = checkedInsertIndex(index);
        std::vector<T> incoming = other.toArray();
        if (incoming.empty()) {
            return false;
        }
        values_.insert(values_.begin() + static_cast<std::ptrdiff_t>(position), incoming.begin(),
            incoming.end());
        ++this->modCount_;
        return true;
    }

    void sort(const Comparator<T>& comparator) override {
        std::sort(values_.begin(), values_.end(),
            [&comparator](const T& left, const T& right) {
                return comparator.compare(left, right) < 0;
            });
        ++this->modCount_;
    }

    [[nodiscard]] std::unique_ptr<Iterator<T>> iterator() override {
        return std::make_unique<ArrayListIterator>(*this, 0);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> constIterator() const override {
        return std::make_unique<ArrayListConstIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<ListIterator<T>> listIterator() override {
        return std::make_unique<ArrayListIterator>(*this, 0);
    }

    [[nodiscard]] std::unique_ptr<ListIterator<T>> listIterator(int index) override {
        if (index < 0 || index > size()) {
            throw IndexOutOfBoundsException("Index: " + std::to_string(index) +
                                            ", Size: " + std::to_string(size()));
        }
        return std::make_unique<ArrayListIterator>(*this, index);
    }

private:
    /// Read-only iterator over a const list. Because Iterator<const T>::next()
    /// returns `const T&`, remove() is left to the base class default that throws
    /// UnsupportedOperationException -- no extra code, and no way to mutate a const
    /// container.
    class ArrayListConstIterator final : public Iterator<const T> {
    public:
        explicit ArrayListConstIterator(const ArrayList<T>& owner)
            : owner_(&owner), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < owner_->values_.size(); }

        const T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (cursor_ >= owner_->values_.size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            return owner_->values_[cursor_++];
        }

    private:
        const ArrayList<T>* owner_;
        std::size_t cursor_ = 0;
        int expectedModCount_;
    };

    /// Backs both iterator() and listIterator(), matching Java's Itr and ListItr.
    /// remove()/add() resynchronise expectedModCount so that AbstractCollection's
    /// removeAll/retainAll/clear can drive this iterator without tripping fail-fast.
    class ArrayListIterator final : public ListIterator<T> {
    public:
        ArrayListIterator(ArrayList<T>& owner, int index)
            : owner_(&owner), cursor_(index), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < owner_->size(); }

        T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (cursor_ >= owner_->size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = cursor_;
            return owner_->values_[static_cast<std::size_t>(cursor_++)];
        }

        [[nodiscard]] bool hasPrevious() const override { return cursor_ > 0; }

        T& previous() override {
            owner_->checkForComodification(expectedModCount_);
            if (cursor_ <= 0) {
                throw NoSuchElementException("no previous element");
            }
            lastReturned_ = cursor_ - 1;
            return owner_->values_[static_cast<std::size_t>(--cursor_)];
        }

        [[nodiscard]] int nextIndex() const override { return cursor_; }

        [[nodiscard]] int previousIndex() const override { return cursor_ - 1; }

        void remove() override {
            owner_->checkNotFrozen();
            owner_->checkForComodification(expectedModCount_);
            if (lastReturned_ < 0) {
                throw IllegalStateException("remove() called before next() or previous()");
            }
            eraseAt(lastReturned_);
            cursor_ = lastReturned_;
            lastReturned_ = -1;
        }

        void set(const T& element) override {
            owner_->checkNotFrozen();
            ArrayList<T>::requireNonNull(element);
            if (lastReturned_ < 0) {
                throw IllegalStateException("set() called before next() or previous()");
            }
            owner_->values_[static_cast<std::size_t>(lastReturned_)] = element;
        }

        void set(T&& element) override {
            owner_->checkNotFrozen();
            ArrayList<T>::requireNonNull(element);
            if (lastReturned_ < 0) {
                throw IllegalStateException("set() called before next() or previous()");
            }
            owner_->values_[static_cast<std::size_t>(lastReturned_)] = std::move(element);
        }

        void add(const T& element) override {
            owner_->checkNotFrozen();
            ArrayList<T>::requireNonNull(element);
            insertAt(cursor_, element);
        }

        void add(T&& element) override {
            owner_->checkNotFrozen();
            ArrayList<T>::requireNonNull(element);
            insertAt(cursor_, std::move(element));
        }

    private:
        template <class U>
        void insertAt(int index, U&& element) {
            owner_->values_.insert(owner_->values_.begin() + static_cast<std::ptrdiff_t>(index),
                std::forward<U>(element));
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
            ++cursor_;
            lastReturned_ = -1;
        }

        void eraseAt(int index) {
            owner_->values_.erase(owner_->values_.begin() + static_cast<std::ptrdiff_t>(index));
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
        }

        ArrayList<T>* owner_;
        int cursor_ = 0;
        int lastReturned_ = -1;
        int expectedModCount_;
    };

    [[nodiscard]] std::size_t checkedIndex(int index) const {
        if (index < 0 || index >= size()) {
            throw IndexOutOfBoundsException("Index " + std::to_string(index) +
                                            " out of bounds for length " + std::to_string(size()));
        }
        return static_cast<std::size_t>(index);
    }

    [[nodiscard]] std::size_t checkedInsertIndex(int index) const {
        if (index < 0 || index > size()) {
            throw IndexOutOfBoundsException("Index: " + std::to_string(index) +
                                            ", Size: " + std::to_string(size()));
        }
        return static_cast<std::size_t>(index);
    }

    std::vector<T> values_;
};

/// Java: Stream.toList().
///
/// Defined out of line because the declaration in Stream.h can only name the
/// return type, not use it: putting these few lines in Stream.h would close the
/// Stream <-> ArrayList include cycle (DESIGN.md section 7.4).
template <class T>
ArrayList<T> Stream<T>::toList() && {
    ArrayList<T> result(std::move(*this).toArray());
    result.freeze();
    return result;
}

}  // namespace cppstream
