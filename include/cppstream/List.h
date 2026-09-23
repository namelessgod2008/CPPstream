#pragma once

#include <cppstream/AbstractCollection.h>
#include <cppstream/Collection.h>
#include <cppstream/Comparator.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cppstream {

template <class T>
class ArrayList;

template <class T>
class ListView;

/// A port of java.util.ListIterator, extending Iterator with backwards traversal
/// and in-place update.
template <class T>
class ListIterator : public Iterator<T> {
public:
    [[nodiscard]] virtual bool hasPrevious() const = 0;
    virtual T& previous() = 0;
    [[nodiscard]] virtual int nextIndex() const = 0;
    [[nodiscard]] virtual int previousIndex() const = 0;
    virtual void add(const T& element) = 0;
    virtual void add(T&& element) = 0;
    virtual void set(const T& element) = 0;
    virtual void set(T&& element) = 0;
};

/// A port of java.util.List.
///
/// Deliberate divergence: Java overloads remove(int index) and
/// remove(Object element). In C++ those collide whenever T is int, because
/// `remove(3)` is then ambiguous. The index form is therefore named removeAt.
/// See DESIGN.md section 8, item 3.
///
/// List derives from AbstractCollection rather than from Collection directly.
/// Java splits these into AbstractCollection and AbstractList; C++ needs a single
/// inheritance chain, otherwise ArrayList : List, AbstractCollection would carry
/// two Collection subobjects and every member lookup would be ambiguous.
template <class T>
class List : public AbstractCollection<T> {
public:
    using valueType = T;

    // List declares add(int, ...) and addAll(int, ...). Any declaration of a name
    // in a derived class hides every base-class overload of that name, so the
    // single-argument forms inherited from Collection/AbstractCollection must be
    // pulled back into scope or `list.add(x)` would stop compiling for good.
    using Collection<T>::add;
    using AbstractCollection<T>::addAll;

    /// Java: List.get(index).
    ///
    /// Deliberately not [[nodiscard]]: `get` hands out a reference and mutating
    /// through it is a normal use, so discarding the result is not a mistake.
    virtual T& get(int index) = 0;
    [[nodiscard]] virtual const T& get(int index) const = 0;

    /// Java: List.set(index, element). Returns the element it replaced.
    virtual T set(int index, const T& element) = 0;
    virtual T set(int index, T&& element) = 0;

    /// Java: List.add(index, element).
    virtual void add(int index, const T& element) = 0;
    virtual void add(int index, T&& element) = 0;

    /// Java: List.remove(index). Renamed, see the class comment.
    virtual T removeAt(int index) = 0;

    [[nodiscard]] virtual int indexOf(const T& element) const = 0;
    [[nodiscard]] virtual int lastIndexOf(const T& element) const = 0;

    [[nodiscard]] virtual std::unique_ptr<ListIterator<T>> listIterator() = 0;
    [[nodiscard]] virtual std::unique_ptr<ListIterator<T>> listIterator(int index) = 0;

    /// Java: List.subList(from, to), which returns a live view onto this list.
    ///
    /// The returned ListView delegates every operation back to this list with an
    /// index offset, so writes through the view are writes to the list. The two
    /// overloads split on constness: the const one hands back a view that rejects
    /// every mutator, because a const list has nothing to write through to.
    ///
    /// Read-only-ness is inherited: subList of a read-only view is a read-only view
    /// rather than an error, exactly as Collections.unmodifiableList(l).subList(a, b)
    /// reads. The same is true of the map views and of the ordered range views.
    ///
    /// Returned by pointer because ListView is a view, not a value: copying it
    /// would copy a reference to the same window. See DESIGN.md section 8, item 11.
    [[nodiscard]] virtual std::unique_ptr<ListView<T>> subList(int fromIndex, int toIndex) = 0;
    [[nodiscard]] virtual std::unique_ptr<ListView<T>> subList(
        int fromIndex, int toIndex) const = 0;

    /// Java: List.addAll(index, other).
    virtual bool addAll(int index, const Collection<T>& other) = 0;

    /// Java: List.sort(comparator).
    virtual void sort(const Comparator<T>& comparator) = 0;
};

