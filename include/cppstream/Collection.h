#pragma once

#include <cppstream/Iterable.h>
#include <cppstream/Stream.h>

#include <cstddef>
#include <vector>

namespace cppstream {

/// A port of java.util.Collection.
///
/// The interface is pure: every shared algorithm lives in AbstractCollection, so
/// that the containers below only have to supply `size`, `add`, `iterator` and
/// `constIterator`. That mirrors Java, where AbstractCollection does the same job.
///
/// Note the two deliberate C++-isms inherited from Iterable: `stream()` is const
/// because reading a container to build a stream must not require mutation, and
/// the mutable/read-only iterator split (see Iterable.h and DESIGN.md section 6.6).
template <class T>
class Collection : public Iterable<T> {
public:
    using valueType = T;

    /// Java: Collection.size(). Java returns int and so does this: the 2^31
    /// element ceiling is part of the ported semantics.
    [[nodiscard]] virtual int size() const = 0;

    /// Java: Collection.isEmpty().
    [[nodiscard]] virtual bool isEmpty() const = 0;

    /// Java: Collection.contains(element).
    [[nodiscard]] virtual bool contains(const T& element) const = 0;

    /// Java: Collection.add(element).
    virtual bool add(const T& element) = 0;
    virtual bool add(T&& element) = 0;

    /// Java: Collection.remove(element), removing the first matching element.
    virtual bool remove(const T& element) = 0;

    [[nodiscard]] virtual bool containsAll(const Collection<T>& other) const = 0;
    virtual bool addAll(const Collection<T>& other) = 0;
    virtual bool removeAll(const Collection<T>& other) = 0;
    virtual bool retainAll(const Collection<T>& other) = 0;
    virtual void clear() = 0;

    /// Java: Collection.toArray(). std::vector is the C++ spelling.
    [[nodiscard]] virtual std::vector<T> toArray() const = 0;

    /// Java: Collection.stream().
    ///
    /// Element values are copied into the stream: the pipeline yields `T` by
    /// value, so a stream never aliases the container. See DESIGN.md section 6.6.
    [[nodiscard]] virtual Stream<T> stream() const = 0;

    /// Java: Collection.parallelStream().
    ///
    /// The container knows its size, so the stream starts with a pull budget and
    /// every element-wise stage built from it can batch. See DESIGN.md divergence 8
    /// for why the parallel flag has to be set *before* the stages are built, which
    /// makes this -- like Java's -- the idiomatic entry point for a parallel
    /// pipeline.
    [[nodiscard]] virtual Stream<T> parallelStream() const = 0;

    /// Java: Collection.equals(other). Order-sensitive, as in Java's
    /// AbstractCollection; Set overrides it to be order-insensitive.
    [[nodiscard]] virtual bool equals(const Collection<T>& other) const = 0;

    /// Java: Collection.hashCode(), the sum of the element hash codes.
    [[nodiscard]] virtual std::size_t hashCode() const = 0;

    /// C++-only stand-in for Java's `instanceof Set`.
    ///
    /// AbstractSet's equals has to reject a List holding the same elements, which
    /// Java spells `o instanceof Set`. The direct C++ translation is dynamic_cast,
    /// but that would make the whole library require RTTI for one line of one
    /// method. A virtual discriminator answers the same question, compiles under
    /// -fno-rtti, and costs one vtable slot. Set overrides it; nothing else does.
    [[nodiscard]] virtual bool isSet() const noexcept { return false; }
};

}  // namespace cppstream
