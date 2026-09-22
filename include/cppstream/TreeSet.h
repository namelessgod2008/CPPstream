#pragma once

#include <cppstream/AbstractSet.h>
#include <cppstream/Comparator.h>
#include <cppstream/Optional.h>

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace cppstream {

/// The live window onto a TreeSet handed out by subSet/headSet/tailSet,
/// descendingSet, and the same methods on another window. Declared here so that
/// TreeSet can name it as its return type and befriend it.
///
/// One class covers both directions: it holds the *ascending* fence pair plus a
/// direction flag, exactly as java.util.TreeMap.DescendingSubMap does, so a
/// descending window is still a handle onto the same tree rather than a copy or a
/// dangling reference to an intermediate view.
template <class T, class Compare>
class TreeSetRangeView;

/// A port of java.util.TreeSet, backed by std::set.
///
/// TreeSet in Java is a NavigableSet, so this carries the navigable queries too:
/// first/last, the four directional lookups, the three range views and
/// descendingSet. As in Java, subSet/headSet/tailSet/descendingSet all return a
/// *live view* of this tree rather than a copy -- see TreeSetRangeView below for
/// what that means and how the fences behave. Divergence 30 is closed for TreeSet.
///
/// descendingIterator() is faithful too, and Descending names the type of an
/// independent reversed *copy* for callers who want one; descendingSet() itself is
/// a view, as it is in Java.
template <class T, class Compare = std::less<T>>
class TreeSet : public AbstractSet<T> {
public:
    using valueType = T;
    using Sorted = std::set<T, Compare>;

    TreeSet() = default;

    explicit TreeSet(const Collection<T>& source) { this->addAll(source); }

    explicit TreeSet(Sorted tree) : tree_(std::move(tree)) {}

    explicit TreeSet(Compare comparator) : tree_(comparator) {}

    TreeSet(std::initializer_list<T> values) {
        for (const T& value : values) {
            this->add(value);
        }
    }

    [[nodiscard]] int size() const override { return static_cast<int>(tree_.size()); }

    [[nodiscard]] bool contains(const T& element) const override {
        return tree_.find(element) != tree_.end();
    }

    bool add(const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        const bool inserted = tree_.insert(element).second;
        if (inserted) {
            ++this->modCount_;
        }
        return inserted;
    }

    bool add(T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        const bool inserted = tree_.insert(std::move(element)).second;
        if (inserted) {
            ++this->modCount_;
        }
        return inserted;
    }

    bool remove(const T& element) override {
        this->checkNotFrozen();
        const bool erased = tree_.erase(element) > 0;
        if (erased) {
            ++this->modCount_;
        }
        return erased;
    }

    void clear() override {
        this->checkNotFrozen();
        if (!tree_.empty()) {
            ++this->modCount_;
        }
        tree_.clear();
    }

    [[nodiscard]] std::unique_ptr<Iterator<T>> iterator() override {
        return std::make_unique<TreeSetIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> constIterator() const override {
        return std::make_unique<TreeSetConstIterator>(*this);
    }

    // --- NavigableSet ------------------------------------------------------

    /// Java: TreeSet.comparator(). Java returns null for natural ordering; this
    /// always has a comparer, so it returns it, wrapped in the type-erased
    /// Comparator<T> that its compare() contract belongs to. That return type is
    /// what lets a view report the reversed ordering when it is descending, so the
    /// two spellings agree.
    [[nodiscard]] Comparator<T> comparator() const {
        return comparatorFrom<T>(tree_.key_comp());
    }

    /// Java: NavigableSet.first().
    [[nodiscard]] const T& first() const {
        requireNotEmpty();
        return *tree_.begin();
    }

    /// Java: NavigableSet.last().
    [[nodiscard]] const T& last() const {
        requireNotEmpty();
        return *tree_.rbegin();
    }

    /// Java: NavigableSet.pollFirst(). A copy leaves the set, as Java's does.
    [[nodiscard]] Optional<T> pollFirst() {
        this->checkNotFrozen();
        if (tree_.empty()) {
            return Optional<T>::empty();
        }
        auto position = tree_.begin();
        T removed = *position;
        tree_.erase(position);
        ++this->modCount_;
        return Optional<T>::of(std::move(removed));
    }

    /// Java: NavigableSet.pollLast().
    [[nodiscard]] Optional<T> pollLast() {
        this->checkNotFrozen();
        if (tree_.empty()) {
            return Optional<T>::empty();
        }
        auto position = std::prev(tree_.end());
        T removed = *position;
        tree_.erase(position);
        ++this->modCount_;
        return Optional<T>::of(std::move(removed));
    }

    /// Java: NavigableSet.lower(element) -- strictly less than.
    [[nodiscard]] Optional<T> lower(const T& element) const {
        auto position = tree_.lower_bound(element);
        if (position == tree_.begin()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(*std::prev(position));
    }

    /// Java: NavigableSet.floor(element) -- less than or equal.
    [[nodiscard]] Optional<T> floor(const T& element) const {
        auto position = tree_.upper_bound(element);
        if (position == tree_.begin()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(*std::prev(position));
    }

    /// Java: NavigableSet.higher(element) -- strictly greater than.
    [[nodiscard]] Optional<T> higher(const T& element) const {
        auto position = tree_.upper_bound(element);
        if (position == tree_.end()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(*position);
    }

    /// Java: NavigableSet.ceiling(element) -- greater than or equal.
    [[nodiscard]] Optional<T> ceiling(const T& element) const {
        auto position = tree_.lower_bound(element);
        if (position == tree_.end()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(*position);
    }

    /// Java: NavigableSet.subSet(from, to) -- half-open [from, to), and a live view
    /// of this tree rather than a copy. See TreeSetRangeView.
    [[nodiscard]] TreeSetRangeView<T, Compare> subSet(const T& fromElement, const T& toElement);

    /// Java: NavigableSet.subSet(from, fromInclusive, to, toInclusive).
    [[nodiscard]] TreeSetRangeView<T, Compare> subSet(
        const T& fromElement, bool fromInclusive, const T& toElement, bool toInclusive);

    /// Java: NavigableSet.headSet(to) -- everything strictly below to.
    [[nodiscard]] TreeSetRangeView<T, Compare> headSet(const T& toElement);

    /// Java: NavigableSet.headSet(to, inclusive).
    [[nodiscard]] TreeSetRangeView<T, Compare> headSet(const T& toElement, bool inclusive);

    /// Java: NavigableSet.tailSet(from) -- from onwards, inclusive.
    [[nodiscard]] TreeSetRangeView<T, Compare> tailSet(const T& fromElement);

    /// Java: NavigableSet.tailSet(from, inclusive).
    [[nodiscard]] TreeSetRangeView<T, Compare> tailSet(const T& fromElement, bool inclusive);

    /// The read-only forms, built from a const set. Every mutator on the view
    /// they return throws UnsupportedOperationException, mirroring Java's
    /// Collections.unmodifiableNavigableSet(set).subSet(...) chain.
    [[nodiscard]] TreeSetRangeView<T, Compare> subSet(const T& fromElement, const T& toElement) const;
    [[nodiscard]] TreeSetRangeView<T, Compare> subSet(
        const T& fromElement, bool fromInclusive, const T& toElement, bool toInclusive) const;
    [[nodiscard]] TreeSetRangeView<T, Compare> headSet(const T& toElement) const;
    [[nodiscard]] TreeSetRangeView<T, Compare> headSet(const T& toElement, bool inclusive) const;
    [[nodiscard]] TreeSetRangeView<T, Compare> tailSet(const T& fromElement) const;
    [[nodiscard]] TreeSetRangeView<T, Compare> tailSet(const T& fromElement, bool inclusive) const;

    /// Sorted in ascending order by Compare, so this is a copy of the backing
    /// storage rather than a hash traversal.
    [[nodiscard]] std::vector<T> toSortedVector() const { return std::vector<T>(tree_.begin(), tree_.end()); }

    // --- Descending traversal ----------------------------------------------

    /// The type of an independent, descending-order *copy* of this set. Naming it
    /// matters: a C++ function cannot return "the same class with the opposite
    /// ordering" without a type, and TreeSet is parameterised on the comparer.
    ///
    /// descendingSet() itself no longer returns this -- it is a view, as in Java --
    /// but `Descending copy(ascendingSet)` is still the one-liner for a reversed
    /// set of your own, and the reversed comparer is what makes it sort itself.
    using Descending = TreeSet<T, ReversedCompare<T, Compare>>;

    /// Java: NavigableSet.descendingSet(). A live view of this same tree in
    /// reverse order: writes through it land here, an element outside the tree's
    /// fences is rejected the same way, and taking descendingSet() twice gives the
    /// ascending order back. Defined below the class body, where TreeSetRangeView
    /// is complete.
    [[nodiscard]] TreeSetRangeView<T, Compare> descendingSet();
    [[nodiscard]] TreeSetRangeView<T, Compare> descendingSet() const;

    /// Java: NavigableSet.descendingIterator().
    [[nodiscard]] std::unique_ptr<Iterator<T>> descendingIterator() {
        return std::make_unique<TreeSetDescendingIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> descendingConstIterator() const {
        return std::make_unique<TreeSetDescendingConstIterator>(*this);
    }

    class TreeSetConstIterator final : public Iterator<const T> {
    public:
        explicit TreeSetConstIterator(const TreeSet& owner)
            : owner_(&owner), current_(owner.tree_.begin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->tree_.end(); }

        const T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->tree_.end()) {
                throw NoSuchElementException("iterator exhausted");
            }
            return *current_++;
        }

    private:
        const TreeSet* owner_;
        typename Sorted::const_iterator current_;
        int expectedModCount_;
    };

    /// Same const_cast story as HashSet: std::set keys are const, Java's elements
    /// are not, and mutating an element of a sorted set is illegal in both.
    class TreeSetIterator final : public Iterator<T> {
    public:
        explicit TreeSetIterator(TreeSet& owner)
            : owner_(&owner), current_(owner.tree_.begin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->tree_.end(); }

        T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->tree_.end()) {
                throw NoSuchElementException("iterator exhausted");
            }
            last_ = current_;
            hasLast_ = true;
            T& element = const_cast<T&>(*current_);
            ++current_;
            return element;
        }

        void remove() override {
            owner_->checkNotFrozen();
            owner_->checkForComodification(expectedModCount_);
            if (!hasLast_) {
                throw IllegalStateException("remove() called before next()");
            }
            owner_->tree_.erase(last_);
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
            hasLast_ = false;
        }

    private:
        TreeSet* owner_;
        typename Sorted::iterator current_;
        typename Sorted::iterator last_;
        bool hasLast_ = false;
        int expectedModCount_;
    };

    /// Reverse walk. Mirrors the forward iterator exactly, including the
    /// const_cast rationale and fail-fast bookkeeping.
    class TreeSetDescendingIterator final : public Iterator<T> {
    public:
        explicit TreeSetDescendingIterator(TreeSet& owner)
            : owner_(&owner), current_(owner.tree_.rbegin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->tree_.rend(); }

        T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->tree_.rend()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = current_;
            hasLast_ = true;
            T& element = const_cast<T&>(*current_);
            ++current_;
            return element;
        }

        void remove() override {
            owner_->checkNotFrozen();
            owner_->checkForComodification(expectedModCount_);
            if (!hasLast_) {
                throw IllegalStateException("remove() called before next()");
            }
            // A reverse_iterator's base() points one past the element it denotes.
            owner_->tree_.erase(std::prev(lastReturned_.base()));
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
            hasLast_ = false;
        }

    private:
        TreeSet* owner_;
        typename Sorted::reverse_iterator current_;
        typename Sorted::reverse_iterator lastReturned_;
        bool hasLast_ = false;
        int expectedModCount_;
    };

    class TreeSetDescendingConstIterator final : public Iterator<const T> {
    public:
        explicit TreeSetDescendingConstIterator(const TreeSet& owner)
            : owner_(&owner), current_(owner.tree_.rbegin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->tree_.rend(); }

        const T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->tree_.rend()) {
                throw NoSuchElementException("iterator exhausted");
            }
            return *current_++;
        }

    private:
        const TreeSet* owner_;
        typename Sorted::const_reverse_iterator current_;
        int expectedModCount_;
    };

private:
    void requireNotEmpty() const {
        if (tree_.empty()) {
            throw NoSuchElementException("set is empty");
        }
    }

    /// TreeSetRangeView walks this very tree, so it needs the storage. Every other
    /// view in the library is built out of its owner's public API; this one cannot
    /// be, because a range of a sorted set can only be located through the tree's
    /// own lower_bound/upper_bound.
    template <class, class>
    friend class TreeSetRangeView;

    Sorted tree_;
};

