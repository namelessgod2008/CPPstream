#pragma once

#include <cppstream/Elements.h>
#include <cppstream/Map.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace cppstream {

/// A port of java.util.HashMap, backed by std::unordered_map.
///
/// The hash strategy is a template parameter for the same reason as HashSet: the
/// Map interface stays strategy-free, and a user who cannot specialise std::hash
/// for their key type still gets a working map.
///
/// Insertion order is not preserved, exactly as in Java. That matters for
/// Collectors::groupingBy, whose result map iterates in bucket order.
template <class K, class V, class Hash = std::hash<K>, class Equal = std::equal_to<K>>
class HashMap : public Map<K, V> {
public:
    using keyType = K;
    using valueType = V;
    using Entries = std::unordered_map<K, V, Hash, Equal>;
    using Entry = Map<K, V>::Entry;

    HashMap() = default;

    /// Java: new HashMap<>(initialCapacity).
    explicit HashMap(int initialCapacity) {
        if (initialCapacity < 0) {
            throw IllegalArgumentException("Illegal Capacity: " + std::to_string(initialCapacity));
        }
        entries_.reserve(static_cast<std::size_t>(initialCapacity));
    }

    explicit HashMap(Entries entries) : entries_(std::move(entries)) {}

    HashMap(std::initializer_list<std::pair<const K, V>> values) : entries_(values) {}

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
    Entries entries_;
};

}  // namespace cppstream
