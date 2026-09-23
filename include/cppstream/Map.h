#pragma once

#include <cppstream/AbstractSet.h>
#include <cppstream/Elements.h>
#include <cppstream/Optional.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cppstream {

// The live views a map hands out. They are defined after Map because they call
// its members, and befriended by Map because building a key snapshot needs
// visitEntries, which is protected. See the comment block above them.
template <class K, class V>
class KeySetView;

template <class K, class V>
class ValuesView;

template <class K, class V>
class EntrySetView;

template <class K, class V, class Compare>
class TreeMapRangeView;

/// A port of java.util.Map.
///
/// Java splits this in two: the Map interface and AbstractMap, which implements
/// everything in terms of one primitive, entrySet().iterator(). The design keeps
/// a single file, so Map plays both roles: it declares the primitives pure
/// virtual, and implements the algorithms that can be derived from them
/// (equals, hashCode, forEach) directly on top. keySet()/values()/entrySet() are
/// the live view classes below.
///
/// The derived members are deliberately non-virtual. A pure virtual keySet()
/// would be instantiated with HashMap/TreeMap and force every TreeMap key to be
/// hashable even when nobody asked for a key set. As ordinary members they are
/// instantiated on use, which is the rule every other "extra" operation in this
/// library follows.
///
/// Return types: get() hands back a pointer, because the value stays in the map
/// and a pointer is the honest way to say "here it is, or nullptr". put() and
/// friends hand back Optional<V>, because the previous value leaves the map and a
/// copy has to be produced either way -- Java's `null` is exactly Optional::empty.
template <class K, class V>
class Map {
public:
    using keyType = K;
    using valueType = V;

    /// A port of java.util.Map.Entry.
    ///
    /// A value, not a view. keySet/values/entrySet are live views now, but the
    /// iterator they hand out materialises each entry, so setValue() writes to
    /// that copy and the map is untouched, where Java's live node writes through.
    /// This is the one remaining gap in the entry view (divergence 12).
    class Entry {
    public:
        Entry(K key, V value) : key_(std::move(key)), value_(std::move(value)) {}

        [[nodiscard]] const K& getKey() const noexcept { return key_; }
        [[nodiscard]] const V& getValue() const noexcept { return value_; }

        /// Java: Entry.setValue(value). Returns the value it replaced.
        V setValue(V value) {
            V previous = std::move(value_);
            value_ = std::move(value);
            return previous;
        }

        [[nodiscard]] bool equals(const Entry& other) const {
            return elementEquals(key_, other.key_) && elementEquals(value_, other.value_);
        }

        /// Java: Map.Entry.equals(Object), which the interface defines as "the same
        /// key and the same value". Spelled as operator== as well so that Entry is
        /// usable with the C++ operators users reach for first.
        [[nodiscard]] friend bool operator==(const Entry& left, const Entry& right) {
            return left.equals(right);
        }

        /// Java: Map.Entry.hashCode(), the xor of the two component hash codes.
        [[nodiscard]] std::size_t hashCode() const {
            return elementHash(key_) ^ elementHash(value_);
        }

    private:
        K key_;
        V value_;
    };

    virtual ~Map() = default;

    [[nodiscard]] virtual int size() const = 0;

    [[nodiscard]] virtual bool isEmpty() const { return size() == 0; }

    [[nodiscard]] virtual bool containsKey(const K& key) const = 0;
    [[nodiscard]] virtual bool containsValue(const V& value) const = 0;

    /// Java: Map.get(key), which returns null when absent.
    [[nodiscard]] virtual const V* get(const K& key) const = 0;
    [[nodiscard]] virtual V* get(const K& key) = 0;

    /// Java: Map.getOrDefault(key, defaultValue).
    [[nodiscard]] virtual V getOrDefault(const K& key, const V& defaultValue) const {
        const V* found = get(key);
        return found != nullptr ? *found : defaultValue;
    }

    /// Java: Map.put(key, value), which returns the previous value or null.
    [[nodiscard]] virtual Optional<V> put(const K& key, const V& value) = 0;
    [[nodiscard]] virtual Optional<V> put(K&& key, V&& value) = 0;

    /// Java: Map.putIfAbsent(key, value). Returns the value already present, or
    /// empty when this call installed it.
    [[nodiscard]] virtual Optional<V> putIfAbsent(const K& key, const V& value) = 0;
    [[nodiscard]] virtual Optional<V> putIfAbsent(K&& key, V&& value) = 0;

