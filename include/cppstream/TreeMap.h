#pragma once

#include <cppstream/Comparator.h>
#include <cppstream/Elements.h>
#include <cppstream/Map.h>
#include <cppstream/Optional.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace cppstream {

/// The live range view handed out by TreeMap.subMap/headMap/tailMap and
/// descendingMap. Declared here so that TreeMap can name it as its return type
/// and befriend it.
template <class K, class V, class Compare>
class TreeMapRangeView;

/// The live NavigableSet of keys handed out by navigableKeySet() and
/// descendingKeySet(). Declared here so a range view can name it as a return
/// type; defined below TreeMapRangeView, whose API every one of its members
/// delegates to.
template <class K, class V, class Compare>
class TreeMapKeySetView;

/// A port of java.util.TreeMap, backed by std::map.
///
/// Sorted by Compare, so iteration order is key order -- this is the one
/// container where "encounter order" is not insertion order but sorted order, and
/// Collectors::groupingBy with a TreeMap factory therefore produces sorted groups.
///
/// The navigable queries are included (first/last key, the four directional key
/// lookups, the three range views, descendingMap, navigableKeySet and
/// descendingKeySet) because they are what TreeMap is for. As in Java, all of them
/// return a *live view* of this map rather than a copy -- see TreeMapRangeView and
/// TreeMapKeySetView below. Divergence 30 is closed for TreeMap.
///
/// Descending names the type of an independent reversed *copy* for callers who
/// want one; descendingMap() itself is a view, as it is in Java.
template <class K, class V, class Compare = std::less<K>>
class TreeMap : public Map<K, V> {
public:
    using keyType = K;
    using valueType = V;
    using Entries = std::map<K, V, Compare>;
    using Entry = Map<K, V>::Entry;

    TreeMap() = default;

    explicit TreeMap(Entries entries) : entries_(std::move(entries)) {}

    explicit TreeMap(Compare comparator) : entries_(comparator) {}

    TreeMap(std::initializer_list<std::pair<const K, V>> values) : entries_(values) {}

    [[nodiscard]] int size() const override { return static_cast<int>(entries_.size()); }

    [[nodiscard]] bool containsKey(const K& key) const override {
        return entries_.find(key) != entries_.end();
    }