/// A port of the view that java.util.AbstractList.subList returns.
///
/// Java hands out a live window onto the backing list; this is that window. Every
/// operation is delegated to the backing list with an index offset, so a write
/// through the view is a write to the list, and a structural change made through
/// the view resizes the window exactly as Java's SubList does. Divergence 11 --
/// "subList copies" -- is therefore gone.
///
/// Two things a view cannot enforce, only document, precisely as with iterators
/// (divergence 17):
///
///   * the view must not outlive the list it was taken from;
///   * a structural change made *directly* on the backing list invalidates the
///     window. The view detects that the way Java's SubList does -- by comparing
///     the backing list's modification count against the value it last observed --
///     and throws ConcurrentModificationException. size() and get() alone cannot
///     detect it, which is why Java documents the same hole.
///
/// A view taken from a const list is read-only. Its mutators throw
/// UnsupportedOperationException, and so does iterator(), because Iterator<T>
/// hands out mutable element references that a const list cannot supply.
/// constIterator() and range-for work as usual.
template <class T>
class ListView : public List<T> {
public:
    using valueType = T;
    using List<T>::add;
    using List<T>::addAll;

    /// Java: AbstractList.subList(fromIndex, toIndex) on a mutable list.
    ListView(List<T>& backing, int fromIndex, int toIndex)
        : backing_(&backing), writableBacking_(&backing), offset_(fromIndex),
          length_(toIndex - fromIndex) {
        validate(backing.size(), fromIndex, toIndex);
    }

    /// The read-only form, built by the const overload of List::subList.
    ListView(const List<T>& backing, int fromIndex, int toIndex)
        : backing_(&backing), offset_(fromIndex), length_(toIndex - fromIndex) {
        validate(backing.size(), fromIndex, toIndex);
    }

    /// Reports the backing list's count rather than the view's own: iterating this
    /// view is iterating that list, so any structural change to the list has to
    /// invalidate the iterator. See AbstractCollection::modCount().
    [[nodiscard]] int modCount() const noexcept override { return backingModCount(); }

    [[nodiscard]] int size() const override { return length_; }

    T& get(int index) override { return writable().get(offset_ + checkedIndex(index)); }

    [[nodiscard]] const T& get(int index) const override {
        return backing_->get(offset_ + checkedIndex(index));
    }

    T set(int index, const T& element) override {
        this->checkNotFrozen();
        return writable().set(offset_ + checkedIndex(index), element);
    }

    T set(int index, T&& element) override {
        this->checkNotFrozen();
        return writable().set(offset_ + checkedIndex(index), std::move(element));
    }

    bool add(const T& element) override { return insertAt(length_, element); }

    bool add(T&& element) override { return insertAt(length_, std::move(element)); }

    void add(int index, const T& element) override { static_cast<void>(insertAt(index, element)); }

    void add(int index, T&& element) override {
        static_cast<void>(insertAt(index, std::move(element)));
    }

    T removeAt(int index) override {
        this->checkNotFrozen();
        T removed = writable().removeAt(offset_ + checkedIndex(index));
        --length_;
        return removed;
    }

    bool remove(const T& element) override {
        this->checkNotFrozen();
        const int found = indexOf(element);
        if (found < 0) {
            return false;
        }
        static_cast<void>(removeAt(found));
        return true;
    }

    void clear() override {
        this->checkNotFrozen();
        List<T>& target = writable();
        // Back to front, so that every intermediate removal stays inside the window.
        for (int remaining = length_; remaining > 0; --remaining) {
            static_cast<void>(target.removeAt(offset_ + remaining - 1));
        }
        length_ = 0;
    }

    [[nodiscard]] int indexOf(const T& element) const override {
        for (int position = 0; position < length_; ++position) {
            if (elementEquals(backing_->get(offset_ + position), element)) {
                return position;
            }
        }
        return -1;
    }

    [[nodiscard]] int lastIndexOf(const T& element) const override {
        for (int position = length_; position > 0; --position) {
            if (elementEquals(backing_->get(offset_ + position - 1), element)) {
                return position - 1;
            }
        }
        return -1;
    }