    virtual void putAll(const Map<K, V>& other) {
        other.visitEntries([this](const K& key, const V& value) { this->put(key, value); });
    }

    /// Java: Map.remove(key).
    [[nodiscard]] virtual Optional<V> remove(const K& key) = 0;

    virtual void clear() = 0;

    /// Java: Map.computeIfAbsent(key, mappingFunction).
    ///
    /// The mapping function returns Optional<V> so that "do not map this key" is
    /// expressible -- that is Java's `return null`. An ordinary V converts
    /// implicitly, so the common case still reads like Java's.
    [[nodiscard]] virtual Optional<V> computeIfAbsent(
        const K& key, const std::function<Optional<V>(const K&)>& mappingFunction) = 0;

    /// Java: Map.computeIfPresent(key, remappingFunction). Empty removes the entry.
    [[nodiscard]] virtual Optional<V> computeIfPresent(
        const K& key, const std::function<Optional<V>(const K&, const V&)>& remappingFunction) = 0;

    /// Java: Map.merge(key, value, remappingFunction). Empty removes the entry.
    [[nodiscard]] virtual Optional<V> merge(const K& key, const V& value,
        const std::function<Optional<V>(const V&, const V&)>& remappingFunction) = 0;

    /// Java: Map.forEach(action). The action receives a mutable reference to the
    /// value, matching Java's reference semantics.
    virtual void forEach(const std::function<void(const K&, V&)>& action) { visitEntries(action); }

    // --- Live views (divergence 12 is gone) ---------------------------------

    /// Java: Map.keySet(), a live Set view of the keys.
    ///
    /// Returned by value, not by pointer: the view is stateless (one pointer to
    /// the map), so copying it costs nothing and Java's habit of handing back the
    /// same object every call is not worth a lifetime hazard here.
    ///
    /// The const overload returns the same type holding a read-only map, so a view
    /// of a const map rejects every mutator. Return types have to be concrete
    /// (divergence 9), which is why this cannot be `std::unique_ptr<Set<K>>`.
    [[nodiscard]] KeySetView<K, V> keySet();
    [[nodiscard]] KeySetView<K, V> keySet() const;

    /// Java: Map.values(), a live Collection view of the values.
    [[nodiscard]] ValuesView<K, V> values();
    [[nodiscard]] ValuesView<K, V> values() const;

    /// Java: Map.entrySet(), a live Set view of the mappings.
    ///
    /// The entries themselves are copies -- Map::Entry is a value type here, so
    /// Entry::setValue writes to the copy and not to the map, unlike Java's live
    /// Node. Everything else about the view is live: size, membership, removal and
    /// iteration all read the map as it is right now.
    [[nodiscard]] EntrySetView<K, V> entrySet();
    [[nodiscard]] EntrySetView<K, V> entrySet() const;

    /// Java: AbstractMap.equals(Object): same size, and every key maps to an equal
    /// value in the other map.
    [[nodiscard]] virtual bool equals(const Map<K, V>& other) const {
        if (std::addressof(other) == this) {
            return true;
        }
        if (size() != other.size()) {
            return false;
        }
        bool equal = true;
        visitEntries([&other, &equal](const K& key, const V& value) {
            const V* theirs = other.get(key);
            if (theirs == nullptr || !elementEquals(*theirs, value)) {
                equal = false;
            }
        });
        return equal;
    }

    /// Java: AbstractMap.hashCode() -- the sum of the entry hash codes.
    [[nodiscard]] virtual std::size_t hashCode() const {
        std::size_t result = 0;
        visitEntries([&result](const K& key, const V& value) {
            result += elementHash(key) ^ elementHash(value);
        });
        return result;
    }

    /// C++-only: the structural-modification count, exactly like
    /// AbstractCollection's.
    ///
    /// "Structural" means the *set of mappings* changed. Overwriting an existing
    /// key is not structural, which is what java.util.HashMap does too, so
    /// `put(k, newValue)` inside a loop over keySet() stays legal. The map views
    /// read this to fail fast.
    ///
    /// Virtual so that a live range view can answer with the count of the map it
    /// is a window onto. That matters because keySet()/values()/entrySet() on such
    /// a view are built against the view, yet they iterate the backing map's
    /// storage -- a change to the backing has to invalidate them.
    [[nodiscard]] virtual int modCount() const noexcept { return modCount_; }