// ---------------------------------------------------------------------------
// Live range views
// ---------------------------------------------------------------------------
//
// Java's NavigableSet range methods do not copy: subSet()/headSet()/tailSet()
// hand back a view of the same tree. A write through the view lands in the tree,
// an add() outside the window is an IllegalArgumentException, and an iterator
// over the view fails fast when the tree changes underneath it. This is that view,
// and its arrival closes divergence 30 for TreeSet.
//
// The view is a handle -- one pointer to the tree plus its two fences -- so
// nothing is copied to keep it in sync. The fence rules are the ones
// java.util.TreeMap.NavigableSubMap uses, checked against a JDK (section 9):
//
//   * a key is in the window when it lies between the fences, honouring each
//     fence's inclusivity;
//   * a new fence is accepted only when it does not widen the window, so a bound
//     equal to an exclusive fence has to be exclusive too;
//   * a window is rejected outright when from > to.
//
// The view carries a direction as well as a window, so descendingSet() is the
// same handle with the flag flipped -- and descendingSet() on a window is a
// descending window, not a copy and not a reference to a temporary. Direction
// shows up in four places: iteration order, which end first()/last() and
// pollFirst()/pollLast() come from, the floor/ceiling and lower/higher pairs
// (which swap), and the orientation of subSet/headSet/tailSet. Everything else --
// membership, size, add, remove, clear, fail-fast -- is direction-blind, because
// the window itself is.
//
// As with ListView, a view taken from a const TreeSet is read-only: every mutator
// throws UnsupportedOperationException, exactly as Java's
// Collections.unmodifiableNavigableSet(set).subSet(...) chain does.
template <class T, class Compare>
class TreeSetRangeView final : public AbstractSet<T> {
public:
    using valueType = T;
    using Sorted = typename TreeSet<T, Compare>::Sorted;