    [[nodiscard]] bool containsValue(const V& value) const override {
        for (const auto& entry : entries_) {
            if (elementEquals(entry.second, value)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const V* get(const K& key) const override {
        const auto found = entries_.find(key);
        return found == entries_.end() ? nullptr : std::addressof(found->second);
    }

    [[nodiscard]] V* get(const K& key) override {
        const auto found = entries_.find(key);
        return found == entries_.end() ? nullptr : std::addressof(found->second);
    }

    Optional<V> put(const K& key, const V& value) override {
        this->checkNotFrozen();
        requireNonNull(key);
        requireNonNull(value);
        auto found = entries_.find(key);
        if (found == entries_.end()) {
            entries_.emplace(key, value);
            this->bumpModCount();
            return Optional<V>::empty();
        }
        V previous = std::move(found->second);
        found->second = value;
        return Optional<V>::of(std::move(previous));
    }

    Optional<V> put(K&& key, V&& value) override {
        this->checkNotFrozen();
        requireNonNull(key);
        requireNonNull(value);
        auto found = entries_.find(key);
        if (found == entries_.end()) {
            entries_.emplace(std::move(key), std::move(value));
            this->bumpModCount();
            return Optional<V>::empty();
        }
        V previous = std::move(found->second);
        found->second = std::move(value);
        return Optional<V>::of(std::move(previous));
    }

    Optional<V> putIfAbsent(const K& key, const V& value) override {
        this->checkNotFrozen();
        requireNonNull(key);
        requireNonNull(value);
        auto found = entries_.find(key);
        if (found != entries_.end()) {
            return Optional<V>::of(found->second);
        }
        entries_.emplace(key, value);
        this->bumpModCount();
        return Optional<V>::empty();
    }

    Optional<V> putIfAbsent(K&& key, V&& value) override {
        this->checkNotFrozen();
        requireNonNull(key);
        requireNonNull(value);
        auto found = entries_.find(key);
        if (found != entries_.end()) {
            return Optional<V>::of(found->second);
        }
        entries_.emplace(std::move(key), std::move(value));
        this->bumpModCount();
        return Optional<V>::empty();
    }

    Optional<V> remove(const K& key) override {
        this->checkNotFrozen();
        auto found = entries_.find(key);
        if (found == entries_.end()) {
            return Optional<V>::empty();
        }
        V removed = std::move(found->second);
        entries_.erase(found);
        this->bumpModCount();
        return Optional<V>::of(std::move(removed));
    }

    void clear() override {
        this->checkNotFrozen();
        if (!entries_.empty()) {
            entries_.clear();
            this->bumpModCount();
        }
    }

    Optional<V> computeIfAbsent(
        const K& key, const std::function<Optional<V>(const K&)>& mappingFunction) override {
        this->checkNotFrozen();
        requireNonNull(key);
        auto found = entries_.find(key);
        if (found != entries_.end()) {
            return Optional<V>::of(found->second);
        }
        Optional<V> computed = std::invoke(mappingFunction, key);
        if (computed.isEmpty()) {
            return Optional<V>::empty();
        }
        auto position = entries_.emplace(key, std::move(computed).get()).first;
        this->bumpModCount();
        return Optional<V>::of(position->second);
    }

    Optional<V> computeIfPresent(const K& key,
        const std::function<Optional<V>(const K&, const V&)>& remappingFunction) override {
        this->checkNotFrozen();
        auto found = entries_.find(key);
        if (found == entries_.end()) {
            return Optional<V>::empty();
        }
        Optional<V> computed = std::invoke(remappingFunction, key, found->second);
        if (computed.isEmpty()) {
            entries_.erase(found);
            this->bumpModCount();
            return Optional<V>::empty();
        }
        found->second = std::move(computed).get();
        return Optional<V>::of(found->second);
    }

    Optional<V> merge(const K& key, const V& value,
        const std::function<Optional<V>(const V&, const V&)>& remappingFunction) override {
        this->checkNotFrozen();
        requireNonNull(key);
        requireNonNull(value);
        auto found = entries_.find(key);
        if (found == entries_.end()) {
            entries_.emplace(key, value);
            this->bumpModCount();
            return Optional<V>::of(value);
        }
        Optional<V> computed = std::invoke(remappingFunction, found->second, value);
        if (computed.isEmpty()) {
            entries_.erase(found);
            this->bumpModCount();
            return Optional<V>::empty();
        }
        found->second = std::move(computed).get();
        return Optional<V>::of(found->second);
    }

    // --- NavigableMap ------------------------------------------------------

    /// Java: NavigableMap.comparator(). Java returns null for natural ordering;
    /// this always has a comparer, so it returns it, wrapped in the type-erased
    /// Comparator<K> that its compare() contract belongs to.
    [[nodiscard]] Comparator<K> comparator() const {
        return comparatorFrom<K>(entries_.key_comp());
    }

    /// Java: NavigableMap.firstKey().
    [[nodiscard]] const K& firstKey() const {
        requireNotEmpty();
        return entries_.begin()->first;
    }

    /// Java: NavigableMap.lastKey().
    [[nodiscard]] const K& lastKey() const {
        requireNotEmpty();
        return std::prev(entries_.end())->first;
    }

    /// Java: NavigableMap.firstEntry(). A copy, like every Entry this library
    /// hands out: Map::Entry is a value type (divergence 12).
    [[nodiscard]] Optional<Entry> firstEntry() const {
        if (entries_.empty()) {
            return Optional<Entry>::empty();
        }
        return Optional<Entry>::of(Entry(entries_.begin()->first, entries_.begin()->second));
    }

    /// Java: NavigableMap.lastEntry().
    [[nodiscard]] Optional<Entry> lastEntry() const {
        if (entries_.empty()) {
            return Optional<Entry>::empty();
        }
        const auto position = std::prev(entries_.end());
        return Optional<Entry>::of(Entry(position->first, position->second));
    }

    /// Java: NavigableMap.pollFirstEntry(). Removes, and hands back a copy.
    [[nodiscard]] Optional<Entry> pollFirstEntry() {
        this->checkNotFrozen();
        if (entries_.empty()) {
            return Optional<Entry>::empty();
        }
        auto position = entries_.begin();
        Entry removed(position->first, std::move(position->second));
        entries_.erase(position);
        this->bumpModCount();
        return Optional<Entry>::of(std::move(removed));
    }

    /// Java: NavigableMap.pollLastEntry().
    [[nodiscard]] Optional<Entry> pollLastEntry() {
        this->checkNotFrozen();
        if (entries_.empty()) {
            return Optional<Entry>::empty();
        }
        auto position = std::prev(entries_.end());
        Entry removed(position->first, std::move(position->second));
        entries_.erase(position);
        this->bumpModCount();
        return Optional<Entry>::of(std::move(removed));
    }

    /// Java: NavigableMap.floorKey(key) -- the greatest key <= key.
    [[nodiscard]] Optional<K> floorKey(const K& key) const {
        auto position = entries_.upper_bound(key);
        if (position == entries_.begin()) {
            return Optional<K>::empty();
        }
        return Optional<K>::of(std::prev(position)->first);
    }

    /// Java: NavigableMap.ceilingKey(key) -- the least key >= key.
    [[nodiscard]] Optional<K> ceilingKey(const K& key) const {
        auto position = entries_.lower_bound(key);
        if (position == entries_.end()) {
            return Optional<K>::empty();
        }
        return Optional<K>::of(position->first);
    }

    /// Java: NavigableMap.lowerKey(key) -- the greatest key strictly < key.
    [[nodiscard]] Optional<K> lowerKey(const K& key) const {
        auto position = entries_.lower_bound(key);
        if (position == entries_.begin()) {
            return Optional<K>::empty();
        }
        return Optional<K>::of(std::prev(position)->first);
    }

    /// Java: NavigableMap.higherKey(key) -- the least key strictly > key.
    [[nodiscard]] Optional<K> higherKey(const K& key) const {
        auto position = entries_.upper_bound(key);
        if (position == entries_.end()) {
            return Optional<K>::empty();
        }
        return Optional<K>::of(position->first);
    }

    /// Java: NavigableMap.subMap(from, to) -- half-open [from, to), and a live
    /// view of this map rather than a copy. See TreeMapRangeView.
    [[nodiscard]] TreeMapRangeView<K, V, Compare> subMap(const K& fromKey, const K& toKey);

    /// Java: NavigableMap.subMap(from, fromInclusive, to, toInclusive).
    [[nodiscard]] TreeMapRangeView<K, V, Compare> subMap(
        const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive);

    /// Java: NavigableMap.headMap(to) -- everything strictly below to.
    [[nodiscard]] TreeMapRangeView<K, V, Compare> headMap(const K& toKey);

    /// Java: NavigableMap.headMap(to, inclusive).
    [[nodiscard]] TreeMapRangeView<K, V, Compare> headMap(const K& toKey, bool inclusive);

    /// Java: NavigableMap.tailMap(from) -- from onwards, inclusive.
    [[nodiscard]] TreeMapRangeView<K, V, Compare> tailMap(const K& fromKey);

    /// Java: NavigableMap.tailMap(from, inclusive).
    [[nodiscard]] TreeMapRangeView<K, V, Compare> tailMap(const K& fromKey, bool inclusive);

    /// The read-only forms, built from a const map. Every mutator on the view they
    /// return throws UnsupportedOperationException, mirroring Java's
    /// Collections.unmodifiableNavigableMap(map).subMap(...) chain.
    [[nodiscard]] TreeMapRangeView<K, V, Compare> subMap(const K& fromKey, const K& toKey) const;
    [[nodiscard]] TreeMapRangeView<K, V, Compare> subMap(
        const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive) const;
    [[nodiscard]] TreeMapRangeView<K, V, Compare> headMap(const K& toKey) const;
    [[nodiscard]] TreeMapRangeView<K, V, Compare> headMap(const K& toKey, bool inclusive) const;
    [[nodiscard]] TreeMapRangeView<K, V, Compare> tailMap(const K& fromKey) const;
    [[nodiscard]] TreeMapRangeView<K, V, Compare> tailMap(const K& fromKey, bool inclusive) const;

    /// The type of an independent, descending-order *copy* of this map, for the
    /// same reason as TreeSet::Descending. descendingMap() itself is a view now;
    /// `Descending copy(ascendingMap)` is still how you get one of your own.
    using Descending = TreeMap<K, V, ReversedCompare<K, Compare>>;

    /// Java: NavigableMap.descendingMap(). A live view of this same map in reverse
    /// key order: writes through it land here, a key outside the map's fences is
    /// rejected the same way, and taking descendingMap() twice gives the ascending
    /// order back. Defined below the class body, where TreeMapRangeView is
    /// complete.
    [[nodiscard]] TreeMapRangeView<K, V, Compare> descendingMap();
    [[nodiscard]] TreeMapRangeView<K, V, Compare> descendingMap() const;

    /// Java: NavigableMap.navigableKeySet(), the keys as a live NavigableSet view
    /// in this map's order, and descendingKeySet(), the same keys the other way
    /// round. Both walk the map, so remove() takes the mapping with it.
    [[nodiscard]] TreeMapKeySetView<K, V, Compare> navigableKeySet();
    [[nodiscard]] TreeMapKeySetView<K, V, Compare> navigableKeySet() const;
    [[nodiscard]] TreeMapKeySetView<K, V, Compare> descendingKeySet();
    [[nodiscard]] TreeMapKeySetView<K, V, Compare> descendingKeySet() const;

protected:
    void visitEntries(const std::function<void(const K&, V&)>& action) override {
        for (auto& entry : entries_) {
            action(entry.first, entry.second);
        }
    }

    void visitEntries(const std::function<void(const K&, const V&)>& action) const override {
        for (const auto& entry : entries_) {
            action(entry.first, entry.second);
        }
    }

private:
    void requireNotEmpty() const {
        if (entries_.empty()) {
            throw NoSuchElementException("map is empty");
        }
    }

    /// TreeMapRangeView reads this storage to locate its fences (lower_bound and
    /// upper_bound on the tree, and the fence comparisons in between). Writes never
    /// go through this friendship: they go through the public mutators below so
    /// that the modification counter is bumped in the one place that owns it.
    template <class, class, class>
    friend class TreeMapRangeView;

    Entries entries_;
};

// ---------------------------------------------------------------------------
// Live range views
// ---------------------------------------------------------------------------
//
// The map-side counterpart of TreeSetRangeView, with the same contract: a window
// onto the backing tree rather than a copy of it. Writes through put/remove/
// compute/merge land in the backing map, an operation that would create a mapping
// outside the window throws IllegalArgumentException, and the keySet()/values()/
// entrySet() this view inherits see the window and nothing else, because the
// visitEntries they are built on is filtered here.
//
// Two things differ from the set-side view:
//
//   * a Map hands out no iterator of its own, so iteration happens through those
//     three views rather than through an iterator() of this class;
//   * Map::Entry stays a value type (divergence 12), so setValue() on an entry a
//     view's entrySet() handed out still writes to the copy, not to the map.
//
// The fence rules are java.util.TreeMap.NavigableSubMap's, checked against a JDK
// (section 9): a key is in the window when it lies between the fences honouring
// their inclusivity; a new fence must not widen the window; from > to is rejected.
//
// The view also carries a *direction*, exactly as java.util.TreeMap.DescendingSubMap
// does: the fences stay the ascending pair and one flag decides which way the
// window is walked. descendingMap() therefore hands back the same handle with the
// flag flipped rather than a copy, and every direction-sensitive member swaps its
// two senses to match -- firstKey/lastKey, the four lookups, the entry polls, the
// whole iteration order of keySet()/values()/entrySet(), and the orientation of
// subMap/headMap/tailMap.
template <class K, class V, class Compare>
class TreeMapRangeView final : public Map<K, V> {
public:
    using keyType = K;
    using valueType = V;
    using Entries = TreeMap<K, V, Compare>::Entries;
    using Entry = Map<K, V>::Entry;

    /// A modifiable view, built by the non-const overloads of subMap/headMap/
    /// tailMap/descendingMap. The fences are always the *ascending* pair; only the
    /// direction flag distinguishes an ascending window from a descending one.
    TreeMapRangeView(TreeMap<K, V, Compare>& backing, std::optional<K> from, bool fromInclusive,
        std::optional<K> to, bool toInclusive, bool descending = false)
        : backing_(&backing), writableBacking_(&backing), from_(std::move(from)),
          to_(std::move(to)), fromInclusive_(fromInclusive), toInclusive_(toInclusive),
          descending_(descending) {}

    /// The read-only form, built by the const overloads.
    TreeMapRangeView(const TreeMap<K, V, Compare>& backing, std::optional<K> from,
        bool fromInclusive, std::optional<K> to, bool toInclusive, bool descending = false)
        : backing_(&backing), from_(std::move(from)), to_(std::move(to)),
          fromInclusive_(fromInclusive), toInclusive_(toInclusive), descending_(descending) {}

    /// Reports the backing map's count rather than the view's own: the key/values/
    /// entry views built on top of this one iterate that map, so a structural
    /// change to it has to invalidate them however the caller reached them.
    [[nodiscard]] int modCount() const noexcept override { return backing_->modCount(); }

    /// Java: SubMap.size(), a walk of the window -- the view stores no count, and
    /// the window has the same cardinality whichever way it is walked.
    [[nodiscard]] int size() const override {
        return static_cast<int>(std::distance(firstInRange(), pastLastInRange()));
    }

    [[nodiscard]] bool containsKey(const K& key) const override {
        return inRange(key) && backing_->containsKey(key);
    }

    [[nodiscard]] bool containsValue(const V& value) const override {
        for (auto position = firstInRange(); position != pastLastInRange(); ++position) {
            if (elementEquals(position->second, value)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const V* get(const K& key) const override {
        return inRange(key) ? backing_->get(key) : nullptr;
    }

    [[nodiscard]] V* get(const K& key) override {
        return inRange(key) ? writable().get(key) : nullptr;
    }

    Optional<V> put(const K& key, const V& value) override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        requireInRange(key);
        requireNonNull(value);
        return target.put(key, value);
    }

    Optional<V> put(K&& key, V&& value) override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        requireInRange(key);
        requireNonNull(value);
        return target.put(std::move(key), std::move(value));
    }

    /// Java: SubMap.putIfAbsent. The window is checked even when the key is absent:
    /// this call could install it, which is what Java rejects.
    Optional<V> putIfAbsent(const K& key, const V& value) override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        requireInRange(key);
        requireNonNull(value);
        return target.putIfAbsent(key, value);
    }

    Optional<V> putIfAbsent(K&& key, V&& value) override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        requireInRange(key);
        requireNonNull(value);
        return target.putIfAbsent(std::move(key), std::move(value));
    }

    /// Java: SubMap.remove(Object). A key outside the window simply has no mapping
    /// here, so this answers empty rather than throwing.
    Optional<V> remove(const K& key) override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        if (!inRange(key)) {
            return Optional<V>::empty();
        }
        return target.remove(key);
    }

    /// Removes the mappings inside the window; the rest of the map is untouched,
    /// as Java's SubMap.clear() does.
    void clear() override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        const std::vector<K> doomed = keysInWindow();
        for (const K& key : doomed) {
            static_cast<void>(target.remove(key));
        }
    }