    bool addAll(int index, const Collection<T>& other) override {
        this->checkNotFrozen();
        const int position = checkedInsertIndex(index);
        const std::vector<T> incoming = other.toArray();
        if (incoming.empty()) {
            return false;
        }
        List<T>& target = writable();
        for (std::size_t step = 0; step < incoming.size(); ++step) {
            target.add(offset_ + position + static_cast<int>(step), incoming[step]);
        }
        length_ += static_cast<int>(incoming.size());
        return true;
    }

    void sort(const Comparator<T>& comparator) override {
        this->checkNotFrozen();
        List<T>& target = writable();
        std::vector<T> window;
        window.reserve(static_cast<std::size_t>(length_));
        for (int position = 0; position < length_; ++position) {
            window.push_back(backing_->get(offset_ + position));
        }
        std::sort(window.begin(), window.end(), [&comparator](const T& left, const T& right) {
            return comparator.compare(left, right) < 0;
        });
        for (int position = 0; position < length_; ++position) {
            static_cast<void>(
                target.set(offset_ + position, window[static_cast<std::size_t>(position)]));
        }
    }

    [[nodiscard]] std::unique_ptr<ListView<T>> subList(int fromIndex, int toIndex) override {
        checkSubRange(fromIndex, toIndex);
        return makeSubWindow(offset_ + fromIndex, offset_ + toIndex);
    }

    [[nodiscard]] std::unique_ptr<ListView<T>> subList(int fromIndex, int toIndex) const override {
        checkSubRange(fromIndex, toIndex);
        return std::make_unique<ListView<T>>(*backing_, offset_ + fromIndex, offset_ + toIndex);
    }

    [[nodiscard]] std::unique_ptr<Iterator<T>> iterator() override {
        // A read-only view has no mutable backing to hand out references to.
        static_cast<void>(writable());
        return std::make_unique<ListViewIterator>(*this, 0);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> constIterator() const override {
        return std::make_unique<ListViewConstIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<ListIterator<T>> listIterator() override {
        return std::make_unique<ListViewIterator>(*this, 0);
    }

    [[nodiscard]] std::unique_ptr<ListIterator<T>> listIterator(int index) override {
        if (index < 0 || index > length_) {
            throw IndexOutOfBoundsException(
                "Index: " + std::to_string(index) + ", Size: " + std::to_string(length_));
        }
        return std::make_unique<ListViewIterator>(*this, index);
    }

    class ListViewConstIterator final : public Iterator<const T> {
    public:
        explicit ListViewConstIterator(const ListView& owner)
            : owner_(&owner), expectedModCount_(owner.backingModCount()) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < owner_->length_; }

        const T& next() override {
            owner_->checkBacking(expectedModCount_);
            if (cursor_ >= owner_->length_) {
                throw NoSuchElementException("iterator exhausted");
            }
            return owner_->backing_->get(owner_->offset_ + cursor_++);
        }

    private:
        const ListView* owner_;
        int cursor_ = 0;
        int expectedModCount_;
    };

    /// Backs iterator() and listIterator(). Writes go through the view, so they
    /// resize the window and resynchronise both counters.
    class ListViewIterator final : public ListIterator<T> {
    public:
        ListViewIterator(ListView& owner, int index)
            : owner_(&owner), cursor_(index), expectedModCount_(owner.backingModCount()) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < owner_->length_; }

        T& next() override {
            owner_->checkBacking(expectedModCount_);
            if (cursor_ >= owner_->length_) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = cursor_;
            return owner_->writable().get(owner_->offset_ + cursor_++);
        }

        [[nodiscard]] bool hasPrevious() const override { return cursor_ > 0; }

        T& previous() override {
            owner_->checkBacking(expectedModCount_);
            if (cursor_ <= 0) {
                throw NoSuchElementException("no previous element");
            }
            lastReturned_ = cursor_ - 1;
            return owner_->writable().get(owner_->offset_ + --cursor_);
        }

        [[nodiscard]] int nextIndex() const override { return cursor_; }

        [[nodiscard]] int previousIndex() const override { return cursor_ - 1; }

        void remove() override {
            owner_->checkNotFrozen();
            owner_->checkBacking(expectedModCount_);
            if (lastReturned_ < 0) {
                throw IllegalStateException("remove() called before next() or previous()");
            }
            cursor_ = lastReturned_;
            static_cast<void>(owner_->removeAt(lastReturned_));
            lastReturned_ = -1;
            expectedModCount_ = owner_->backingModCount();
        }

        void set(const T& element) override {
            owner_->checkNotFrozen();
            if (lastReturned_ < 0) {
                throw IllegalStateException("set() called before next() or previous()");
            }
            static_cast<void>(owner_->set(lastReturned_, element));
        }

        void set(T&& element) override {
            owner_->checkNotFrozen();
            if (lastReturned_ < 0) {
                throw IllegalStateException("set() called before next() or previous()");
            }
            static_cast<void>(owner_->set(lastReturned_, std::move(element)));
        }

        void add(const T& element) override {
            owner_->checkNotFrozen();
            static_cast<void>(owner_->insertAt(cursor_, element));
            ++cursor_;
            lastReturned_ = -1;
            expectedModCount_ = owner_->backingModCount();
        }

        void add(T&& element) override {
            owner_->checkNotFrozen();
            static_cast<void>(owner_->insertAt(cursor_, std::move(element)));
            ++cursor_;
            lastReturned_ = -1;
            expectedModCount_ = owner_->backingModCount();
        }

    private:
        ListView* owner_;
        int cursor_ = 0;
        int lastReturned_ = -1;
        int expectedModCount_;
    };

private:
    template <class U>
    bool insertAt(int index, U&& element) {
        this->checkNotFrozen();
        const int position = checkedInsertIndex(index);
        writable().add(offset_ + position, std::forward<U>(element));
        ++length_;
        return true;
    }

