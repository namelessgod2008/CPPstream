#pragma once

#include <cppstream/AbstractCollection.h>
#include <cppstream/Set.h>

#include <cstddef>
#include <memory>

namespace cppstream {

/// A port of java.util.AbstractSet: the algorithms that are order-insensitive.
///
/// Java keeps AbstractList and AbstractSet as sibling subclasses of
/// AbstractCollection. C++ needs a single chain (a diamond would give every set
/// two Collection subobjects and make every member lookup ambiguous), so Set
/// itself sits inside the chain and this simply overrides the two methods whose
/// contract differs from AbstractCollection's list-flavoured defaults.
///
/// The library compiles with -fno-rtti: the `instanceof Set` test is answered by
/// Collection::isSet rather than by dynamic_cast.
template <class T>
class AbstractSet : public Set<T> {
public:
    /// Java: AbstractSet.equals(Object).
    ///
    /// Two differences from the inherited version: the other collection must
    /// actually be a Set (Java spells this `instanceof Set`), and the comparison is
    /// by membership rather than by position.
    [[nodiscard]] bool equals(const Collection<T>& other) const override {
        if (std::addressof(other) == static_cast<const Collection<T>*>(this)) {
            return true;
        }
        if (!other.isSet()) {
            return false;
        }
        if (this->size() != other.size()) {
            return false;
        }
        return this->containsAll(other);
    }

    /// Java: AbstractSet.hashCode() -- the sum, not the ordered 31* fold that
    /// AbstractCollection (playing AbstractList's role) uses.
    [[nodiscard]] std::size_t hashCode() const override {
        std::size_t result = 0;
        for (const T& candidate : *this) {
            result += elementHash(candidate);
        }
        return result;
    }
};

}  // namespace cppstream