    /// A modifiable view, built by the non-const overloads of subSet/headSet/
    /// tailSet/descendingSet. The fences are always the *ascending* pair; only the
    /// direction flag distinguishes an ascending window from a descending one.
    TreeSetRangeView(TreeSet<T, Compare>& backing, std::optional<T> from, bool fromInclusive,
        std::optional<T> to, bool toInclusive, bool descending = false)
        : backing_(&backing), writableBacking_(&backing), from_(std::move(from)),
          to_(std::move(to)), fromInclusive_(fromInclusive), toInclusive_(toInclusive),
          descending_(descending) {}

    /// The read-only form, built by the const overloads.
    TreeSetRangeView(const TreeSet<T, Compare>& backing, std::optional<T> from, bool fromInclusive,
        std::optional<T> to, bool toInclusive, bool descending = false)
        : backing_(&backing), from_(std::move(from)), to_(std::move(to)),
          fromInclusive_(fromInclusive), toInclusive_(toInclusive), descending_(descending) {}

    /// Reports the backing tree's count rather than the view's own: iterating this
    /// view is iterating that tree, so any structural change to the tree has to
    /// invalidate the iterator. See AbstractCollection::modCount().
    [[nodiscard]] int modCount() const noexcept override { return backing_->modCount(); }

    /// Java: SubSet.size(), a walk of the window -- the view stores no count, and
    /// the window has the same cardinality whichever way it is walked.
    [[nodiscard]] int size() const override {
        return static_cast<int>(std::distance(firstInRange(), pastLastInRange()));
    }

    [[nodiscard]] bool contains(const T& element) const override {
        return inRange(element) && backing_->contains(element);
    }

    bool add(const T& element) override {
        this->checkNotFrozen();
        TreeSet<T, Compare>& target = writable();
        requireInRange(element);
        return target.add(element);
    }

    bool add(T&& element) override {
        this->checkNotFrozen();
        TreeSet<T, Compare>& target = writable();
        requireInRange(element);
        return target.add(std::move(element));
    }

    /// Java: SubSet.remove(Object). A key outside the window is simply not there,
    /// so this answers false rather than throwing.
    bool remove(const T& element) override {
        this->checkNotFrozen();
        TreeSet<T, Compare>& target = writable();
        if (!inRange(element)) {
            return false;
        }
        return target.remove(element);
    }