    /// Java: SubMap.computeIfAbsent. Rejected outside the window because it could
    /// install a mapping there.
    Optional<V> computeIfAbsent(
        const K& key, const std::function<Optional<V>(const K&)>& mappingFunction) override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        requireInRange(key);
        return target.computeIfAbsent(key, mappingFunction);
    }

    /// Java: SubMap.computeIfPresent. A key outside the window cannot be present,
    /// so this answers empty rather than throwing -- but it has to answer empty
    /// *here*: delegating would let the backing remap a key the window cannot see,
    /// which is not what Java does. Verified against a JDK: the mapping outside the
    /// window is untouched.
    Optional<V> computeIfPresent(const K& key,
        const std::function<Optional<V>(const K&, const V&)>& remappingFunction) override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        if (!inRange(key)) {
            return Optional<V>::empty();
        }
        return target.computeIfPresent(key, remappingFunction);
    }

    /// Java: SubMap.merge. Rejected outside the window for the same reason
    /// putIfAbsent is.
    Optional<V> merge(const K& key, const V& value,
        const std::function<Optional<V>(const V&, const V&)>& remappingFunction) override {
        this->checkNotFrozen();
        TreeMap<K, V, Compare>& target = writable();
        requireInRange(key);
        requireNonNull(value);
        return target.merge(key, value, remappingFunction);
    }

    // --- NavigableMap queries over the window -------------------------------


    /// C++-only: whether this window can be written through. Java has no equivalent
    /// because its unmodifiable wrappers are classes of their own; here the flag is
    /// one bit on the view, and the key-set view needs to ask, because it holds a
    /// window by value and still has to answer a const iterator's remove() with a
    /// write rather than a compile error.
    [[nodiscard]] bool isWritable() const noexcept { return writableBacking_ != nullptr; }

    /// Java: NavigableMap.comparator(), which for a descending view is the reverse
    /// of the map's own ordering.
    ///
    /// Type-erased for the same reason as TreeSetRangeView::comparator(): a
    /// std-style Compare cannot be reversed, and answering with the ascending
    /// comparer for a descending view would mislead anyone who then sorted with it.
    [[nodiscard]] Comparator<K> comparator() const {
        Comparator<K> base = asComparator();
        return descending_ ? base.reversed() : base;
    }

    /// Java: NavigableMap.firstKey() -- the reference points into the backing map,
    /// which outlives the call, exactly as TreeMap's own firstKey does. It is the
    /// first key *in this view's own order*, so a descending view answers with the
    /// largest key.
    [[nodiscard]] const K& firstKey() const {
        return descending_ ? windowLastKey() : windowFirstKey();
    }

    /// Java: NavigableMap.lastKey().
    [[nodiscard]] const K& lastKey() const {
        return descending_ ? windowFirstKey() : windowLastKey();
    }

    /// Java: NavigableMap.firstEntry(). A copy, like every Entry this library
    /// hands out (divergence 12).
    [[nodiscard]] Optional<Entry> firstEntry() const {
        return entryAt(descending_ ? lastKeyOrEmpty() : firstKeyOrEmpty());
    }

    /// Java: NavigableMap.lastEntry().
    [[nodiscard]] Optional<Entry> lastEntry() const {
        return entryAt(descending_ ? firstKeyOrEmpty() : lastKeyOrEmpty());
    }

    /// Java: NavigableMap.pollFirstEntry(). An empty window yields empty and
    /// changes nothing.
    [[nodiscard]] Optional<Entry> pollFirstEntry() {
        this->checkNotFrozen();
        static_cast<void>(writable());
        return descending_ ? pollWindowLastEntry() : pollWindowFirstEntry();
    }

    /// Java: NavigableMap.pollLastEntry().
    [[nodiscard]] Optional<Entry> pollLastEntry() {
        this->checkNotFrozen();
        static_cast<void>(writable());
        return descending_ ? pollWindowFirstEntry() : pollWindowLastEntry();
    }

    /// Java: NavigableMap.floorKey(key) -- the greatest key of the window that is
    /// <= key. The answer is clamped to the window rather than empty merely because
    /// the argument lies outside it, which is what Java does (verified against a
    /// JDK; see DESIGN.md section 9).
    /// On a descending view the pair swaps, exactly as Java's DescendingSubMap
    /// does, so floorKey is the ascending ceilingKey.
    [[nodiscard]] Optional<K> floorKey(const K& key) const {
        return descending_ ? ascendingCeilingKey(key) : ascendingFloorKey(key);
    }

    /// Java: NavigableMap.lowerKey(key) -- greatest key of the window < key.
    [[nodiscard]] Optional<K> lowerKey(const K& key) const {
        return descending_ ? ascendingHigherKey(key) : ascendingLowerKey(key);
    }

    /// Java: NavigableMap.ceilingKey(key) -- least key of the window >= key.
    [[nodiscard]] Optional<K> ceilingKey(const K& key) const {
        return descending_ ? ascendingFloorKey(key) : ascendingCeilingKey(key);
    }

    /// Java: NavigableMap.higherKey(key) -- least key of the window > key.
    [[nodiscard]] Optional<K> higherKey(const K& key) const {
        return descending_ ? ascendingLowerKey(key) : ascendingHigherKey(key);
    }

    // --- Ranges of a range --------------------------------------------------

    /// Java: NavigableMap.subMap(from, fromInclusive, to, toInclusive) on a view.
    /// The new window may not widen this one, exactly as Java's SubMap requires,
    /// and on a descending view "from" is the *upper* end, so the two bounds are
    /// stored the other way round while the view keeps its direction.
    [[nodiscard]] TreeMapRangeView subMap(
        const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive) {
        requireAcceptableWindow(fromKey, fromInclusive, toKey, toInclusive);
        return makeSubWindowFor(fromKey, fromInclusive, toKey, toInclusive);
    }

    [[nodiscard]] TreeMapRangeView subMap(
        const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive) const {
        requireAcceptableWindow(fromKey, fromInclusive, toKey, toInclusive);
        return makeReadOnlySubWindowFor(fromKey, fromInclusive, toKey, toInclusive);
    }

    [[nodiscard]] TreeMapRangeView subMap(const K& fromKey, const K& toKey) {
        return subMap(fromKey, true, toKey, false);
    }

    [[nodiscard]] TreeMapRangeView subMap(const K& fromKey, const K& toKey) const {
        return subMap(fromKey, true, toKey, false);
    }

    /// Java: NavigableMap.headMap(to, inclusive) on a view: the fence at the far
    /// end is kept and the one at this view's start is replaced. Descending, that
    /// means the *ascending lower* fence moves, because the traversal starts at the
    /// top key.
    [[nodiscard]] TreeMapRangeView headMap(const K& toKey, bool inclusive) {
        requireAcceptedBound(toKey, inclusive, "toKey out of range");
        if (descending_) {
            return makeSubWindow(toKey, inclusive, to_, toInclusive_);
        }
        return makeSubWindow(from_, fromInclusive_, toKey, inclusive);
    }

    [[nodiscard]] TreeMapRangeView headMap(const K& toKey, bool inclusive) const {
        requireAcceptedBound(toKey, inclusive, "toKey out of range");
        if (descending_) {
            return TreeMapRangeView(*backing_, toKey, inclusive, to_, toInclusive_, descending_);
        }
        return TreeMapRangeView(*backing_, from_, fromInclusive_, toKey, inclusive);
    }

    [[nodiscard]] TreeMapRangeView headMap(const K& toKey) { return headMap(toKey, false); }

    [[nodiscard]] TreeMapRangeView headMap(const K& toKey) const { return headMap(toKey, false); }

    /// Java: NavigableMap.tailMap(from, inclusive) on a view: the fence at this
    /// view's start is replaced and the far one is kept. Descending, the traversal
    /// starts at the top key, so the *ascending upper* fence moves.
    [[nodiscard]] TreeMapRangeView tailMap(const K& fromKey, bool inclusive) {
        requireAcceptedBound(fromKey, inclusive, "fromKey out of range");
        if (descending_) {
            return makeSubWindow(from_, fromInclusive_, fromKey, inclusive);
        }
        return makeSubWindow(fromKey, inclusive, to_, toInclusive_);
    }

    [[nodiscard]] TreeMapRangeView tailMap(const K& fromKey, bool inclusive) const {
        requireAcceptedBound(fromKey, inclusive, "fromKey out of range");
        if (descending_) {
            return TreeMapRangeView(*backing_, from_, fromInclusive_, fromKey, inclusive, descending_);
        }
        return TreeMapRangeView(*backing_, fromKey, inclusive, to_, toInclusive_);
    }

    [[nodiscard]] TreeMapRangeView tailMap(const K& fromKey) { return tailMap(fromKey, true); }

    [[nodiscard]] TreeMapRangeView tailMap(const K& fromKey) const {
        return tailMap(fromKey, true);
    }

    // --- The same window, the other way round -------------------------------

    /// Java: NavigableMap.descendingMap() on a view. The same window, walked the
    /// other way; a second call gives the original order back, as it does in Java.
    [[nodiscard]] TreeMapRangeView descendingMap() { return makeFlippedWindow(); }

    [[nodiscard]] TreeMapRangeView descendingMap() const {
        return TreeMapRangeView(*backing_, from_, fromInclusive_, to_, toInclusive_, !descending_);
    }

    /// Java: NavigableMap.navigableKeySet(), the keys of this window in this view's
    /// order, and descendingKeySet(), the same keys the other way round. Both are
    /// declared here and defined below, where TreeMapKeySetView is complete.
    [[nodiscard]] TreeMapKeySetView<K, V, Compare> navigableKeySet();
    [[nodiscard]] TreeMapKeySetView<K, V, Compare> navigableKeySet() const;
    [[nodiscard]] TreeMapKeySetView<K, V, Compare> descendingKeySet();
    [[nodiscard]] TreeMapKeySetView<K, V, Compare> descendingKeySet() const;

