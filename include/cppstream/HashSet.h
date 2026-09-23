#pragma once

#include <cppstream/AbstractSet.h>

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

namespace cppstream {

/// A port of java.util.HashSet, backed by std::unordered_set.
///
/// The hash strategy is a template parameter rather than a hard dependency on
/// std::hash, which is strictly more capable than Java: `HashSet<K>` works out of
/// the box when std::hash<K> is specialised, and a user who cannot specialise it
/// just passes their own Hash/Equal. The Set interface stays strategy-free.
template <class T, class Hash = std::hash<T>, class Equal = std::equal_to<T>>
class HashSet : public AbstractSet<T> {
public:
    using valueType = T;
    using Buckets = std::unordered_set<T, Hash, Equal>;

    HashSet() = default;

    /// Java: new HashSet<>(initialCapacity).
    explicit HashSet(int initialCapacity) {
        if (initialCapacity < 0) {
            throw IllegalArgumentException("Illegal Capacity: " + std::to_string(initialCapacity));
        }
        buckets_.reserve(static_cast<std::size_t>(initialCapacity));
    }

    /// Java: new HashSet<>(collection).
    explicit HashSet(const Collection<T>& source) { this->addAll(source); }

    explicit HashSet(Buckets buckets) : buckets_(std::move(buckets)) {}

    HashSet(std::initializer_list<T> values) {
        buckets_.reserve(values.size());
        for (const T& value : values) {
            this->add(value);
        }
    }

    [[nodiscard]] int size() const override { return static_cast<int>(buckets_.size()); }

    [[nodiscard]] bool contains(const T& element) const override {
        return buckets_.find(element) != buckets_.end();
    }

    /// Java: Set.add(element). False when the element was already present.
    bool add(const T& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        const bool inserted = buckets_.insert(element).second;
        if (inserted) {
            ++this->modCount_;
        }
        return inserted;
    }

    bool add(T&& element) override {
        this->checkNotFrozen();
        this->requireNonNull(element);
        const bool inserted = buckets_.insert(std::move(element)).second;
        if (inserted) {
            ++this->modCount_;
        }
        return inserted;
    }

    bool remove(const T& element) override {
        this->checkNotFrozen();
        const bool erased = buckets_.erase(element) > 0;
        if (erased) {
            ++this->modCount_;
        }
        return erased;
    }

    void clear() override {
        this->checkNotFrozen();
        if (!buckets_.empty()) {
            ++this->modCount_;
        }
        buckets_.clear();
    }

    [[nodiscard]] std::unique_ptr<Iterator<T>> iterator() override {
        return std::make_unique<HashSetIterator>(*this);
    }

    [[nodiscard]] std::unique_ptr<Iterator<const T>> constIterator() const override {
        return std::make_unique<HashSetConstIterator>(*this);
    }

    /// Read-only iterator over the buckets. remove() falls through to Iterator's
    /// throwing default, and no const_cast is involved anywhere on this path.
    class HashSetConstIterator final : public Iterator<const T> {
    public:
        explicit HashSetConstIterator(const HashSet& owner)
            : owner_(&owner), current_(owner.buckets_.begin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->buckets_.end(); }

        const T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->buckets_.end()) {
                throw NoSuchElementException("iterator exhausted");
            }
            return *current_++;
        }

    private:
        const HashSet* owner_;
        Buckets::const_iterator current_;
        int expectedModCount_;
    };

    /// The mutable iterator. std::unordered_set hands out `const T&` because a
    /// key must not change under the container's feet; Java hands out the element
    /// itself and documents the same hazard instead of preventing it. The
    /// const_cast below reproduces that contract rather than inventing a stricter
    /// one. Fail-fast is the safety net that actually matters here.
    class HashSetIterator final : public Iterator<T> {
    public:
        explicit HashSetIterator(HashSet& owner)
            : owner_(&owner), current_(owner.buckets_.begin()), expectedModCount_(owner.modCount_) {}

        [[nodiscard]] bool hasNext() const override { return current_ != owner_->buckets_.end(); }

        T& next() override {
            owner_->checkForComodification(expectedModCount_);
            if (current_ == owner_->buckets_.end()) {
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
            owner_->buckets_.erase(last_);
            ++owner_->modCount_;
            expectedModCount_ = owner_->modCount_;
            hasLast_ = false;
        }

    private:
        HashSet* owner_;
        Buckets::iterator current_;
        Buckets::iterator last_;
        bool hasLast_ = false;
        int expectedModCount_;
    };

private:
    Buckets buckets_;
};

}  // namespace cppstream
