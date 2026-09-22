#pragma once

#include <cppstream/Collection.h>
#include <cppstream/Elements.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace cppstream {

/// A port of java.util.AbstractCollection: the shared algorithms, written once in
/// terms of the iterator and `size`, so that every container only supplies those.
///
/// One structural difference from Java: Java's modCount lives in each concrete
/// class (ArrayList has its own, HashMap has its own). Hoisting it here lets the
/// whole container layer share a single fail-fast implementation, and lets
/// AbstractCollection's own algorithms -- removeAll, retainAll, clear -- be
/// fail-fast for free.
template <class T>
class AbstractCollection : public Collection<T> {
public:
    // Every unqualified call below needs `this->`: size/iterator/constIterator are
    // members of the dependent base Collection<T>, so ordinary lookup at template
    // definition time cannot find them.
    [[nodiscard]] bool isEmpty() const override { return this->size() == 0; }

    [[nodiscard]] bool contains(const T& element) const override {
        for (const T& candidate : *this) {
            if (elementEquals(candidate, element)) {
                return true;
            }
        }
        return false;
    }

    bool remove(const T& element) override {
        std::unique_ptr<Iterator<T>> iter = this->iterator();
        while (iter->hasNext()) {
            if (elementEquals(iter->next(), element)) {
                iter->remove();
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool containsAll(const Collection<T>& other) const override {
        for (const T& candidate : other) {
            if (!contains(candidate)) {
                return false;
            }
        }
        return true;
    }

    bool addAll(const Collection<T>& other) override {
        bool changed = false;
        for (const T& candidate : other) {
            if (this->add(candidate)) {
                changed = true;
            }
        }
        return changed;
    }

    bool removeAll(const Collection<T>& other) override {
        bool changed = false;
        std::unique_ptr<Iterator<T>> iter = this->iterator();
        while (iter->hasNext()) {
            if (other.contains(iter->next())) {
                iter->remove();
                changed = true;
            }
        }
        return changed;
    }

    bool retainAll(const Collection<T>& other) override {
        bool changed = false;
        std::unique_ptr<Iterator<T>> iter = this->iterator();
        while (iter->hasNext()) {
            if (!other.contains(iter->next())) {
                iter->remove();
                changed = true;
            }
        }
        return changed;
    }

    void clear() override {
        std::unique_ptr<Iterator<T>> iter = this->iterator();
        while (iter->hasNext()) {
            iter->next();
            iter->remove();
        }
    }

    [[nodiscard]] std::vector<T> toArray() const override {
        std::vector<T> result;
        result.reserve(static_cast<std::size_t>(this->size()));
        for (const T& candidate : *this) {
            result.push_back(candidate);
        }
        return result;
    }

    /// Lazily walks the container through the read-only iterator. Each element is
    /// copied, so streaming requires T to be copy-constructible -- unavoidable for
    /// a pipeline whose stage signature yields T by value.
    [[nodiscard]] Stream<T> stream() const override {
        // The container knows its size, so it passes it on: this is the hint that
        // lets stream().toList() and stream().sorted() preallocate. See
        // Stream::SizeHint.
        return Stream<T>(typename Stream<T>::NextFn(
            [iter = this->constIterator()]() mutable -> std::optional<T> {
                if (!iter->hasNext()) {
                    return std::nullopt;
                }
                return std::optional<T>(std::in_place, iter->next());
            }), static_cast<std::size_t>(this->size()));
    }

    [[nodiscard]] bool equals(const Collection<T>& other) const override {
        if (static_cast<const Collection<T>*>(this) == std::addressof(other)) {
            return true;
        }
        if (this->size() != other.size()) {
            return false;
        }
        std::unique_ptr<Iterator<const T>> left = this->constIterator();
        std::unique_ptr<Iterator<const T>> right = other.constIterator();
        while (left->hasNext()) {
            if (!elementEquals(left->next(), right->next())) {
                return false;
            }
        }
        return true;
    }

    /// Java: AbstractList.hashCode() -- the ordered 31* fold, not a plain sum.
    /// AbstractSet overrides this with the order-insensitive sum, which is what
    /// the matching AbstractSet.hashCode() in Java does.
    [[nodiscard]] std::size_t hashCode() const override {
        std::size_t result = 1;
        for (const T& candidate : *this) {
            result = 31 * result + elementHash(candidate);
        }
        return result;
    }

    /// Java 8: Collection.removeIf(predicate).
    ///
    /// Java declares this as a default method on the interface. A member template
    /// cannot be virtual, so it lives here instead; consequently it is available on
    /// concrete containers but not through a `const Collection<T>&`.
    template <class P>
    bool removeIf(P predicate) {
        bool changed = false;
        std::unique_ptr<Iterator<T>> iter = this->iterator();
        while (iter->hasNext()) {
            if (std::invoke(predicate, iter->next())) {
                iter->remove();
                changed = true;
            }
        }
        return changed;
    }

    /// C++-only: renders the container unmodifiable in place.
    ///
    /// Java spells this Collections.unmodifiableList(list) and returns a wrapper.
    /// A flag on the container is cheaper and, more importantly, preserves the
    /// concrete type -- a value-returning function cannot return the abstract
    /// List (section 5.2), so Stream.toList() and Collectors.toUnmodifiableList()
    /// have to hand back an ArrayList<T> that is already frozen. See DESIGN.md
    /// divergence 26.
    void freeze() noexcept { frozen_ = true; }

    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

    /// C++-only: the structural-modification count, as observed.
    ///
    /// Java keeps modCount private, which works because ArrayList.SubList is
    /// written against the same object and may read it. A view here holds a
    /// reference to a *different* object, and C++ protected-member rules forbid
    /// reading a sibling object's protected member, so one const accessor is the
    /// cheapest way to keep view iterators fail-fast. See ListView in List.h.
    ///
    /// Virtual so that a live range view can answer with the count of what it is a
    /// window onto instead of its own: iterating TreeSetRangeView is iterating the
    /// backing tree, so a structural change to that tree has to invalidate the
    /// iterator no matter which object the caller happens to hold.
    [[nodiscard]] virtual int modCount() const noexcept { return modCount_; }

    /// Throws if freeze() has been called. Every mutator calls this first.
    void checkNotFrozen() const {
        if (frozen_) {
            throw UnsupportedOperationException("this collection is unmodifiable");
        }
    }

protected:
    /// Structural-modification counter. Incremented by every operation that
    /// changes the element count; iterators snapshot it and compare on each step.
    int modCount_ = 0;

    /// Whether freeze() has been called. Shared by the whole container layer for
    /// the same reason modCount_ is.
    bool frozen_ = false;

    /// Fails fast when the container changed underneath an iterator. Java throws
    /// this with a null message; a descriptive one is a small, deliberate upgrade.
    void checkForComodification(int expectedModCount) const {
        if (modCount_ != expectedModCount) {
            throw ConcurrentModificationException("collection modified during iteration");
        }
    }

    /// Rejects null for pointer element types. Value types cannot be null, so this
    /// compiles away to nothing for them. A thin forwarder onto the free function
    /// in Elements.h so that the Map layer can share it without inheriting from
    /// AbstractCollection.
    static void requireNonNull(const T& element) { cppstream::requireNonNull(element); }
};

}  // namespace cppstream