protected:
    /// The primitive the key/values/entry views are built on, filtered to the
    /// window and walked in this view's direction, which is what makes
    /// descendingMap().keySet()/values()/entrySet() come out in reverse order. A
    /// read-only view still answers the const overload, which is what lets
    /// `const TreeMapRangeView` be iterated.
    void visitEntries(const std::function<void(const K&, V&)>& action) override {
        static_cast<void>(writable());
        if (!descending_) {
            for (auto position = firstInRangeForWrite(); position != pastLastInRangeForWrite();
                 ++position) {
                action(position->first, position->second);
            }
            return;
        }
        for (auto position = pastLastInRangeForWrite(); position != firstInRangeForWrite();) {
            --position;
            action(position->first, position->second);
        }
    }

    void visitEntries(const std::function<void(const K&, const V&)>& action) const override {
        if (!descending_) {
            for (auto position = firstInRange(); position != pastLastInRange(); ++position) {
                action(position->first, position->second);
            }
            return;
        }
        for (auto position = pastLastInRange(); position != firstInRange();) {
            --position;
            action(position->first, position->second);
        }
    }

private:
    /// A window onto this window. A read-only view only ever produces read-only
    /// views, so `unmodifiableNavigableMap(map).subMap(a, b)` reads rather than
    /// throws, exactly as Java's wrapper does. Which backing to carry is the whole
    /// decision; the new fences are already known to lie inside this window.
    [[nodiscard]] TreeMapRangeView makeSubWindow(const std::optional<K>& from, bool fromInclusive,
        const std::optional<K>& to, bool toInclusive) const {
        if (writableBacking_ == nullptr) {
            return TreeMapRangeView(
                *backing_, from, fromInclusive, to, toInclusive, descending_);
        }
        return TreeMapRangeView(
            *writableBacking_, from, fromInclusive, to, toInclusive, descending_);
    }

    /// subMap()'s window, with the two bounds stored ascending whichever way this
    /// view walks: Java's DescendingSubMap passes the pair to its base constructor
    /// in the opposite order for exactly this reason.
    [[nodiscard]] TreeMapRangeView makeSubWindowFor(const K& fromKey, bool fromInclusive,
        const K& toKey, bool toInclusive) const {
        if (descending_) {
            return makeSubWindow(toKey, toInclusive, fromKey, fromInclusive);
        }
        return makeSubWindow(fromKey, fromInclusive, toKey, toInclusive);
    }

    /// The same, forced read-only. Used by the const overloads, which must not hand
    /// out a writable window however writable this one happens to be.
    [[nodiscard]] TreeMapRangeView makeReadOnlySubWindowFor(const K& fromKey, bool fromInclusive,
        const K& toKey, bool toInclusive) const {
        if (descending_) {
            return TreeMapRangeView(
                *backing_, toKey, toInclusive, fromKey, fromInclusive, descending_);
        }
        return TreeMapRangeView(
            *backing_, fromKey, fromInclusive, toKey, toInclusive, descending_);
    }

    /// The same window walked the other way -- descendingMap().
    [[nodiscard]] TreeMapRangeView makeFlippedWindow() const {
        if (writableBacking_ == nullptr) {
            return TreeMapRangeView(
                *backing_, from_, fromInclusive_, to_, toInclusive_, !descending_);
        }
        return TreeMapRangeView(
            *writableBacking_, from_, fromInclusive_, to_, toInclusive_, !descending_);
    }

    /// The one place a read-only view is rejected. Everything writable funnels here.
    [[nodiscard]] TreeMap<K, V, Compare>& writable() const {
        if (writableBacking_ == nullptr) {
            throw UnsupportedOperationException(
                "this view is read-only: it was taken from a const map");
        }
        return *writableBacking_;
    }

    /// The backing map's ordering, in its own std-style form: the fence comparisons
    /// need it, while the public comparator() hands out the Java-style one. The
    /// friendship is what allows reading the map's key_comp() here.
    [[nodiscard]] Compare comparatorOf() const { return backing_->entries_.key_comp(); }

    /// The map's own ordering as the type-erased Comparator<K> whose compare()
    /// contract is Java's.
    [[nodiscard]] Comparator<K> asComparator() const {
        return comparatorFrom<K>(comparatorOf());
    }

    /// The four lookups as the *ascending* window answers them. floorKey/lowerKey/
    /// ceilingKey/higherKey pick a pair out of these, which is how a descending
    /// view swaps them without duplicating the clamping rules.
    [[nodiscard]] Optional<K> ascendingFloorKey(const K& key) const {
        return clampDownward(backing_->floorKey(key));
    }

    [[nodiscard]] Optional<K> ascendingLowerKey(const K& key) const {
        return clampDownward(backing_->lowerKey(key));
    }

    [[nodiscard]] Optional<K> ascendingCeilingKey(const K& key) const {
        return clampUpward(backing_->ceilingKey(key));
    }

    [[nodiscard]] Optional<K> ascendingHigherKey(const K& key) const {
        return clampUpward(backing_->higherKey(key));
    }

    /// Java: NavigableMap.firstKey()/lastKey() as the *ascending* window answers
    /// them; the two public accessors pick one according to the direction.
    [[nodiscard]] const K& windowFirstKey() const {
        const auto position = firstInRange();
        if (position == pastLastInRange()) {
            throw NoSuchElementException("map is empty");
        }
        return position->first;
    }

    [[nodiscard]] const K& windowLastKey() const {
        const auto position = pastLastInRange();
        if (position == firstInRange()) {
            throw NoSuchElementException("map is empty");
        }
        return std::prev(position)->first;
    }

    /// An Entry value for a key of this window, or empty. Entries are copies
    /// (divergence 12), so they can be built from anywhere.
    [[nodiscard]] Optional<Entry> entryAt(const Optional<K>& key) const {
        if (key.isEmpty()) {
            return Optional<Entry>::empty();
        }
        const V* value = backing_->get(key.get());
        return Optional<Entry>::of(Entry(key.get(), *value));
    }

    [[nodiscard]] Optional<Entry> pollWindowFirstEntry() {
        TreeMap<K, V, Compare>& target = writable();
        const Optional<K> key = firstKeyOrEmpty();
        if (key.isEmpty()) {
            return Optional<Entry>::empty();
        }
        const K removedKey = key.get();
        Optional<V> value = target.remove(removedKey);
        if (value.isEmpty()) {
            return Optional<Entry>::empty();
        }
        return Optional<Entry>::of(Entry(removedKey, std::move(value).get()));
    }

    [[nodiscard]] Optional<Entry> pollWindowLastEntry() {
        TreeMap<K, V, Compare>& target = writable();
        const Optional<K> key = lastKeyOrEmpty();
        if (key.isEmpty()) {
            return Optional<Entry>::empty();
        }
        const K removedKey = key.get();
        Optional<V> value = target.remove(removedKey);
        if (value.isEmpty()) {
            return Optional<Entry>::empty();
        }
        return Optional<Entry>::of(Entry(removedKey, std::move(value).get()));
    }

    /// Java: NavigableSubMap.inRange(key) -- is this key between the fences?
    [[nodiscard]] bool inRange(const K& key) const {
        return !belowLowerFence(key) && !aboveUpperFence(key);
    }

    [[nodiscard]] bool belowLowerFence(const K& key) const {
        if (!from_.has_value()) {
            return false;
        }
        const Compare& less = comparatorOf();
        if (less(key, *from_)) {
            return true;
        }
        return !fromInclusive_ && !less(*from_, key);
    }

    [[nodiscard]] bool aboveUpperFence(const K& key) const {
        if (!to_.has_value()) {
            return false;
        }
        const Compare& less = comparatorOf();
        if (less(*to_, key)) {
            return true;
        }
        return !toInclusive_ && !less(key, *to_);
    }

    /// Java: the check NavigableSubMap applies to a *new* fence. A bound is
    /// rejected when it would widen the window: beyond a fence, or equal to an
    /// exclusive fence while claiming to include it.
    [[nodiscard]] bool acceptsBound(const K& bound, bool inclusive) const {
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

    void requireAcceptedBound(const K& bound, bool inclusive, const char* message) const {
        requireNonNull(bound);
        if (!acceptsBound(bound, inclusive)) {
            throw IllegalArgumentException(message);
        }
    }

    void requireAcceptableWindow(const K& fromKey, bool fromInclusive, const K& toKey,
        bool toInclusive) const {
        requireAcceptedBound(fromKey, fromInclusive, "fromKey out of range");
        requireAcceptedBound(toKey, toInclusive, "toKey out of range");
        // Java tests the *ascending* pair, because that is what its submap
        // constructor is handed: for a descending window the arguments arrive
        // swapped, so "from > to" flips with them.
        const Compare& less = comparatorOf();
        const bool backwards = descending_ ? less(fromKey, toKey) : less(toKey, fromKey);
        if (backwards) {
            throw IllegalArgumentException("fromKey > toKey");
        }
    }

    void requireInRange(const K& key) const {
        requireNonNull(key);
        if (!inRange(key)) {
            throw IllegalArgumentException("key out of range");
        }
    }

    /// A candidate that fell below the window collapses to empty; one that fell
    /// above it collapses to the window's last key. That clamping is Java's,
    /// verified against a JDK -- see floorKey()/lowerKey() above.
    [[nodiscard]] Optional<K> clampDownward(Optional<K> candidate) const {
        if (candidate.isEmpty()) {
            return candidate;
        }
        if (belowLowerFence(candidate.get())) {
            return Optional<K>::empty();
        }
        if (!inRange(candidate.get())) {
            return lastKeyOrEmpty();
        }
        return candidate;
    }

    [[nodiscard]] Optional<K> clampUpward(Optional<K> candidate) const {
        if (candidate.isEmpty()) {
            return candidate;
        }
        if (aboveUpperFence(candidate.get())) {
            return Optional<K>::empty();
        }
        if (!inRange(candidate.get())) {
            return firstKeyOrEmpty();
        }
        return candidate;
    }

    [[nodiscard]] Optional<K> firstKeyOrEmpty() const {
        const auto position = firstInRange();
        if (position == pastLastInRange()) {
            return Optional<K>::empty();
        }
        return Optional<K>::of(position->first);
    }

    [[nodiscard]] Optional<K> lastKeyOrEmpty() const {
        const auto position = pastLastInRange();
        if (position == firstInRange()) {
            return Optional<K>::empty();
        }
        return Optional<K>::of(std::prev(position)->first);
    }

    [[nodiscard]] std::vector<K> keysInWindow() const {
        std::vector<K> keys;
        keys.reserve(static_cast<std::size_t>(size()));
        for (auto position = firstInRange(); position != pastLastInRange(); ++position) {
            keys.push_back(position->first);
        }
        return keys;
    }

    /// Both helpers are templates over the owner's constness, so a read-only view
    /// gets const_iterators and a modifiable one gets iterators without a
    /// const_cast in either direction.
    ///
    /// `firstAtOrAboveLowerFenceOf` is the raw lower boundary, with the upper one
    /// ignored: the candidate start of the window. Whether it really is inside is
    /// the emptiness question answered below.
    template <class Owner>
    [[nodiscard]] auto firstAtOrAboveLowerFenceOf(Owner& owner) const {
        auto& entries = owner.entries_;
        if (!from_.has_value()) {
            return entries.begin();
        }
        return fromInclusive_ ? entries.lower_bound(*from_) : entries.upper_bound(*from_);
    }

    /// Java: NavigableSubMap's inRange test, applied to the candidate start. A
    /// window can be accepted with its two fences a hair apart -- Java allows
    /// `descending.headMap(k, false)` even when that yields the *empty* window
    /// [k, k), because an exclusive bound only has to lie in the closed range --
    /// and then the candidate start sits above the upper fence with no entry in
    /// the window at all.
    ///
    /// Deciding emptiness explicitly is also what keeps firstInRange() and
    /// pastLastInRange() mutually reachable: in an inverted window the raw bounds
    /// are reversed (upper_bound(k) past lower_bound(k)), so `std::distance` and
    /// the descending `--cursor_` walk would otherwise run off the end of the
    /// map. Both helpers collapse such a window to [end(), end()).
    template <class Owner>
    [[nodiscard]] bool windowIsEmptyOf(Owner& owner) const {
        auto& entries = owner.entries_;
        const auto candidate = firstAtOrAboveLowerFenceOf(owner);
        return candidate == entries.end() || aboveUpperFence(candidate->first);
    }

    template <class Owner>
    [[nodiscard]] auto firstInRangeOf(Owner& owner) const {
        auto& entries = owner.entries_;
        if (windowIsEmptyOf(owner)) {
            return entries.end();
        }
        return firstAtOrAboveLowerFenceOf(owner);
    }

    template <class Owner>
    [[nodiscard]] auto pastLastInRangeOf(Owner& owner) const {
        auto& entries = owner.entries_;
        if (firstInRangeOf(owner) == entries.end()) {
            return entries.end();
        }
        if (!to_.has_value()) {
            return entries.end();
        }
        return toInclusive_ ? entries.upper_bound(*to_) : entries.lower_bound(*to_);
    }

    [[nodiscard]] Entries::const_iterator firstInRange() const { return firstInRangeOf(*backing_); }

    [[nodiscard]] Entries::const_iterator pastLastInRange() const {
        return pastLastInRangeOf(*backing_);
    }

    [[nodiscard]] Entries::iterator firstInRangeForWrite() { return firstInRangeOf(writable()); }

    [[nodiscard]] Entries::iterator pastLastInRangeForWrite() {
        return pastLastInRangeOf(writable());
    }

    const TreeMap<K, V, Compare>* backing_;
    /// Null for a view of a const map; every mutator checks it.
    TreeMap<K, V, Compare>* writableBacking_ = nullptr;
    std::optional<K> from_;
    std::optional<K> to_;
    bool fromInclusive_ = true;
    bool toInclusive_ = false;
    /// The fences above are always the ascending pair; this is the only thing that
    /// distinguishes a descending window from an ascending one.
    bool descending_ = false;
};

