#pragma once

#include <cppstream/RuntimeException.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace cppstream {

template <class T>
class Optional;

template <class T>
class Stream;

/// Detects Optional<U> for any U. Needed to constrain flatMap, whose mapper must
/// return an Optional rather than a bare value.
template <class V>
struct IsOptional : std::false_type {};

template <class V>
struct IsOptional<Optional<V>> : std::true_type {};

template <class V>
inline constexpr bool isOptional = IsOptional<std::remove_cvref_t<V>>::value;

/// A port of java.util.Optional.
///
/// std::optional differs from java.util.Optional in almost every method name
/// (isPresent vs has_value, get vs value, orElseGet vs or_else), so an alias
/// would not be faithful. This wraps std::optional<T> and exposes the Java
/// surface on top. Conversions to and from std::optional are implicit in both
/// directions so the wrapper does not fragment the standard-library ecosystem.
///
/// Deliberate divergence from Java: get() returns a reference rather than a
/// value. Java's get() likewise yields the referenced object without copying,
/// so returning `const T&` is arguably the closer translation.
template <class T>
class Optional {
public:
    static_assert(!std::is_reference_v<T>, "Optional<T> cannot hold a reference type");
    static_assert(!std::is_void_v<T>, "Optional<T> cannot hold void");
    static_assert(!std::is_const_v<T>, "Optional<T> cannot hold a const-qualified type");

    using valueType = T;

    // --- Construction ------------------------------------------------------

    constexpr Optional() noexcept = default;
    constexpr Optional(std::nullopt_t /*unused*/) noexcept {}

    Optional(const T& value) : value_(value) {}
    Optional(T&& value) : value_(std::move(value)) {}

    Optional(const std::optional<T>& other) : value_(other) {}
    Optional(std::optional<T>&& other) : value_(std::move(other)) {}

    /// Java: Optional.empty().
    static constexpr Optional empty() noexcept { return Optional(); }

    /// Java: Optional.of(value).
    static Optional of(const T& value) { return Optional(value); }
    static Optional of(T&& value) { return Optional(std::move(value)); }

    /// Java: Optional.ofNullable(value). Only pointer payloads can express
    /// "null"; for every other type this is equivalent to of().
    static Optional ofNullable(T value) {
        if constexpr (std::is_pointer_v<T>) {
            if (value == nullptr) {
                return Optional();
            }
        }
        return Optional(std::move(value));
    }

    /// Bridge from std::optional. The converting constructor already covers
    /// this; fromStd exists so the intent reads explicitly at call sites.
    static Optional fromStd(std::optional<T> value) { return Optional(std::move(value)); }

    // --- Inspection --------------------------------------------------------

    /// Java: Optional.isPresent().
    [[nodiscard]] constexpr bool isPresent() const noexcept { return value_.has_value(); }