    /// C++-only: renders the map unmodifiable in place. See AbstractCollection's
    /// freeze() for why a flag beats Java's wrapper class here.
    void freeze() noexcept { frozen_ = true; }

    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

    void checkNotFrozen() const {
        if (frozen_) {
            throw UnsupportedOperationException("this map is unmodifiable");
        }
    }

protected:
    /// Bumped by the concrete maps whenever the number of mappings changes. See
    /// modCount() above.
    void bumpModCount() noexcept { ++modCount_; }

    /// The one primitive every Map must supply, in the two constnesses.
    ///
    /// C++-only: Java's AbstractMap gets away with entrySet().iterator(), but here
    /// the entry view is built *from* this, so the dependency has to point the
    /// other way. Overriding both overloads in the concrete class keeps both
    /// visible; overriding only one would hide the other.
    virtual void visitEntries(const std::function<void(const K&, V&)>& action) = 0;
    virtual void visitEntries(const std::function<void(const K&, const V&)>& action) const = 0;

private:
    /// Building a key snapshot needs visitEntries, which is protected and must
    /// stay that way: it is an implementation primitive, not API.
    template <class, class>
    friend class KeySetView;
    template <class, class>
    friend class ValuesView;
    template <class, class>
    friend class EntrySetView;

    bool frozen_ = false;
    int modCount_ = 0;
};

// ---------------------------------------------------------------------------
// Live views
// ---------------------------------------------------------------------------
//
// Java defines these three views as nested classes of HashMap, backed by the
// table, and that is essentially what these are: one pointer back to the map and
// no state of their own. Divergence 12 ("keySet / values / entrySet return frozen
// snapshots") is therefore gone.
//
// The remaining fidelity gap is narrow and stated in each class: Map::Entry is a
// value type, so Entry::setValue writes to the copy the iterator handed out, never
// to the map. Java's entry iterator hands out a live node. Everything else --
// size, membership, removal, iteration order, fail-fast -- reads the map itself.
//
// Iteration walks a key snapshot taken when the iterator is built. That is an
// implementation choice, not a semantic one: the snapshot can never be observed
// to be stale, because any structural change to the map in the meantime bumps
// modCount and the very next next() throws ConcurrentModificationException.

/// A port of java.util.HashMap.KeySet.
template <class K, class V>
class KeySetView : public AbstractSet<K> {
public:
    using keyType = K;
    using valueType = V;

    explicit KeySetView(Map<K, V>& owner) : map_(&owner), writable_(&owner) {}

    /// A read-only view, built by the const overload of Map::keySet().
    explicit KeySetView(const Map<K, V>& owner) : map_(&owner) {}

    [[nodiscard]] int size() const override { return map_->size(); }

    [[nodiscard]] bool isEmpty() const override { return map_->isEmpty(); }

    [[nodiscard]] bool contains(const K& key) const override { return map_->containsKey(key); }

    /// Java: HashMap.KeySet.add, which throws. A key with no value is not
    /// something a Map can represent, so there is no sensible thing to do here.
    bool add(const K&) override { throw unsupported(); }

    bool add(K&&) override { throw unsupported(); }

    /// Java: KeySet.remove(key), which removes the *mapping*, not merely the key.
    bool remove(const K& key) override { return writable().remove(key).isPresent(); }

    void clear() override { writable().clear(); }

    /// Sentinel for "no element has been returned yet".
    static constexpr std::size_t noElement = static_cast<std::size_t>(-1);

    [[nodiscard]] std::unique_ptr<Iterator<K>> iterator() override {
        static_cast<void>(writable());
        return std::make_unique<KeySetIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const K>> constIterator() const override {
        return std::make_unique<KeySetConstIterator>(*this);
    }

    class KeySetConstIterator final : public Iterator<const K> {
    public:
        explicit KeySetConstIterator(const KeySetView& owner)
            : owner_(&owner), keys_(owner.snapshotKeys()), expectedModCount_(owner.map_->modCount()) {}

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
            expectedModCount_ = owner_->map_->modCount();
            lastReturned_ = noElement;
        }

    private:
        void checkModification() const {
            if (owner_->map_->modCount() != expectedModCount_) {
                throw ConcurrentModificationException("map modified during iteration");
            }
        }

        const KeySetView* owner_;
        std::vector<K> keys_;
        std::size_t cursor_ = 0;
        std::size_t lastReturned_ = noElement;
        int expectedModCount_;
    };

