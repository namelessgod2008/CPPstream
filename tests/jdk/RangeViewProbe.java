// Prints the behaviour that CppStream's range views replicate.
//
// CppStream's TreeSetRangeView / TreeMapRangeView follow java.util.TreeMap's
// NavigableSubMap (the class behind TreeSet.subSet and friends). Its fence rules
// are under-specified in the javadoc -- whether a lookup clamps to the window,
// whether a new fence may widen it, whether computeIfPresent sees keys outside
// it -- so they were read off a real JDK instead of guessed, and the output below
// is what tests/testRangeViews.cpp asserts.
//
// Run with:  javac RangeViewProbe.java && java RangeViewProbe
//
// Verified against: Oracle GraalVM 25.2.4+7.1 (Java 25).

import java.util.*;

public class RangeViewProbe {
    private static final TreeMap<Integer, String> BASE = base();

    private static TreeMap<Integer, String> base() {
        TreeMap<Integer, String> map = new TreeMap<>();
        for (int key = 10; key <= 50; key += 10) map.put(key, "v" + key);
        return map;
    }

    private static void show(String label, Object value) {
        System.out.println(label + " = " + value);
    }

    private static void attempt(String label, Runnable body) {
        try {
            body.run();
        } catch (RuntimeException error) {
            show(label + " THROWS", error.getClass().getSimpleName() + ": " + error.getMessage());
        }
    }

    public static void main(String[] args) {
        NavigableMap<Integer, String> sub = BASE.subMap(20, true, 40, false);
        show("sub", sub.keySet() + " size=" + sub.size());

        System.out.println("\n-- membership honours both fences --");
        show("containsKey(20) / containsKey(40)", sub.containsKey(20) + " / " + sub.containsKey(40));
        show("get(10) / get(30)", sub.get(10) + " / " + sub.get(30));
        show("remove(10) /*outside*/", sub.remove(10));

        System.out.println("\n-- writes outside the window are rejected --");
        attempt("sub.put(50, x)", () -> sub.put(50, "x"));
        attempt("sub.putIfAbsent(50, x)", () -> sub.putIfAbsent(50, "x"));
        attempt("sub.computeIfAbsent(50, -> x)", () -> sub.computeIfAbsent(50, key -> "x"));
        attempt("sub.merge(50, x)", () -> sub.merge(50, "x", (left, right) -> left + right));

        System.out.println("\n-- ...except computeIfPresent, which is a no-op --");
        show("computeIfPresent(50, v + ?)", sub.computeIfPresent(50, (key, value) -> value + "?"));
        show("map.get(50) afterwards", BASE.get(50));
        show("computeIfPresent(30, v + ?)", sub.computeIfPresent(30, (key, value) -> value + "?"));

        System.out.println("\n-- lookups clamp to the window --");
        show("floorKey(10) /*below*/", sub.floorKey(10));
        show("floorKey(40) /*the exclusive fence*/", sub.floorKey(40));
        show("floorKey(100) /*above*/", sub.floorKey(100));
        show("lowerKey(20) / lowerKey(100)", sub.lowerKey(20) + " / " + sub.lowerKey(100));
        show("ceilingKey(10) /*below*/", sub.ceilingKey(10));
        show("ceilingKey(100) /*above*/", sub.ceilingKey(100));
        show("higherKey(10) / higherKey(35)", sub.higherKey(10) + " / " + sub.higherKey(35));

        System.out.println("\n-- a new fence may not widen the window --");
        show("sub.subMap(20,true,30,false)", sub.subMap(20, true, 30, false).keySet());
        show("sub.subMap(20,false,30,false)", sub.subMap(20, false, 30, false).keySet());
        show("sub.headMap(40,false)", sub.headMap(40, false).keySet());
        attempt("sub.headMap(40,true)", () -> sub.headMap(40, true));
        attempt("sub.headMap(10,false)", () -> sub.headMap(10, false));
        attempt("sub.tailMap(45,true)", () -> sub.tailMap(45, true));
        show("sub.tailMap(20,false)", sub.tailMap(20, false).keySet());
        attempt("sub.subMap(35,true,25,true)", () -> sub.subMap(35, true, 25, true));
        show("sub.subMap(25,true,35,true)", sub.subMap(25, true, 35, true).keySet());

        System.out.println("\n-- an exclusive lower fence rejects an inclusive equal bound --");
        NavigableMap<Integer, String> exclusive = BASE.subMap(20, false, 40, false);
        attempt("exclusive.tailMap(20,true)", () -> exclusive.tailMap(20, true));
        show("exclusive.tailMap(20,false)", exclusive.tailMap(20, false).keySet());

        System.out.println("\n-- the container rejects a backwards window --");
        attempt("map.subMap(40,20)", () -> BASE.subMap(40, 20));

        System.out.println("\n-- the views on a window are filtered too --");
        show("sub.keySet() / values() / entrySet()", sub.keySet() + " / " + sub.values() + " / " + sub.entrySet());
        show("sub.firstKey() / lastKey()", sub.firstKey() + " / " + sub.lastKey());
        show("sub.pollFirstEntry()", sub.pollFirstEntry());
        show("map afterwards", BASE.keySet());

        System.out.println("\n-- a set window behaves the same way --");
        NavigableSet<Integer> set = new TreeSet<>(Arrays.asList(10, 20, 30, 40, 50));
        NavigableSet<Integer> window = set.subSet(20, true, 40, false);
        set.add(25);
        show("the window sees a later insert", window);
        window.add(35);
        show("the tree sees a write through the window", set);
        attempt("window.add(45)", () -> window.add(45));
        attempt("window.addAll([21,45])", () -> window.addAll(Arrays.asList(21, 45)));
        show("the tree after the partial addAll", set);
        show("window.floor(10) / floor(100)", window.floor(10) + " / " + window.floor(100));
        show("window.ceiling(10) / ceiling(100)", window.ceiling(10) + " / " + window.ceiling(100));
        show("window.first() / last()", window.first() + " / " + window.last());
        Iterator<Integer> iterator = window.iterator();
        show("iterator.next()", iterator.next());
        set.add(45);
        attempt("iterator.next() after the tree changed", iterator::next);
    }
}