    /// Removes exactly the elements inside the window; the rest of the tree is
    /// untouched, as Java's SubSet.clear() does.
    void clear() override {
        this->checkNotFrozen();
        TreeSet<T, Compare>& target = writable();
        const std::vector<T> doomed = this->toArray();
        for (const T& value : doomed) {
            static_cast<void>(target.remove(value));
        }
    }

    [[nodiscard]] std::unique_ptr<Iterator<T>> iterator() override {
        static_cast<void>(writable());
        return std::make_unique<TreeSetRangeIterator>(*this, descending_);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> constIterator() const override {
        return std::make_unique<TreeSetRangeConstIterator>(*this, descending_);
    }

    // --- NavigableSet queries over the window -------------------------------

    /// Java: NavigableSet.comparator(), which for a descending view is the reverse
    /// of the tree's own ordering.
    ///
    /// Returned as the type-erased Comparator<T> rather than as Compare, because
    /// the direction lives in the view: a Compare cannot be reversed -- it is a
    /// std-style functor -- and handing back the tree's ascending comparer for a
    /// descending view would be actively wrong for anyone who then sorted with it.
    /// Java's reverseOrder(natural) is likewise a fresh object rather than the
    /// backing comparer.
    [[nodiscard]] Comparator<T> comparator() const {
        Comparator<T> base = asComparator();
        return descending_ ? base.reversed() : base;
    }

    /// Java: NavigableSet.first() -- the first element of the window *in this
    /// view's own order*, so a descending view answers with the window's largest.
    /// Throws NoSuchElementException when the window is empty, which is why the
    /// internal helpers answer with Optional instead.
    [[nodiscard]] const T& first() const {
        return descending_ ? windowLast() : windowFirst();
    }

    /// Java: NavigableSet.last().
    [[nodiscard]] const T& last() const {
        return descending_ ? windowFirst() : windowLast();
    }

    /// Java: NavigableSet.pollFirst(). An empty window yields empty and changes
    /// nothing, where first() would throw. A descending view polls the window's
    /// largest element, because that is where its traversal starts.
    [[nodiscard]] Optional<T> pollFirst() {
        this->checkNotFrozen();
        static_cast<void>(writable());
        return descending_ ? pollWindowLast() : pollWindowFirst();
    }

    /// Java: NavigableSet.pollLast().
    [[nodiscard]] Optional<T> pollLast() {
        this->checkNotFrozen();
        static_cast<void>(writable());
        return descending_ ? pollWindowFirst() : pollWindowLast();
    }

    /// Java: NavigableSet.floor(element) -- the greatest element of the window that
    /// is <= element *under this view's ordering*, or empty. The answer is clamped
    /// to the window: Java does not answer empty merely because the argument lies
    /// outside it, so subSet(20,40).floor(100) is the window's last element.
    /// Verified against a JDK; see DESIGN.md section 9.
    ///
    /// On a descending view the pair swaps, exactly as Java's DescendingSet does
    /// (`floor` delegates to the ascending ceiling), so floor(25) on {30,40} is 30
    /// and ceiling(25) is empty.
    [[nodiscard]] Optional<T> floor(const T& element) const {
        return descending_ ? ascendingCeiling(element) : ascendingFloor(element);
    }

    /// Java: NavigableSet.lower(element) -- greatest element of the window < element.
    [[nodiscard]] Optional<T> lower(const T& element) const {
        return descending_ ? ascendingHigher(element) : ascendingLower(element);
    }

    /// Java: NavigableSet.ceiling(element) -- least element of the window >= element.
    [[nodiscard]] Optional<T> ceiling(const T& element) const {
        return descending_ ? ascendingFloor(element) : ascendingCeiling(element);
    }

    /// Java: NavigableSet.higher(element) -- least element of the window > element.
    [[nodiscard]] Optional<T> higher(const T& element) const {
        return descending_ ? ascendingLower(element) : ascendingHigher(element);
    }

    // --- Ranges of a range --------------------------------------------------

    /// Java: NavigableSet.subSet(from, fromInclusive, to, toInclusive) on a view.
    /// The new window may not widen this one, exactly as Java's SubSet requires,
    /// and on a descending view "from" is the *upper* end, so the two bounds are
    /// stored the other way round while the view keeps its direction.
    [[nodiscard]] TreeSetRangeView subSet(
        const T& fromElement, bool fromInclusive, const T& toElement, bool toInclusive) {
        requireAcceptableWindow(fromElement, fromInclusive, toElement, toInclusive);
        return makeSubWindowFor(fromElement, fromInclusive, toElement, toInclusive);
    }

    [[nodiscard]] TreeSetRangeView subSet(
        const T& fromElement, bool fromInclusive, const T& toElement, bool toInclusive) const {
        requireAcceptableWindow(fromElement, fromInclusive, toElement, toInclusive);
        return makeReadOnlySubWindowFor(fromElement, fromInclusive, toElement, toInclusive);
    }

    [[nodiscard]] TreeSetRangeView subSet(const T& fromElement, const T& toElement) {
        return subSet(fromElement, true, toElement, false);
    }

    [[nodiscard]] TreeSetRangeView subSet(const T& fromElement, const T& toElement) const {
        return subSet(fromElement, true, toElement, false);
    }

    /// Java: NavigableSet.headSet(to, inclusive) on a view: the fence at the far
    /// end is kept and the one at this view's start is replaced. Descending, that
    /// means the *ascending lower* fence moves, because the traversal starts at
    /// the top.
    [[nodiscard]] TreeSetRangeView headSet(const T& toElement, bool inclusive) {
        requireAcceptedBound(toElement, inclusive, "toKey out of range");
        if (descending_) {
            return makeSubWindow(toElement, inclusive, to_, toInclusive_);
        }
        return makeSubWindow(from_, fromInclusive_, toElement, inclusive);
    }

    [[nodiscard]] TreeSetRangeView headSet(const T& toElement, bool inclusive) const {
        requireAcceptedBound(toElement, inclusive, "toKey out of range");
        if (descending_) {
            return TreeSetRangeView(
                *backing_, toElement, inclusive, to_, toInclusive_, descending_);
        }
        return TreeSetRangeView(*backing_, from_, fromInclusive_, toElement, inclusive);
    }

    [[nodiscard]] TreeSetRangeView headSet(const T& toElement) { return headSet(toElement, false); }

    [[nodiscard]] TreeSetRangeView headSet(const T& toElement) const {
        return headSet(toElement, false);
    }

    /// Java: NavigableSet.tailSet(from, inclusive) on a view: the fence at this
    /// view's start is replaced and the far one is kept. Descending, the traversal
    /// starts at the top, so the *ascending upper* fence moves.
    [[nodiscard]] TreeSetRangeView tailSet(const T& fromElement, bool inclusive) {
        requireAcceptedBound(fromElement, inclusive, "fromKey out of range");
        if (descending_) {
            return makeSubWindow(from_, fromInclusive_, fromElement, inclusive);
        }
        return makeSubWindow(fromElement, inclusive, to_, toInclusive_);
    }

    [[nodiscard]] TreeSetRangeView tailSet(const T& fromElement, bool inclusive) const {
        requireAcceptedBound(fromElement, inclusive, "fromKey out of range");
        if (descending_) {
            return TreeSetRangeView(
                *backing_, from_, fromInclusive_, fromElement, inclusive, descending_);
        }
        return TreeSetRangeView(*backing_, fromElement, inclusive, to_, toInclusive_);
    }

    [[nodiscard]] TreeSetRangeView tailSet(const T& fromElement) {
        return tailSet(fromElement, true);
    }

    [[nodiscard]] TreeSetRangeView tailSet(const T& fromElement) const {
        return tailSet(fromElement, true);
    }

    /// Java: NavigableSet.descendingSet() on a view. The same window, walked the
    /// other way; a second call gives the original order back, as it does in Java.
    [[nodiscard]] TreeSetRangeView descendingSet() {
        return makeFlippedWindow();
    }

    [[nodiscard]] TreeSetRangeView descendingSet() const {
        return TreeSetRangeView(
            *backing_, from_, fromInclusive_, to_, toInclusive_, !descending_);
    }

    /// Java: NavigableSet.descendingIterator(). Note this is the *opposite* of the
    /// view's own order, so on an already-descending view it walks forwards --
    /// which is what TreeSet.descendingIterator() does for an ascending set.
    [[nodiscard]] std::unique_ptr<Iterator<T>> descendingIterator() {
        static_cast<void>(writable());
        return std::make_unique<TreeSetRangeIterator>(*this, !descending_);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> descendingConstIterator() const {
        return std::make_unique<TreeSetRangeConstIterator>(*this, !descending_);
    }

    /// Walks the window in this view's own order. Fail-fast on the *backing* tree,
    /// since that is what the cursor points into.
    class TreeSetRangeConstIterator final : public Iterator<const T> {
    public:
        /// `descending` is the *walk* direction, which is normally the view's own
        /// but is flipped by descendingIterator(). It is a parameter rather than a
        /// second owner because a flipped walk must not point at a temporary view.
        explicit TreeSetRangeConstIterator(const TreeSetRangeView& owner, bool descending)
            : owner_(&owner), descending_(descending),
              cursor_(descending ? owner.pastLastInRange() : owner.firstInRange()),
              expectedModCount_(owner.backingModCount()) {}

        /// The cursor is one past the next element in *ascending* terms when
        /// descending, so a reverse walk needs no reverse_iterator and never has to
        /// form an iterator before begin().
        [[nodiscard]] bool hasNext() const override {
            return descending_ ? cursor_ != owner_->firstInRange()
                               : cursor_ != owner_->pastLastInRange();
        }

        const T& next() override {
            owner_->checkBacking(expectedModCount_);
            if (!hasNext()) {
                throw NoSuchElementException("iterator exhausted");
            }
            if (descending_) {
                --cursor_;
                return *cursor_;
            }
            return *cursor_++;
        }

    private:
        const TreeSetRangeView* owner_;
        bool descending_;
        typename TreeSet<T, Compare>::Sorted::const_iterator cursor_;
        int expectedModCount_;
    };

    /// The mutable form. Elements come back as `T&` through the same const_cast
    /// story as TreeSetIterator: std::set keys are const, Java's elements are not.
    class TreeSetRangeIterator final : public Iterator<T> {
    public:
        explicit TreeSetRangeIterator(TreeSetRangeView& owner, bool descending)
            : owner_(&owner), descending_(descending),
              cursor_(descending ? owner.pastLastInRangeForWrite() : owner.firstInRangeForWrite()),
              expectedModCount_(owner.backingModCount()) {}

        [[nodiscard]] bool hasNext() const override {
            return descending_ ? cursor_ != owner_->firstInRangeForWrite()
                               : cursor_ != owner_->pastLastInRangeForWrite();
        }

        T& next() override {
            owner_->checkBacking(expectedModCount_);
            if (!hasNext()) {
                throw NoSuchElementException("iterator exhausted");
            }
            if (descending_) {
                --cursor_;
                last_ = cursor_;
                hasLast_ = true;
                return const_cast<T&>(*cursor_);
            }
            last_ = cursor_;
            hasLast_ = true;
            return const_cast<T&>(*cursor_++);
        }

        void remove() override {
            owner_->checkNotFrozen();
            owner_->checkBacking(expectedModCount_);
            if (!hasLast_) {
                throw IllegalStateException("remove() called before next()");
            }
            // A forward cursor already sits past the element it yielded; a reverse
            // one sits on it, so erasing invalidates it and the successor has to be
            // captured first.
            const typename TreeSet<T, Compare>::Sorted::iterator successor = std::next(last_);
            static_cast<void>(owner_->writable().remove(*last_));
            if (descending_) {
                cursor_ = successor;
            }
            expectedModCount_ = owner_->backingModCount();
            hasLast_ = false;
        }

    private:
        TreeSetRangeView* owner_;
        bool descending_;
        typename TreeSet<T, Compare>::Sorted::iterator cursor_;
        typename TreeSet<T, Compare>::Sorted::iterator last_;
        bool hasLast_ = false;
        int expectedModCount_;
    };

private:
    /// A window onto this window, keeping this view's direction. A read-only view
    /// only ever produces read-only views, so
    /// `unmodifiableNavigableSet(set).subSet(a, b)` reads rather than throws,
    /// exactly as Java's wrapper does. Which backing to carry is the whole
    /// decision; the new fences are already known to lie inside this window.
    [[nodiscard]] TreeSetRangeView makeSubWindow(const std::optional<T>& from, bool fromInclusive,
        const std::optional<T>& to, bool toInclusive) const {
        if (writableBacking_ == nullptr) {
            return TreeSetRangeView(*backing_, from, fromInclusive, to, toInclusive, descending_);
        }
        return TreeSetRangeView(
            *writableBacking_, from, fromInclusive, to, toInclusive, descending_);
    }

    /// subSet()'s window, with the two bounds stored ascending whichever way this
    /// view walks: Java's DescendingSubMap passes the pair to its base constructor
    /// in the opposite order for exactly this reason.
    [[nodiscard]] TreeSetRangeView makeSubWindowFor(const std::optional<T>& fromElement,
        bool fromInclusive, const std::optional<T>& toElement, bool toInclusive) const {
        if (descending_) {
            return makeSubWindow(toElement, toInclusive, fromElement, fromInclusive);
        }
        return makeSubWindow(fromElement, fromInclusive, toElement, toInclusive);
    }

    /// The same, forced read-only. Used by the const overloads, which must not hand
    /// out a writable window however writable this one happens to be.
    [[nodiscard]] TreeSetRangeView makeReadOnlySubWindowFor(const std::optional<T>& fromElement,
        bool fromInclusive, const std::optional<T>& toElement, bool toInclusive) const {
        if (descending_) {
            return TreeSetRangeView(
                *backing_, toElement, toInclusive, fromElement, fromInclusive, descending_);
        }
        return TreeSetRangeView(
            *backing_, fromElement, fromInclusive, toElement, toInclusive, descending_);
    }

    /// The same window walked the other way -- descendingSet().
    [[nodiscard]] TreeSetRangeView makeFlippedWindow() const {
        if (writableBacking_ == nullptr) {
            return TreeSetRangeView(
                *backing_, from_, fromInclusive_, to_, toInclusive_, !descending_);
        }
        return TreeSetRangeView(
            *writableBacking_, from_, fromInclusive_, to_, toInclusive_, !descending_);
    }

    /// The one place a read-only view is rejected. Everything writable funnels here.
    [[nodiscard]] TreeSet<T, Compare>& writable() const {
        if (writableBacking_ == nullptr) {
            throw UnsupportedOperationException(
                "this view is read-only: it was taken from a const set");
        }
        return *writableBacking_;
    }

    [[nodiscard]] int backingModCount() const { return backing_->modCount(); }

    void checkBacking(int expectedModCount) const {
        if (backingModCount() != expectedModCount) {
            throw ConcurrentModificationException("set modified during iteration");
        }
    }

    /// The backing tree's ordering, in its own std-style form: the fence
    /// comparisons need it, while the public comparator() hands out the Java-style
    /// one. The friendship is what allows reading the tree's key_comp() here.
    [[nodiscard]] Compare comparatorOf() const { return backing_->tree_.key_comp(); }

    /// The tree's own ordering as the type-erased Comparator<T> whose
    /// compare() contract is Java's. Reversing *that* is what gives a descending
    /// view a comparator that reports the order it actually walks in.
    [[nodiscard]] Comparator<T> asComparator() const {
        return comparatorFrom<T>(comparatorOf());
    }

    /// The four lookups as the *ascending* window answers them. floor/lower/
    /// ceiling/higher pick a pair out of these, which is how a descending view
    /// swaps them without duplicating the clamping rules.
    [[nodiscard]] Optional<T> ascendingFloor(const T& element) const {
        return clampDownward(backing_->floor(element));
    }

    [[nodiscard]] Optional<T> ascendingLower(const T& element) const {
        return clampDownward(backing_->lower(element));
    }

    [[nodiscard]] Optional<T> ascendingCeiling(const T& element) const {
        return clampUpward(backing_->ceiling(element));
    }

    [[nodiscard]] Optional<T> ascendingHigher(const T& element) const {
        return clampUpward(backing_->higher(element));
    }

    /// The window's smallest and largest elements, in ascending terms. first(),
    /// last() and the two polls pick one of each according to the direction.
    [[nodiscard]] const T& windowFirst() const {
        const auto position = firstInRange();
        if (position == pastLastInRange()) {
            throw NoSuchElementException("set is empty");
        }
        return *position;
    }

    [[nodiscard]] const T& windowLast() const {
        const auto position = pastLastInRange();
        if (position == firstInRange()) {
            throw NoSuchElementException("set is empty");
        }
        return *std::prev(position);
    }

    [[nodiscard]] Optional<T> pollWindowFirst() {
        TreeSet<T, Compare>& target = writable();
        const auto position = firstInRange();
        if (position == pastLastInRange()) {
            return Optional<T>::empty();
        }
        T removed = *position;
        static_cast<void>(target.remove(removed));
        return Optional<T>::of(std::move(removed));
    }

    [[nodiscard]] Optional<T> pollWindowLast() {
        TreeSet<T, Compare>& target = writable();
        const auto position = pastLastInRange();
        if (position == firstInRange()) {
            return Optional<T>::empty();
        }
        T removed = *std::prev(position);
        static_cast<void>(target.remove(removed));
        return Optional<T>::of(std::move(removed));
    }

    /// Java: NavigableSubMap.inRange(key) -- is this key between the fences?
    [[nodiscard]] bool inRange(const T& element) const {
        return !belowLowerFence(element) && !aboveUpperFence(element);
    }

    [[nodiscard]] bool belowLowerFence(const T& element) const {
        if (!from_.has_value()) {
            return false;
        }
        const Compare& less = comparatorOf();
        if (less(element, *from_)) {
            return true;
        }
        return !fromInclusive_ && !less(*from_, element);
    }

    [[nodiscard]] bool aboveUpperFence(const T& element) const {
        if (!to_.has_value()) {
            return false;
        }
        const Compare& less = comparatorOf();
        if (less(*to_, element)) {
            return true;
        }
        return !toInclusive_ && !less(element, *to_);
    }

    /// Java: the check NavigableSubMap applies to a *new* fence. A bound is
    /// rejected when it would widen the window: beyond a fence, or equal to an
    /// exclusive fence while claiming to include it.
    [[nodiscard]] bool acceptsBound(const T& bound, bool inclusive) const {
        if (from_.has_value()) {
            const Compare& less = comparatorOf();
            if (less(bound, *from_)) {
                return false;
            }
            if (inclusive && !fromInclusive_ && !less(*from_, bound)) {
                return false;
            }
        }
        if (to_.has_value()) {
            const Compare& less = comparatorOf();
            if (less(*to_, bound)) {
                return false;
            }
            if (inclusive && !toInclusive_ && !less(bound, *to_)) {
                return false;
            }
        }
        return true;
    }

    void requireAcceptedBound(const T& bound, bool inclusive, const char* message) const {
        this->requireNonNull(bound);
        if (!acceptsBound(bound, inclusive)) {
            throw IllegalArgumentException(message);
        }
    }

    void requireAcceptableWindow(const T& fromElement, bool fromInclusive, const T& toElement,
        bool toInclusive) const {
        requireAcceptedBound(fromElement, fromInclusive, "fromKey out of range");
        requireAcceptedBound(toElement, toInclusive, "toKey out of range");
        // Java tests the *ascending* pair, because that is what its submap
        // constructor is handed: for a descending window the arguments arrive
        // swapped, so "from > to" flips with them.
        const Compare& less = comparatorOf();
        const bool backwards = descending_ ? less(fromElement, toElement) : less(toElement, fromElement);
        if (backwards) {
            throw IllegalArgumentException("fromKey > toKey");
        }
    }

    void requireInRange(const T& element) const {
        this->requireNonNull(element);
        if (!inRange(element)) {
            throw IllegalArgumentException("key out of range");
        }
    }

    /// A candidate that fell below the window collapses to empty; one that fell
    /// above it collapses to the window's last element. That clamping is Java's,
    /// verified against a JDK -- see floor()/lower() above.
    [[nodiscard]] Optional<T> clampDownward(Optional<T> candidate) const {
        if (candidate.isEmpty()) {
            return candidate;
        }
        if (belowLowerFence(candidate.get())) {
            return Optional<T>::empty();
        }
        if (!inRange(candidate.get())) {
            return lastOrEmpty();
        }
        return candidate;
    }

    [[nodiscard]] Optional<T> clampUpward(Optional<T> candidate) const {
        if (candidate.isEmpty()) {
            return candidate;
        }
        if (aboveUpperFence(candidate.get())) {
            return Optional<T>::empty();
        }
        if (!inRange(candidate.get())) {
            return firstOrEmpty();
        }
        return candidate;
    }

    [[nodiscard]] Optional<T> firstOrEmpty() const {
        const auto position = firstInRange();
        if (position == pastLastInRange()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(*position);
    }

    [[nodiscard]] Optional<T> lastOrEmpty() const {
        const auto position = pastLastInRange();
        if (position == firstInRange()) {
            return Optional<T>::empty();
        }
        return Optional<T>::of(*std::prev(position));
    }

    /// Both helpers are templates over the owner's constness, so the read-only
    /// iterator gets const_iterators and the mutable one gets iterators without a
    /// const_cast in either direction.
    ///
    /// `firstAtOrAboveLowerFenceOf` is the raw lower boundary, with the upper one
    /// ignored: the candidate start of the window. Whether it really is inside is
    /// the emptiness question answered below.
    template <class Owner>
    [[nodiscard]] auto firstAtOrAboveLowerFenceOf(Owner& owner) const {
        auto& tree = owner.tree_;
        if (!from_.has_value()) {
            return tree.begin();
        }
        return fromInclusive_ ? tree.lower_bound(*from_) : tree.upper_bound(*from_);
    }

    /// Java: NavigableSubMap's inRange test, applied to the candidate start. A
    /// window can be accepted with its two fences a hair apart -- Java allows
    /// `descending.headSet(k, false)` even when that yields the *empty* window
    /// [k, k), because an exclusive bound only has to lie in the closed range --
    /// and then the candidate start sits above the upper fence with no element in
    /// the window at all.
    ///
    /// Deciding emptiness explicitly is also what keeps firstInRange() and
    /// pastLastInRange() mutually reachable: in an inverted window the raw bounds
    /// are reversed (upper_bound(k) past lower_bound(k)), so `std::distance` and
    /// the descending `--cursor_` walk would otherwise run off the end of the
    /// tree. Both helpers collapse such a window to [end(), end()).
    template <class Owner>
    [[nodiscard]] bool windowIsEmptyOf(Owner& owner) const {
        auto& tree = owner.tree_;
        const auto candidate = firstAtOrAboveLowerFenceOf(owner);
        return candidate == tree.end() || aboveUpperFence(*candidate);
    }

    template <class Owner>
    [[nodiscard]] auto firstInRangeOf(Owner& owner) const {
        auto& tree = owner.tree_;
        if (windowIsEmptyOf(owner)) {
            return tree.end();
        }
        return firstAtOrAboveLowerFenceOf(owner);
    }

    template <class Owner>
    [[nodiscard]] auto pastLastInRangeOf(Owner& owner) const {
        auto& tree = owner.tree_;
        if (firstInRangeOf(owner) == tree.end()) {
            return tree.end();
        }
        if (!to_.has_value()) {
            return tree.end();
        }
        return toInclusive_ ? tree.upper_bound(*to_) : tree.lower_bound(*to_);
    }

    [[nodiscard]] typename Sorted::const_iterator firstInRange() const {
        return firstInRangeOf(*backing_);
    }

    [[nodiscard]] typename Sorted::const_iterator pastLastInRange() const {
        return pastLastInRangeOf(*backing_);
    }

    [[nodiscard]] typename Sorted::iterator firstInRangeForWrite() {
        return firstInRangeOf(writable());
    }

    [[nodiscard]] typename Sorted::iterator pastLastInRangeForWrite() {
        return pastLastInRangeOf(writable());
    }

    const TreeSet<T, Compare>* backing_;
    /// Null for a view of a const set; every mutator checks it.
    TreeSet<T, Compare>* writableBacking_ = nullptr;
    std::optional<T> from_;
    std::optional<T> to_;
    bool fromInclusive_ = true;
    bool toInclusive_ = false;
    /// The fences above are always the ascending pair; this is the only thing that
    /// distinguishes a descending window from an ascending one.
    bool descending_ = false;
};

// ---------------------------------------------------------------------------
// TreeSet range accessors
// ---------------------------------------------------------------------------
//
// Defined down here because the return type TreeSetRangeView is incomplete inside
// the class body above -- the same reason Map::keySet() is defined at the bottom
// of Map.h. The whole-window checks (null bounds, fromKey > toKey) live here, on
// the container; a window taken from another window re-checks both its fences in
// TreeSetRangeView itself.
//
// descendingSet() is here for the same incompleteness reason: it hands out a view,
// so it cannot be written inside the class body either.

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::descendingSet() {
    return TreeSetRangeView<T, Compare>(*this, std::nullopt, true, std::nullopt, false, true);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::descendingSet() const {
    return TreeSetRangeView<T, Compare>(*this, std::nullopt, true, std::nullopt, false, true);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::subSet(
    const T& fromElement, bool fromInclusive, const T& toElement, bool toInclusive) {
    this->requireNonNull(fromElement);
    this->requireNonNull(toElement);
    const Compare& less = tree_.key_comp();
    if (less(toElement, fromElement)) {
        throw IllegalArgumentException("fromKey > toKey");
    }
    return TreeSetRangeView<T, Compare>(*this, fromElement, fromInclusive, toElement, toInclusive);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::subSet(
    const T& fromElement, bool fromInclusive, const T& toElement, bool toInclusive) const {
    this->requireNonNull(fromElement);
    this->requireNonNull(toElement);
    const Compare& less = tree_.key_comp();
    if (less(toElement, fromElement)) {
        throw IllegalArgumentException("fromKey > toKey");
    }
    return TreeSetRangeView<T, Compare>(*this, fromElement, fromInclusive, toElement, toInclusive);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::subSet(
    const T& fromElement, const T& toElement) {
    return subSet(fromElement, true, toElement, false);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::subSet(
    const T& fromElement, const T& toElement) const {
    return subSet(fromElement, true, toElement, false);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::headSet(const T& toElement, bool inclusive) {
    this->requireNonNull(toElement);
    return TreeSetRangeView<T, Compare>(*this, std::nullopt, true, toElement, inclusive);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::headSet(
    const T& toElement, bool inclusive) const {
    this->requireNonNull(toElement);
    return TreeSetRangeView<T, Compare>(*this, std::nullopt, true, toElement, inclusive);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::headSet(const T& toElement) {
    return headSet(toElement, false);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::headSet(const T& toElement) const {
    return headSet(toElement, false);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::tailSet(const T& fromElement, bool inclusive) {
    this->requireNonNull(fromElement);
    return TreeSetRangeView<T, Compare>(*this, fromElement, inclusive, std::nullopt, false);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::tailSet(
    const T& fromElement, bool inclusive) const {
    this->requireNonNull(fromElement);
    return TreeSetRangeView<T, Compare>(*this, fromElement, inclusive, std::nullopt, false);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::tailSet(const T& fromElement) {
    return tailSet(fromElement, true);
}

template <class T, class Compare>
TreeSetRangeView<T, Compare> TreeSet<T, Compare>::tailSet(const T& fromElement) const {
    return tailSet(fromElement, true);
}

}  // namespace cppstream