    /// Java 11: Optional.isEmpty().
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return !value_.has_value(); }

    // --- Extraction --------------------------------------------------------

    /// Java: Optional.get(). Throws NoSuchElementException, with Java's exact
    /// message, when the Optional is empty.
    [[nodiscard]] const T& get() const& {
        if (!value_) {
            throw NoSuchElementException("No value present");
        }
        return *value_;
    }

    T& get() & {
        if (!value_) {
            throw NoSuchElementException("No value present");
        }
        return *value_;
    }

    T&& get() && {
        if (!value_) {
            throw NoSuchElementException("No value present");
        }
        return std::move(*value_);
    }

    /// Java: Optional.orElse(other).
    [[nodiscard]] T orElse(const T& defaultValue) const { return value_ ? *value_ : defaultValue; }

    /// Java: Optional.orElseGet(supplier).
    template <class F>
    [[nodiscard]] T orElseGet(F supplier) const {
        if (value_) {
            return *value_;
        }
        return std::invoke(supplier);
    }

    /// Java 10: Optional.orElseThrow() with no argument.
    [[nodiscard]] const T& orElseThrow() const& { return get(); }

    /// Java: Optional.orElseThrow(exceptionSupplier). The supplier returns the
    /// exception to throw, so the throw site stays at the call site.
    template <class F>
    [[nodiscard]] [[nodiscard]] const T& orElseThrow(F exceptionSupplier) const& {
        if (!value_) {
            throw std::invoke(exceptionSupplier);
        }
        return *value_;
    }

    // --- Conditional execution ---------------------------------------------

    /// Java: Optional.ifPresent(action).
    template <class F>
    void ifPresent(F action) const {
        if (value_) {
            std::invoke(action, *value_);
        }
    }

    /// Java 9: Optional.ifPresentOrElse(action, emptyAction).
    template <class F, class G>
    void ifPresentOrElse(F action, G emptyAction) const {
        if (value_) {
            std::invoke(action, *value_);
        } else {
            std::invoke(emptyAction);
        }
    }

    // --- Transformation ----------------------------------------------------

    /// Java: Optional.map(mapper). A mapper that yields a null pointer produces
    /// an empty Optional, matching Java's Optional.ofNullable wrapping.
    template <class F>
        requires(!std::is_void_v<std::invoke_result_t<F, const T&>>)
    [[nodiscard]] [[nodiscard]] [[nodiscard]] auto map(F mapper) const
        -> Optional<std::remove_cvref_t<std::invoke_result_t<F, const T&>>> {
        using U = std::remove_cvref_t<std::invoke_result_t<F, const T&>>;
        if (!value_) {
            return Optional<U>::empty();
        }
        return Optional<U>::ofNullable(std::invoke(mapper, *value_));
    }

    /// Java: Optional.flatMap(mapper). The mapper must itself return an
    /// Optional; Java would throw a NullPointerException if it returned null,
    /// which C++ cannot detect, so an empty Optional is passed through as-is.
    template <class F>
        requires isOptional<std::invoke_result_t<F, const T&>>
    [[nodiscard]] auto flatMap(F mapper) const -> std::invoke_result_t<F, const T&> {
        using R = std::invoke_result_t<F, const T&>;
        if (!value_) {
            return R();
        }
        return std::invoke(mapper, *value_);
    }

    /// Java: Optional.filter(predicate).
    template <class P>
    [[nodiscard]] [[nodiscard]] [[nodiscard]] Optional filter(P predicate) const {
        if (!value_ || !std::invoke(predicate, *value_)) {
            return Optional();
        }
        return *this;
    }

    /// Java 9: Optional.stream(), a zero- or one-element stream.
    ///
    /// Declared here but defined in Stream.h, because the return type must be a
    /// complete Stream<T>. This is the include-cycle break described in
    /// DESIGN.md section 7.4: include <cppstream/cppstream.h> to use it.
    [[nodiscard]] Stream<T> stream() const;

    // --- Interop and comparison --------------------------------------------

    /// Explicit escape hatch to std::optional; the implicit conversion operator
    /// below makes this optional, but reads better when the target type is not
    /// obvious from context.
    [[nodiscard]] std::optional<T> unwrap() const& { return value_; }
    std::optional<T> unwrap() && { return std::move(value_); }

    operator std::optional<T>() const& { return value_; }              // NOLINT(google-explicit-constructor)
    operator std::optional<T>() && { return std::move(value_); }       // NOLINT(google-explicit-constructor)

    /// Java: Optional.equals(other). CppStream exposes Java's method names
    /// rather than overloading operator==, so that a type never has two
    /// competing notions of equality.
    [[nodiscard]] bool equals(const Optional& other) const {
        if (value_.has_value() != other.value_.has_value()) {
            return false;
        }
        if (!value_) {
            return true;
        }
        return *value_ == *other.value_;
    }

    /// Java: Optional.hashCode(); zero when empty. Requires std::hash<T>.
    [[nodiscard]] std::size_t hashCode() const {
        return value_ ? std::hash<T>{}(*value_) : std::size_t{0};
    }

private:
    std::optional<T> value_;
};

}  // namespace cppstream