    /// A window onto this window. Which backing to carry is the whole decision:
    /// a read-only view only ever produces read-only views, so
    /// `unmodifiableList(list).subList(a, b)` reads rather than throws, exactly as
    /// Java's wrapper does. The bounds are already relative to this window.
    [[nodiscard]] std::unique_ptr<ListView<T>> makeSubWindow(int fromIndex, int toIndex) const {
        if (writableBacking_ == nullptr) {
            return std::make_unique<ListView<T>>(*backing_, fromIndex, toIndex);
        }
        return std::make_unique<ListView<T>>(*writableBacking_, fromIndex, toIndex);
    }

    /// The one place a const view is rejected. Everything writable funnels here.
    [[nodiscard]] List<T>& writable() const {
        if (writableBacking_ == nullptr) {
            throw UnsupportedOperationException(
                "this subList is read-only: it was taken from a const list");
        }
        return *writableBacking_;
    }

    [[nodiscard]] int backingModCount() const { return backing_->modCount(); }

    void checkBacking(int expectedModCount) const {
        if (backingModCount() != expectedModCount) {
            throw ConcurrentModificationException("backing list modified during iteration");
        }
    }

    [[nodiscard]] int checkedIndex(int index) const {
        if (index < 0 || index >= length_) {
            throw IndexOutOfBoundsException("Index " + std::to_string(index) +
                                            " out of bounds for length " + std::to_string(length_));
        }
        return index;
    }

    [[nodiscard]] int checkedInsertIndex(int index) const {
        if (index < 0 || index > length_) {
            throw IndexOutOfBoundsException(
                "Index: " + std::to_string(index) + ", Size: " + std::to_string(length_));
        }
        return index;
    }

    void checkSubRange(int fromIndex, int toIndex) const {
        validate(length_, fromIndex, toIndex);
    }

    static void validate(int bound, int fromIndex, int toIndex) {
        if (fromIndex < 0 || toIndex > bound || fromIndex > toIndex) {
            throw IndexOutOfBoundsException("fromIndex: " + std::to_string(fromIndex) +
                                            ", toIndex: " + std::to_string(toIndex) +
                                            ", size: " + std::to_string(bound));
        }
    }

    const List<T>* backing_;
    /// Null for a view of a const list; every mutator checks it.
    List<T>* writableBacking_ = nullptr;
    int offset_ = 0;
    int length_ = 0;
};

}  // namespace cppstream
