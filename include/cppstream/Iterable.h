#pragma once

#include <cppstream/Iterator.h>

#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>

namespace cppstream {

/// Past-the-end marker for ReadOnlyIterator.
///
/// A separate type rather than an exhausted ReadOnlyIterator because
/// std::sentinel_for demands a semiregular sentinel, and ReadOnlyIterator is
/// deliberately move-only: it owns a single-pass cursor over a Java-style
/// Iterator, and copying it would silently produce two cursors over one position.
/// The empty sentinel is stateless, so end() is free of allocation.
struct ReadOnlySentinel {
    friend constexpr bool operator==(ReadOnlySentinel, ReadOnlySentinel) noexcept = default;
};

/// The C++ range-for facade over a CppStream container.
///
/// Java has no equivalent because it has no range-based for and no iterator
/// concepts. This adapts the Java iterator protocol to std::input_iterator:
/// single pass, move-only, and read-only -- operator* yields `const T&` so that
/// ranging over a const container cannot mutate it. That read-only guarantee is
/// exactly why the container layer keeps a separate const access path.
///
/// Java's Iterator has no notion of a "current" element (you must call next() to
/// get one), so this keeps a pointer to the element most recently pulled. The
/// pointer stays valid as long as the container does and is not structurally
/// modified, which fail-fast iterators enforce.
template <class T>
class ReadOnlyIterator {
public:
    using iterator_concept = std::input_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = const T*;
    using reference = const T&;

    /// The past-the-end value. All exhausted iterators compare equal to it.
    ReadOnlyIterator() noexcept = default;

    explicit ReadOnlyIterator(std::unique_ptr<Iterator<const T>> inner)
        : inner_(std::move(inner)) {
        advance();
    }

    ReadOnlyIterator(ReadOnlyIterator&&) noexcept = default;
    ReadOnlyIterator& operator=(ReadOnlyIterator&&) noexcept = default;
    ReadOnlyIterator(const ReadOnlyIterator&) = delete;
    ReadOnlyIterator& operator=(const ReadOnlyIterator&) = delete;
    ~ReadOnlyIterator() = default;

    [[nodiscard]] reference operator*() const noexcept { return *current_; }
    [[nodiscard]] pointer operator->() const noexcept { return current_; }

    ReadOnlyIterator& operator++() {
        advance();
        return *this;
    }

    void operator++(int) { advance(); }

    [[nodiscard]] friend bool operator==(
        const ReadOnlyIterator& left, const ReadOnlyIterator& right) noexcept {
        return left.inner_.get() == right.inner_.get();
    }

    /// Comparing against the sentinel is what drives range-for and the ranges
    /// algorithms. Only this direction is written; C++20 rewrites `sentinel == iter`
    /// and both `!=` forms from it.
    [[nodiscard]] friend bool operator==(
        const ReadOnlyIterator& iter, ReadOnlySentinel) noexcept {
        return iter.inner_.get() == nullptr;
    }

private:
    /// Pulls the next element, collapsing to the end sentinel once the underlying
    /// Java iterator is exhausted.
    void advance() {
        if (inner_ && inner_->hasNext()) {
            current_ = std::addressof(inner_->next());
            return;
        }
        inner_.reset();
        current_ = nullptr;
    }

    std::unique_ptr<Iterator<const T>> inner_;
    const T* current_ = nullptr;
};

/// A port of java.util.Iterable.
///
/// Two access paths rather than Java's one, because C++ has const and Java does
/// not:
///
///   * iterator()      -- non-const, yields `T&`, remove()-capable. The faithful
///                        translation of Iterable.iterator().
///   * constIterator() -- const, yields `const T&`, remove() throws. Backs
///                        begin()/end() and stream(), which are read-only.
///
/// The split exists because a single const iterator() returning `T&` would let a
/// genuinely const container be mutated through const_cast, which is undefined
/// behaviour rather than merely untidy (see DESIGN.md section 6.6).
template <class T>
class Iterable {
public:
    using valueType = T;

    virtual ~Iterable() = default;

    /// Java: Iterable.iterator().
    [[nodiscard]] virtual std::unique_ptr<Iterator<T>> iterator() = 0;

    /// Read-only counterpart of iterator(); the C++-only half of the split.
    [[nodiscard]] virtual std::unique_ptr<Iterator<const T>> constIterator() const = 0;

    /// Java: Iterable.forEach(action). Non-const because the action receives a
    /// mutable reference, exactly as Java's Consumer receives the element itself.
    /// For read-only traversal from a const context use begin()/end().
    virtual void forEach(std::move_only_function<void(T&)> action) {
        std::unique_ptr<Iterator<T>> iter = iterator();
        while (iter->hasNext()) {
            std::invoke(action, iter->next());
        }
    }

    /// Range-for and std::ranges support. Read-only and single-pass.
    [[nodiscard]] ReadOnlyIterator<T> begin() const {
        return ReadOnlyIterator<T>(constIterator());
    }

    /// The sentinel half of the range. A distinct type, see ReadOnlySentinel.
    [[nodiscard]] ReadOnlySentinel end() const noexcept { return ReadOnlySentinel(); }
};

}  // namespace cppstream
