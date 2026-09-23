#pragma once

// Umbrella header: the only include path guaranteed to be supported.
//
// The library has a genuine include cycle between Stream and the containers
// (Collection::stream() returns Stream<T>, while Stream::toList() returns
// ArrayList<T>). It is broken by forward declarations plus out-of-line member
// definitions that must appear after both types are complete. Including the
// individual headers in an arbitrary order is therefore not supported; include
// this file instead. See DESIGN.md section 7.4.

#include <cppstream/version.h>

// ---------------------------------------------------------------------------
// C++23 feature probes
// ---------------------------------------------------------------------------
//
// The library hard-depends on these. They are checked here so that a
// misconfigured toolchain produces one clear message instead of hundreds of
// template errors.

#include <functional>
#include <optional>
#include <version>

namespace cppstream {

#ifdef __cpp_lib_move_only_function
inline constexpr bool hasMoveOnlyFunction = true;
#else
inline constexpr bool hasMoveOnlyFunction = false;
#endif

#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202110L
inline constexpr bool hasOptionalMonadic = true;
#else
inline constexpr bool hasOptionalMonadic = false;
#endif

static_assert(hasMoveOnlyFunction,
    "CppStream requires std::move_only_function (C++23). Compile with C++23 enabled.");
static_assert(hasOptionalMonadic,
    "CppStream requires the C++23 monadic operations on std::optional.");

}  // namespace cppstream

// ---------------------------------------------------------------------------
// Foundation layer
// ---------------------------------------------------------------------------
//
// These three have no dependencies on each other and none on anything above
// them; every other header in the library is built on top.

#include <cppstream/Comparator.h>
#include <cppstream/Elements.h>
#include <cppstream/Gatherer.h>
#include <cppstream/Optional.h>
#include <cppstream/Parallel.h>
#include <cppstream/RuntimeException.h>

// Stream.h also provides the out-of-line definition of Optional<T>::stream(),
// so it must come after Optional.h.
#include <cppstream/Stream.h>

// ---------------------------------------------------------------------------
// Iterator and container layer
// ---------------------------------------------------------------------------
//
// Collection.h needs Stream.h for the complete return type of stream(), and
// List.h needs AbstractCollection.h, so this order is load-bearing.

#include <cppstream/Collector.h>
#include <cppstream/Iterator.h>
#include <cppstream/Iterable.h>
#include <cppstream/Collection.h>
#include <cppstream/AbstractCollection.h>
#include <cppstream/List.h>
#include <cppstream/Queue.h>
#include <cppstream/Deque.h>
#include <cppstream/Set.h>
#include <cppstream/AbstractSet.h>
#include <cppstream/HashSet.h>
#include <cppstream/TreeSet.h>
#include <cppstream/Map.h>
#include <cppstream/HashMap.h>
#include <cppstream/TreeMap.h>
#include <cppstream/ArrayList.h>
#include <cppstream/LinkedList.h>
#include <cppstream/ArrayDeque.h>
#include <cppstream/Collectors.h>
#include <cppstream/Gatherers.h>
#include <cppstream/Lists.h>
#include <cppstream/Sets.h>
#include <cppstream/Maps.h>
