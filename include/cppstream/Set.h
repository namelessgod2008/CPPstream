#pragma once

#include <cppstream/AbstractCollection.h>
#include <cppstream/Collection.h>

namespace cppstream {

/// A port of java.util.Set.
///
/// Java's Set adds no abstract methods of its own -- it is Collection plus a
/// contract: no duplicate elements, and equals/hashCode defined in terms of
/// membership rather than order. The same is true here; the order-insensitive
/// half of that contract lives in AbstractSet, so every concrete set inherits it.
///
/// Set derives from AbstractCollection rather than from Collection directly, the
/// same way List does and for the same reason: a single inheritance chain. It is
/// load-bearing, not cosmetic -- AbstractSet derives from Set, so if Set sat
/// beside AbstractCollection instead of above it, HashSet would not be a Set, and
/// AbstractSet::equals could never recognise another set (`instanceof Set` would
/// always be false). See DESIGN.md section 6.4.
///
/// Elements are unique under `elementEquals` (or, for HashSet/TreeSet, under the
/// Equal/Compare policy supplied at instantiation). Immutability of elements is a
/// precondition, not an enforced property: mutating an element in a way that
/// changes its equality corrupts the set, exactly as Java documents.
template <class T>
class Set : public AbstractCollection<T> {
public:
    using valueType = T;

    /// The marker that makes `Set` a Set. See Collection::isSet for why this is a
    /// virtual rather than a dynamic_cast.
    [[nodiscard]] bool isSet() const noexcept override { return true; }
};

}  // namespace cppstream
