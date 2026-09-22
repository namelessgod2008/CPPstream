#pragma once

#include <cppstream/HashMap.h>
#include <cppstream/Map.h>

#include <initializer_list>
#include <type_traits>
#include <utility>

namespace cppstream {

/// Java: java.util.Map's static factories, relocated for the same cycle reason as
/// Lists. Every factory returns a frozen map.
class Maps {
public:
    /// Java: Map.of().
    template <class K, class V>
    [[nodiscard]] static HashMap<K, V> empty() {
        HashMap<K, V> result;
        result.freeze();
        return result;
    }

    /// Java: Map.of(k1, v1, k2, v2, ...).
    ///
    /// Java's alternating-argument form cannot be spelled in C++ without either a
    /// variadic type switch or an unreadable interleaving, so the pairs are passed
    /// as pairs. Map.copyOf and Maps::ofEntries cover the dynamic cases.
    template <class K, class V>
    [[nodiscard]] static HashMap<K, V> of(std::initializer_list<std::pair<const K, V>> entries) {
        HashMap<K, V> result(entries);
        result.freeze();
        return result;
    }

    /// Java: Map.ofEntries(Map.entry(k, v), ...).
    ///
    /// Entries, not pairs, because that is what Java takes and what Maps::entry
    /// produces. K and V cannot be deduced through a nested type, so this one
    /// always needs its template arguments spelled out.
    template <class K, class V, class... Rest>
        requires (std::same_as<std::remove_cvref_t<Rest>, typename Map<K, V>::Entry> && ...)
    [[nodiscard]] static HashMap<K, V> ofEntries(
        typename Map<K, V>::Entry first, Rest&&... rest) {
        HashMap<K, V> result;
        result.put(first.getKey(), first.getValue());
        (result.put(rest.getKey(), rest.getValue()), ...);
        result.freeze();
        return result;
    }

    /// Java: Map.copyOf(map). An immutable copy that preserves no ordering, as in
    /// Java, where the result is an arbitrary snapshot.
    template <class K, class V>
    [[nodiscard]] static HashMap<K, V> copyOf(const Map<K, V>& source) {
        HashMap<K, V> result;
        // entrySet() rather than forEach(): forEach hands out a mutable value
        // reference and is therefore non-const, and the source is const here.
        for (const auto& entry : source.entrySet()) {
            result.put(entry.getKey(), entry.getValue());
        }
        result.freeze();
        return result;
    }

    /// Java: Collections.unmodifiableMap(map).
    template <class K, class V>
    [[nodiscard]] static HashMap<K, V> unmodifiableMap(HashMap<K, V> map) {
        map.freeze();
        return map;
    }

    /// Java 9: Map.entry(key, value), an immutable pair.
    template <class K, class V>
    [[nodiscard]] static typename Map<K, V>::Entry entry(K key, V value) {
        return typename Map<K, V>::Entry(std::move(key), std::move(value));
    }
};

}  // namespace cppstream
