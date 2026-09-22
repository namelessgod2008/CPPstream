#pragma once

#include <cppstream/RuntimeException.h>

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

namespace cppstream {

/// True when std::hash<T> is usable. The primary std::hash template is only
/// declared, never defined, so naming it for an unspecialised type is a
/// substitution failure rather than a hard error -- which is exactly what a
/// concept needs.
template <class T>
concept Hashable = requires(const T& value) {
    { std::hash<T>{}(value) } -> std::convertible_to<std::size_t>;
};

/// True when `left == right` yields a bool.
template <class T>
concept EqualityComparable = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

/// The C++ stand-in for java.lang.Object.equals.
///
/// Java gives *every* object an equals, defaulting to reference identity, which
/// is why java.util collections accept any element type at all. C++ has no such
/// universal fallback: operator== may simply not exist. Rather than making the
/// container uninstantiable (an eager virtual override means AbstractCollection
/// would fail to compile for such a T), this falls back to the very same
/// reference identity Java's Object.equals uses.
///
/// See DESIGN.md section 8 item 13.
template <class T>
[[nodiscard]] constexpr bool elementEquals(const T& left, const T& right) {
    if constexpr (EqualityComparable<T>) {
        return left == right;
    } else {
        return std::addressof(left) == std::addressof(right);
    }
}

/// The C++ stand-in for java.lang.Object.hashCode.
///
/// Uses std::hash<T> when the user provided one -- for the built-in types this
/// agrees with Java's Integer/String/... hashCode -- and otherwise falls back to
/// the element's address, which is what Object.hashCode does.
template <class T>
[[nodiscard]] std::size_t elementHash(const T& value) {
    if constexpr (Hashable<T>) {
        return static_cast<std::size_t>(std::hash<T>{}(value));
    } else {
        return reinterpret_cast<std::size_t>(std::addressof(value));
    }
}

/// Rejects null for pointer payloads; compiles away for everything else.
///
/// Containers in this library refuse null elements, which is a deliberate
/// divergence from java.util (DESIGN.md divergence 10). Value types have no null
/// to reject, so the check is a no-op for them.
template <class T>
void requireNonNull(const T& candidate) {
    if constexpr (std::is_pointer_v<T>) {
        if (candidate == nullptr) {
            throw NullPointerException("this collection does not permit null elements");
        }
    }
}

}  // namespace cppstream