    /// The mutable form. The snapshot lives in a non-const member, so handing out
    /// `K&` needs no cast -- but a key reference from the *set* is a copy, which is
    /// one more place where the map's own key stays off limits.
    class KeySetIterator final : public Iterator<K> {
    public:
        explicit KeySetIterator(KeySetView& owner)
            : owner_(&owner), keys_(owner.snapshotKeys()), expectedModCount_(owner.map_->modCount()) {}

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
            expectedModCount_ = owner_->map_->modCount();
            lastReturned_ = noElement;
        }

    private:
        void checkModification() const {
            if (owner_->map_->modCount() != expectedModCount_) {
                throw ConcurrentModificationException("map modified during iteration");
            }
        }

        KeySetView* owner_;
        std::vector<K> keys_;
        std::size_t cursor_ = 0;
        std::size_t lastReturned_ = noElement;
        int expectedModCount_;
    };

private:
    static UnsupportedOperationException unsupported() {
        return UnsupportedOperationException("keySet does not support add; put a mapping instead");
    }

    /// The one place a read-only view is rejected.
    [[nodiscard]] Map<K, V>& writable() const {
        if (writable_ == nullptr) {
            throw UnsupportedOperationException(
                "this view is read-only: it was taken from a const map");
        }
        return *writable_;
    }

    [[nodiscard]] std::vector<K> snapshotKeys() const {
        std::vector<K> keys;
        keys.reserve(static_cast<std::size_t>(map_->size()));
        map_->visitEntries([&keys](const K& key, const V& /*value*/) { keys.push_back(key); });
        return keys;
    }

    const Map<K, V>* map_;
    /// Null for a view of a const map; every mutator checks it.
    Map<K, V>* writable_ = nullptr;
};

/// A port of java.util.HashMap.Values.
template <class K, class V>
class ValuesView : public AbstractCollection<V> {
public:
    using keyType = K;
    using valueType = V;

    explicit ValuesView(Map<K, V>& owner) : map_(&owner), writable_(&owner) {}

    explicit ValuesView(const Map<K, V>& owner) : map_(&owner) {}

    [[nodiscard]] int size() const override { return map_->size(); }

    [[nodiscard]] bool isEmpty() const override { return map_->isEmpty(); }

    [[nodiscard]] bool contains(const V& value) const override { return map_->containsValue(value); }

    /// Java: HashMap.Values.add, which throws. A value with no key has nowhere to
    /// live, which is exactly why Java forbids it too.
    bool add(const V&) override { throw unsupported(); }

    bool add(V&&) override { throw unsupported(); }

    /// Java: HashMap.Values.remove(value), which removes the first mapping whose
    /// value matches. "First" is the map's own iteration order.
    bool remove(const V& value) override {
        Map<K, V>& target = writable();
        std::optional<K> matched;
        target.visitEntries([&value, &matched](const K& key, const V& candidate) {
            if (!matched.has_value() && elementEquals(candidate, value)) {
                matched = key;
            }
        });
        if (!matched.has_value()) {
            return false;
        }
        return target.remove(*matched).isPresent();
    }

    void clear() override { writable().clear(); }

    static constexpr std::size_t noElement = static_cast<std::size_t>(-1);

