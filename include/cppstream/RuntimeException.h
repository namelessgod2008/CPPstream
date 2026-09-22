#pragma once

#include <stdexcept>
#include <string>

namespace cppstream {

/// Root of every exception this library throws.
///
/// Java splits throwables into checked and unchecked. C++ has no such
/// distinction, and every exception raised by the Java Stream / Collection API
/// is unchecked, so a single root derived from std::runtime_error is the
/// faithful translation. Deriving from std::runtime_error also keeps the
/// standard `what()` contract.
class RuntimeException : public std::runtime_error {
public:
    /// Java's no-argument exceptions carry a null message; std::runtime_error has
    /// no null state, so "" is the closest equivalent.
    RuntimeException() : std::runtime_error("") {}
    explicit RuntimeException(const std::string& message) : std::runtime_error(message) {}
    explicit RuntimeException(const char* message) : std::runtime_error(message) {}
};

/// Raised when an operation is attempted at the wrong time or in the wrong
/// state, e.g. consuming a Stream twice.
class IllegalStateException : public RuntimeException {
public:
    using RuntimeException::RuntimeException;
};

/// Raised when an iterator or Optional is read past its end.
class NoSuchElementException : public RuntimeException {
public:
    using RuntimeException::RuntimeException;
};

/// Raised by optional operations on collections that do not support them, such
/// as mutating an unmodifiable list.
class UnsupportedOperationException : public RuntimeException {
public:
    using RuntimeException::RuntimeException;
};

/// Raised when a collection is handed a null element. CppStream containers
/// reject null outright rather than tolerating it the way Java does.
class NullPointerException : public RuntimeException {
public:
    using RuntimeException::RuntimeException;
};

/// Raised for illegal arguments that are not index-related.
class IllegalArgumentException : public RuntimeException {
public:
    using RuntimeException::RuntimeException;
};

/// Derived from IllegalArgumentException so that a single catch of the C++-ish
/// root also covers the Java-specific IndexOutOfBoundsException.
class IndexOutOfBoundsException : public IllegalArgumentException {
public:
    using IllegalArgumentException::IllegalArgumentException;
};

/// Raised by fail-fast iterators when the backing collection is structurally
/// modified during iteration.
class ConcurrentModificationException : public RuntimeException {
public:
    using RuntimeException::RuntimeException;
};

/// Raised on overflow-detected numeric reduction.
class ArithmeticException : public RuntimeException {
public:
    using RuntimeException::RuntimeException;
};

/// Java signals a duplicate key from Collectors::toMap by throwing a bare
/// IllegalStateException; this subtype keeps that catch-compatibility while
/// making the cause identifiable.
class IllegalCollectorStateException : public IllegalStateException {
public:
    using IllegalStateException::IllegalStateException;
};

}  // namespace cppstream