// ---------------------------------------------------------------------------
// The NavigableSet of keys
// ---------------------------------------------------------------------------
//
// Java's TreeMap.navigableKeySet() hands back a KeySet that implements
// NavigableSet by delegating every navigable query to the map's own (firstKey,
// floorKey, subMap, ...), and whose iteration order is the map's own
// keyIterator(). This is that class, built the same way: it holds a TreeMapRangeView
// *by value* -- which is only a handle, a root pointer plus fences and a direction,
// so copying one costs nothing and cannot dangle -- and delegates to it.
//
// Two consequences worth stating:
//
//   * add() throws UnsupportedOperationException, as Java's does: a key with no
//     value is not something a Map can represent. remove() takes the mapping with
//     it, because that is what removing a key from a map means.
//   * descendingSet() is descendingKeySet(), and vice versa; both are views, so
//     removing through one removes from the map.
//
// Iteration snapshots the keys, exactly as KeySetView does, and fail-fast is
// answered by the window's modCount() -- which is the backing map's, so any
// structural change anywhere underneath invalidates the iterator.
template <class K, class V, class Compare>
class TreeMapKeySetView final : public AbstractSet<K> {
public:
    using keyType = K;
    using valueType = V;
    using Entry = Map<K, V>::Entry;

    /// Built by TreeMap::navigableKeySet()/descendingKeySet() and by the same two
    /// methods on a range view. The window is a handle -- a root pointer plus
    /// fences and a direction -- so holding one by value costs nothing and cannot
    /// dangle, which is why no intermediate view has to outlive this set.
    explicit TreeMapKeySetView(TreeMapRangeView<K, V, Compare> window)
        : window_(std::move(window)), writable_(window_.isWritable() ? &window_ : nullptr) {}