    [[nodiscard]] std::unique_ptr<Iterator<V>> iterator() override {
        static_cast<void>(writable());
        return std::make_unique<ValuesIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const V>> constIterator() const override {
        return std::make_unique<ValuesConstIterator>(*this);
    }

    class ValuesConstIterator final : public Iterator<const V> {
    public:
        explicit ValuesConstIterator(const ValuesView& owner)
            : owner_(&owner), keys_(owner.snapshotKeys()), expectedModCount_(owner.map_->modCount()) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < keys_.size(); }

        const V& next() override {
            checkModification();
            if (cursor_ >= keys_.size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = cursor_;
            // The map cannot have changed since checkModification(), so this
            // pointer is live and the reference stays valid until the next
            // structural change -- which is exactly Java's guarantee.
            return *owner_->map_->get(keys_[cursor_++]);
        }

        void remove() override {
            checkModification();
            if (lastReturned_ == noElement) {
                throw IllegalStateException("remove() called before next()");
            }
            static_cast<void>(owner_->writable().remove(keys_[lastReturned_]));
            expectedModCount_ = owner_->map_->modCount();
            lastReturned_ = noElement;
        }

    private:
        void checkModification() const {
            if (owner_->map_->modCount() != expectedModCount_) {
                throw ConcurrentModificationException("map modified during iteration");
            }
        }

        const ValuesView* owner_;
        std::vector<K> keys_;
        std::size_t cursor_ = 0;
        std::size_t lastReturned_ = noElement;
        int expectedModCount_;
    };

    class ValuesIterator final : public Iterator<V> {
    public:
        explicit ValuesIterator(ValuesView& owner)
            : owner_(&owner), keys_(owner.snapshotKeys()), expectedModCount_(owner.map_->modCount()) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < keys_.size(); }

        V& next() override {
            checkModification();
            if (cursor_ >= keys_.size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = cursor_;
            return *owner_->writable().get(keys_[cursor_++]);
        }

        void remove() override {
            checkModification();
            if (lastReturned_ == noElement) {
                throw IllegalStateException("remove() called before next()");
            }
            static_cast<void>(owner_->writable().remove(keys_[lastReturned_]));
            expectedModCount_ = owner_->map_->modCount();
            lastReturned_ = noElement;
        }

    private:
        void checkModification() const {
            if (owner_->map_->modCount() != expectedModCount_) {
                throw ConcurrentModificationException("map modified during iteration");
            }
        }

        ValuesView* owner_;
        std::vector<K> keys_;
        std::size_t cursor_ = 0;
        std::size_t lastReturned_ = noElement;
        int expectedModCount_;
    };

private:
    static UnsupportedOperationException unsupported() {
        return UnsupportedOperationException("values does not support add; put a mapping instead");
    }

    [[nodiscard]] Map<K, V>& writable() const {
        if (writable_ == nullptr) {
            throw UnsupportedOperationException(
                "this view is read-only: it was taken from a const map");
        }
        return *writable_;
    }

    [[nodiscard]] std::vector<K> snapshotKeys() const {
        std::vector<K> keys;
        keys.reserve(static_cast<std::size_t>(map_->size()));
        map_->visitEntries([&keys](const K& key, const V& /*value*/) { keys.push_back(key); });
        return keys;
    }

    const Map<K, V>* map_;
    Map<K, V>* writable_ = nullptr;
};

/// A port of java.util.HashMap.EntrySet.
template <class K, class V>
class EntrySetView : public AbstractSet<typename Map<K, V>::Entry> {
public:
    using Entry = Map<K, V>::Entry;
    using keyType = K;
    using valueType = V;

    explicit EntrySetView(Map<K, V>& owner) : map_(&owner), writable_(&owner) {}

    explicit EntrySetView(const Map<K, V>& owner) : map_(&owner) {}

    [[nodiscard]] int size() const override { return map_->size(); }

    [[nodiscard]] bool isEmpty() const override { return map_->isEmpty(); }

    /// Java: HashMap.EntrySet.contains(entry). Compared by key *and* value, which
    /// is Map.Entry's own definition of equality.
    [[nodiscard]] bool contains(const Entry& entry) const override {
        const V* value = map_->get(entry.getKey());
        return value != nullptr && elementEquals(*value, entry.getValue());
    }

    /// Java: HashMap.EntrySet.add, which throws. Adding a mapping is put()'s job.
    bool add(const Entry&) override { throw unsupported(); }

    bool add(Entry&&) override { throw unsupported(); }

    /// Java: HashMap.EntrySet.remove(entry), which removes the mapping only when
    /// the value matches too.
    bool remove(const Entry& entry) override {
        Map<K, V>& target = writable();
        const V* value = target.get(entry.getKey());
        if (value == nullptr || !elementEquals(*value, entry.getValue())) {
            return false;
        }
        return target.remove(entry.getKey()).isPresent();
    }

    void clear() override { writable().clear(); }

    /// Java's entry-set hash code is by definition the map's own: both are the sum
    /// of the entries' key^value hashes. Delegating is correct *and* avoids
    /// materialising copies just to hash them.
    [[nodiscard]] std::size_t hashCode() const override { return map_->hashCode(); }

    static constexpr std::size_t noElement = static_cast<std::size_t>(-1);

