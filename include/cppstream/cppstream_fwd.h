#pragma once

#include <cstdint>

// Forward declarations for every public name in the library, so that a
// translation unit can refer to the types without pulling in the headers.
//
// Default template arguments are deliberately omitted: a default argument may be
// specified only once per parameter, and that place is the defining header.

namespace cppstream {

class RuntimeException;
class IllegalStateException;
class NoSuchElementException;
class UnsupportedOperationException;
class NullPointerException;
class IllegalArgumentException;
class IndexOutOfBoundsException;
class ConcurrentModificationException;
class ArithmeticException;
class IllegalCollectorStateException;

template <class T>
class Iterator;

template <class T>
class ReadOnlyIterator;

struct ReadOnlySentinel;

template <class T>
class Iterable;

template <class T>
class Collection;

template <class T>
class AbstractCollection;

template <class T>
class List;

template <class T>
class ArrayList;

template <class T>
class LinkedList;

template <class T>
class ListIterator;

template <class T>
class ListView;

template <class T>
class Queue;

template <class T>
class Deque;

template <class T>
class ArrayDeque;

template <class T>
class Set;

template <class T>
class AbstractSet;

template <class T, class Hash, class Equal>
class HashSet;

template <class T, class Compare>
class TreeSet;

template <class T, class Compare>
class TreeSetRangeView;

template <class K, class V>
class Map;

template <class K, class V, class Hash, class Equal>
class HashMap;

template <class K, class V, class Compare>
class TreeMap;

template <class K, class V, class Compare>
class TreeMapRangeView;

template <class K, class V, class Compare>
class TreeMapKeySetView;

template <class K, class V>
class KeySetView;

template <class K, class V>
class ValuesView;

template <class K, class V>
class EntrySetView;

template <class T>
class Stream;

template <class T>
class Optional;

template <class T>
class Comparator;

// comparatorFrom() is deliberately not declared here. It is a function template,
// so a caller needs its definition, never merely a declaration -- and the
// declaration would be a second one in every TU that also includes Comparator.h.

template <class T, class A, class R>
class Collector;

template <class R>
class Downstream;

template <class T, class A, class R>
class Gatherer;

enum class Characteristics : std::uint8_t;

class Collectors;
class Gatherers;
class Lists;
class Sets;
class Maps;

class IntSummaryStatistics;
class LongSummaryStatistics;
class KahanSummation;
class DoubleSummaryStatistics;

template <class V>
struct IsOptional;

template <class V>
struct IsStream;

}  // namespace cppstream