    [[nodiscard]] int size() const override { return window_.size(); }

    [[nodiscard]] bool isEmpty() const override { return window_.isEmpty(); }

    [[nodiscard]] bool contains(const K& key) const override { return window_.containsKey(key); }

    /// The window's count is the backing map's, for the same reason a range view's
    /// is: this set iterates that map.
    [[nodiscard]] int modCount() const noexcept override { return window_.modCount(); }

    /// Java: TreeMap.KeySet.add, which throws.
    bool add(const K&) override { throw unsupported(); }

    bool add(K&&) override { throw unsupported(); }

    /// Java: KeySet.remove(key), which removes the *mapping*, not merely the key.
    bool remove(const K& key) override { return window_.remove(key).isPresent(); }

    void clear() override { window_.clear(); }

    /// Sentinel for "no element has been returned yet".
    static constexpr std::size_t noElement = static_cast<std::size_t>(-1);

    [[nodiscard]] std::unique_ptr<Iterator<K>> iterator() override {
        return std::make_unique<KeyIterator>(*this, false);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const K>> constIterator() const override {
        return std::make_unique<ConstKeyIterator>(*this, false);
    }

    /// Java: NavigableSet.descendingIterator() -- this key set walked the other
    /// way, which on an already-descending key set means forwards.
    [[nodiscard]] std::unique_ptr<Iterator<K>> descendingIterator() {
        return std::make_unique<KeyIterator>(*this, true);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const K>> descendingConstIterator() const {
        return std::make_unique<ConstKeyIterator>(*this, true);
    }

    class ConstKeyIterator final : public Iterator<const K> {
    public:
        explicit ConstKeyIterator(const TreeMapKeySetView& owner, bool descending)
            : owner_(&owner), keys_(owner.snapshotKeys(descending)),
              expectedModCount_(owner.window_.modCount()) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < keys_.size(); }

        const K& next() override {
            checkModification();
            if (cursor_ >= keys_.size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = cursor_;
            return keys_[cursor_++];
        }

        void remove() override {
            checkModification();
            if (lastReturned_ == noElement) {
                throw IllegalStateException("remove() called before next()");
            }
            static_cast<void>(owner_->writable().remove(keys_[lastReturned_]));
            expectedModCount_ = owner_->window_.modCount();
            lastReturned_ = noElement;
        }

    private:
        void checkModification() const {
            if (owner_->window_.modCount() != expectedModCount_) {
                throw ConcurrentModificationException("map modified during iteration");
            }
        }

        const TreeMapKeySetView* owner_;
        std::vector<K> keys_;
        std::size_t cursor_ = 0;
        std::size_t lastReturned_ = noElement;
        int expectedModCount_;
    };

    /// The mutable form. The snapshot lives in a non-const member, so handing out
    /// `K&` needs no cast -- but a key from the *set* is a copy, which is one more
    /// place where the map's own key stays off limits.
    class KeyIterator final : public Iterator<K> {
    public:
        explicit KeyIterator(TreeMapKeySetView& owner, bool descending)
            : owner_(&owner), keys_(owner.snapshotKeys(descending)),
              expectedModCount_(owner.window_.modCount()) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < keys_.size(); }

        K& next() override {
            checkModification();
            if (cursor_ >= keys_.size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = cursor_;
            return keys_[cursor_++];
        }

        void remove() override {
            checkModification();
            if (lastReturned_ == noElement) {
                throw IllegalStateException("remove() called before next()");
            }
            static_cast<void>(owner_->writable().remove(keys_[lastReturned_]));
            expectedModCount_ = owner_->window_.modCount();
            lastReturned_ = noElement;
        }

    private:
        void checkModification() const {
            if (owner_->window_.modCount() != expectedModCount_) {
                throw ConcurrentModificationException("map modified during iteration");
            }
        }

        TreeMapKeySetView* owner_;
        std::vector<K> keys_;
        std::size_t cursor_ = 0;
        std::size_t lastReturned_ = noElement;
        int expectedModCount_;
    };

    // --- NavigableSet ------------------------------------------------------

    /// Java: NavigableSet.comparator(), inherited from the map view so that a
    /// descending key set reports the reversed ordering.
    [[nodiscard]] Comparator<K> comparator() const { return window_.comparator(); }

    /// Java: NavigableSet.first() -- the first key in this key set's own order.
    /// Throws NoSuchElementException when the window is empty.
    [[nodiscard]] const K& first() const { return window_.firstKey(); }

    /// Java: NavigableSet.last().
    [[nodiscard]] const K& last() const { return window_.lastKey(); }

    /// Java: NavigableSet.pollFirst(). Removing a key removes its mapping, which is
    /// exactly what the window's pollFirstEntry does.
    [[nodiscard]] Optional<K> pollFirst() { return keyOf(window_.pollFirstEntry()); }

    /// Java: NavigableSet.pollLast().
    [[nodiscard]] Optional<K> pollLast() { return keyOf(window_.pollLastEntry()); }

    /// The four directional lookups, delegated to the window so that the clamping
    /// and the descending pair-swap happen in the one place that owns them.
    [[nodiscard]] Optional<K> floor(const K& key) const { return window_.floorKey(key); }
    [[nodiscard]] Optional<K> lower(const K& key) const { return window_.lowerKey(key); }
    [[nodiscard]] Optional<K> ceiling(const K& key) const { return window_.ceilingKey(key); }
    [[nodiscard]] Optional<K> higher(const K& key) const { return window_.higherKey(key); }

    // --- Ranges of a key set -----------------------------------------------

    /// Java: KeySet.subSet(from, fromInclusive, to, toInclusive), which is
    /// subMap(...).navigableKeySet(): the window check and the orientation both
    /// live on the map view.
    [[nodiscard]] TreeMapKeySetView subSet(
        const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive) {
        return TreeMapKeySetView(window_.subMap(fromKey, fromInclusive, toKey, toInclusive));
    }

    [[nodiscard]] TreeMapKeySetView subSet(
        const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive) const {
        return TreeMapKeySetView(window_.subMap(fromKey, fromInclusive, toKey, toInclusive));
    }

    [[nodiscard]] TreeMapKeySetView subSet(const K& fromKey, const K& toKey) {
        return subSet(fromKey, true, toKey, false);
    }

    [[nodiscard]] TreeMapKeySetView subSet(const K& fromKey, const K& toKey) const {
        return subSet(fromKey, true, toKey, false);
    }

    [[nodiscard]] TreeMapKeySetView headSet(const K& toKey, bool inclusive) {
        return TreeMapKeySetView(window_.headMap(toKey, inclusive));
    }

    [[nodiscard]] TreeMapKeySetView headSet(const K& toKey, bool inclusive) const {
        return TreeMapKeySetView(window_.headMap(toKey, inclusive));
    }

    [[nodiscard]] TreeMapKeySetView headSet(const K& toKey) { return headSet(toKey, false); }

    [[nodiscard]] TreeMapKeySetView headSet(const K& toKey) const { return headSet(toKey, false); }

    [[nodiscard]] TreeMapKeySetView tailSet(const K& fromKey, bool inclusive) {
        return TreeMapKeySetView(window_.tailMap(fromKey, inclusive));
    }

    [[nodiscard]] TreeMapKeySetView tailSet(const K& fromKey, bool inclusive) const {
        return TreeMapKeySetView(window_.tailMap(fromKey, inclusive));
    }

    [[nodiscard]] TreeMapKeySetView tailSet(const K& fromKey) { return tailSet(fromKey, true); }

    [[nodiscard]] TreeMapKeySetView tailSet(const K& fromKey) const {
        return tailSet(fromKey, true);
    }

    /// Java: KeySet.descendingSet(), which is descendingMap().navigableKeySet().
    [[nodiscard]] TreeMapKeySetView descendingSet() {
        return TreeMapKeySetView(window_.descendingMap());
    }

    [[nodiscard]] TreeMapKeySetView descendingSet() const {
        return TreeMapKeySetView(window_.descendingMap());
    }

private:
    static UnsupportedOperationException unsupported() {
        return UnsupportedOperationException("keySet does not support add; put a mapping instead");
    }

    /// The one place a read-only key set is rejected. Its const iterator funnels
    /// here, exactly as KeySetView's does.
    [[nodiscard]] TreeMapRangeView<K, V, Compare>& writable() const {
        if (writable_ == nullptr) {
            throw UnsupportedOperationException(
                "this view is read-only: it was taken from a const map");
        }
        return *writable_;
    }

    /// The keys in this window, in this window's order -- or reversed, for
    /// descendingIterator(). Taking the snapshot through keySet() is what keeps the
    /// direction in one place: that view is built on visitEntries, which already
    /// walks the window the way the map does.
    [[nodiscard]] std::vector<K> snapshotKeys(bool descending) const {
        std::vector<K> keys;
        keys.reserve(static_cast<std::size_t>(window_.size()));
        for (const K& key : window_.keySet()) {
            keys.push_back(key);
        }
        if (descending) {
            std::reverse(keys.begin(), keys.end());
        }
        return keys;
    }

    [[nodiscard]] static Optional<K> keyOf(Optional<Entry> entry) {
        if (entry.isEmpty()) {
            return Optional<K>::empty();
        }
        return Optional<K>::of(entry.get().getKey());
    }

    TreeMapRangeView<K, V, Compare> window_;
    /// Points at window_ when that window can be written through; null otherwise.
    TreeMapRangeView<K, V, Compare>* writable_;
};

// ---------------------------------------------------------------------------
// TreeMap range accessors
// ---------------------------------------------------------------------------
//
// Defined down here because the return type TreeMapRangeView is incomplete inside
// the class body above -- the same reason Map::keySet() is defined at the bottom
// of Map.h. The whole-window checks (null keys, fromKey > toKey) live here, on the
// container; a window taken from another window re-checks both fences in
// TreeMapRangeView itself.
//
// descendingMap(), navigableKeySet() and descendingKeySet() are down here for the
// same incompleteness reason: they hand out views too.

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::subMap(
    const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive) {
    requireNonNull(fromKey);
    requireNonNull(toKey);
    const Compare& less = entries_.key_comp();
    if (less(toKey, fromKey)) {
        throw IllegalArgumentException("fromKey > toKey");
    }
    return TreeMapRangeView<K, V, Compare>(
        *this, fromKey, fromInclusive, toKey, toInclusive);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::subMap(
    const K& fromKey, bool fromInclusive, const K& toKey, bool toInclusive) const {
    requireNonNull(fromKey);
    requireNonNull(toKey);
    const Compare& less = entries_.key_comp();
    if (less(toKey, fromKey)) {
        throw IllegalArgumentException("fromKey > toKey");
    }
    return TreeMapRangeView<K, V, Compare>(
        *this, fromKey, fromInclusive, toKey, toInclusive);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::subMap(const K& fromKey, const K& toKey) {
    return subMap(fromKey, true, toKey, false);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::subMap(
    const K& fromKey, const K& toKey) const {
    return subMap(fromKey, true, toKey, false);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::headMap(const K& toKey, bool inclusive) {
    requireNonNull(toKey);
    return TreeMapRangeView<K, V, Compare>(*this, std::nullopt, true, toKey, inclusive);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::headMap(
    const K& toKey, bool inclusive) const {
    requireNonNull(toKey);
    return TreeMapRangeView<K, V, Compare>(*this, std::nullopt, true, toKey, inclusive);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::headMap(const K& toKey) {
    return headMap(toKey, false);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::headMap(const K& toKey) const {
    return headMap(toKey, false);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::tailMap(const K& fromKey, bool inclusive) {
    requireNonNull(fromKey);
    return TreeMapRangeView<K, V, Compare>(*this, fromKey, inclusive, std::nullopt, false);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::tailMap(
    const K& fromKey, bool inclusive) const {
    requireNonNull(fromKey);
    return TreeMapRangeView<K, V, Compare>(*this, fromKey, inclusive, std::nullopt, false);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::tailMap(const K& fromKey) {
    return tailMap(fromKey, true);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::tailMap(const K& fromKey) const {
    return tailMap(fromKey, true);
}

// ---------------------------------------------------------------------------
// Descending maps and the navigable key sets
// ---------------------------------------------------------------------------
//
// Also defined down here: every one of these hands back a view, and neither
// TreeMapRangeView nor TreeMapKeySetView is complete inside the class bodies
// above. The container-level window checks (null keys, fromKey > toKey) stay in
// the subMap/headMap/tailMap definitions; these all build an unbounded window or
// flip one that already exists.

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::descendingMap() {
    return TreeMapRangeView<K, V, Compare>(*this, std::nullopt, true, std::nullopt, false, true);
}

template <class K, class V, class Compare>
TreeMapRangeView<K, V, Compare> TreeMap<K, V, Compare>::descendingMap() const {
    return TreeMapRangeView<K, V, Compare>(*this, std::nullopt, true, std::nullopt, false, true);
}

template <class K, class V, class Compare>
TreeMapKeySetView<K, V, Compare> TreeMap<K, V, Compare>::navigableKeySet() {
    return TreeMapKeySetView<K, V, Compare>(descendingMap().descendingMap());
}

template <class K, class V, class Compare>
TreeMapKeySetView<K, V, Compare> TreeMap<K, V, Compare>::navigableKeySet() const {
    return TreeMapKeySetView<K, V, Compare>(descendingMap().descendingMap());
}

template <class K, class V, class Compare>
TreeMapKeySetView<K, V, Compare> TreeMap<K, V, Compare>::descendingKeySet() {
    return TreeMapKeySetView<K, V, Compare>(descendingMap());
}

template <class K, class V, class Compare>
TreeMapKeySetView<K, V, Compare> TreeMap<K, V, Compare>::descendingKeySet() const {
    return TreeMapKeySetView<K, V, Compare>(descendingMap());
}

template <class K, class V, class Compare>
TreeMapKeySetView<K, V, Compare> TreeMapRangeView<K, V, Compare>::navigableKeySet() {
    return TreeMapKeySetView<K, V, Compare>(*this);
}

template <class K, class V, class Compare>
TreeMapKeySetView<K, V, Compare> TreeMapRangeView<K, V, Compare>::navigableKeySet() const {
    return TreeMapKeySetView<K, V, Compare>(*this);
}

template <class K, class V, class Compare>
TreeMapKeySetView<K, V, Compare> TreeMapRangeView<K, V, Compare>::descendingKeySet() {
    return TreeMapKeySetView<K, V, Compare>(descendingMap());
}

template <class K, class V, class Compare>
TreeMapKeySetView<K, V, Compare> TreeMapRangeView<K, V, Compare>::descendingKeySet() const {
    return TreeMapKeySetView<K, V, Compare>(descendingMap());
}

}  // namespace cppstream