    [[nodiscard]] std::unique_ptr<Iterator<Entry>> iterator() override {
        static_cast<void>(writable());
        return std::make_unique<EntrySetIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const Entry>> constIterator() const override {
        return std::make_unique<EntrySetConstIterator>(*this);
    }

    class EntrySetConstIterator final : public Iterator<const Entry> {
    public:
        explicit EntrySetConstIterator(const EntrySetView& owner)
            : owner_(&owner), keys_(owner.snapshotKeys()), expectedModCount_(owner.map_->modCount()) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < keys_.size(); }

        const Entry& next() override {
            checkModification();
            if (cursor_ >= keys_.size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = cursor_;
            const K& key = keys_[cursor_++];
            current_.emplace(key, *owner_->map_->get(key));
            return *current_;
        }

        void remove() override {
            checkModification();
            if (lastReturned_ == noElement) {
                throw IllegalStateException("remove() called before next()");
            }
            static_cast<void>(owner_->writable().remove(keys_[lastReturned_]));
            expectedModCount_ = owner_->map_->modCount();
            lastReturned_ = noElement;
        }

    private:
        void checkModification() const {
            if (owner_->map_->modCount() != expectedModCount_) {
                throw ConcurrentModificationException("map modified during iteration");
            }
        }

        const EntrySetView* owner_;
        std::vector<K> keys_;
        /// Map::Entry has no default constructor, and Java's iterator hands out the
        /// same node each time; reusing one slot mirrors that.
        std::optional<Entry> current_;
        std::size_t cursor_ = 0;
        std::size_t lastReturned_ = noElement;
        int expectedModCount_;
    };

    class EntrySetIterator final : public Iterator<Entry> {
    public:
        explicit EntrySetIterator(EntrySetView& owner)
            : owner_(&owner), keys_(owner.snapshotKeys()), expectedModCount_(owner.map_->modCount()) {}

        [[nodiscard]] bool hasNext() const override { return cursor_ < keys_.size(); }

        Entry& next() override {
            checkModification();
            if (cursor_ >= keys_.size()) {
                throw NoSuchElementException("iterator exhausted");
            }
            lastReturned_ = cursor_;
            const K& key = keys_[cursor_++];
            current_.emplace(key, *owner_->writable().get(key));
            return *current_;
        }

        void remove() override {
            checkModification();
            if (lastReturned_ == noElement) {
                throw IllegalStateException("remove() called before next()");
            }
            static_cast<void>(owner_->writable().remove(keys_[lastReturned_]));
            expectedModCount_ = owner_->map_->modCount();
            lastReturned_ = noElement;
        }

    private:
        void checkModification() const {
            if (owner_->map_->modCount() != expectedModCount_) {
                throw ConcurrentModificationException("map modified during iteration");
            }
        }

        EntrySetView* owner_;
        std::vector<K> keys_;
        std::optional<Entry> current_;
        std::size_t cursor_ = 0;
        std::size_t lastReturned_ = noElement;
        int expectedModCount_;
    };

private:
    static UnsupportedOperationException unsupported() {
        return UnsupportedOperationException("entrySet does not support add; put a mapping instead");
    }

    [[nodiscard]] Map<K, V>& writable() const {
        if (writable_ == nullptr) {
            throw UnsupportedOperationException(
                "this view is read-only: it was taken from a const map");
        }
        return *writable_;
    }

    [[nodiscard]] std::vector<K> snapshotKeys() const {
        std::vector<K> keys;
        keys.reserve(static_cast<std::size_t>(map_->size()));
        map_->visitEntries([&keys](const K& key, const V& /*value*/) { keys.push_back(key); });
        return keys;
    }

    const Map<K, V>* map_;
    Map<K, V>* writable_ = nullptr;
};

// Out-of-line because the return types are only complete once the views above are.
// The const overloads are separate definitions, not a const-qualified pair, so
// that both a mutable and a const map can produce the same view type.

template <class K, class V>
[[nodiscard]] KeySetView<K, V> Map<K, V>::keySet() {
    return KeySetView<K, V>(*this);
}

template <class K, class V>
[[nodiscard]] KeySetView<K, V> Map<K, V>::keySet() const {
    return KeySetView<K, V>(*this);
}

template <class K, class V>
[[nodiscard]] ValuesView<K, V> Map<K, V>::values() {
    return ValuesView<K, V>(*this);
}

template <class K, class V>
[[nodiscard]] ValuesView<K, V> Map<K, V>::values() const {
    return ValuesView<K, V>(*this);
}

template <class K, class V>
[[nodiscard]] EntrySetView<K, V> Map<K, V>::entrySet() {
    return EntrySetView<K, V>(*this);
}

template <class K, class V>
[[nodiscard]] EntrySetView<K, V> Map<K, V>::entrySet() const {
    return EntrySetView<K, V>(*this);
}

}  // namespace cppstream
