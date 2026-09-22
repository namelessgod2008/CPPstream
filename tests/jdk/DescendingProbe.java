// Prints the descending-view behaviour that CppStream replicates.
//
// TreeSet.descendingSet / TreeMap.descendingMap / navigableKeySet in Java are
// *views* of the same tree, and the descending NavigableSubMap behind
// subSet().descendingSet() has its own fence rules. Both are under-specified in
// the javadoc, so they were read off a real JDK instead of guessed. The output is
// what tests/testDescendingViews.cpp asserts.
//
// Run with:  javac DescendingProbe.java && java DescendingProbe
//
// Verified against: Oracle GraalVM 25.2.4+7.1 (Java 25).

import java.util.*;

public class DescendingProbe {
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

    private static TreeSet<Integer> set() {
        return new TreeSet<>(Arrays.asList(10, 20, 30, 40, 50));
    }

    private static TreeMap<Integer, String> map() {
        TreeMap<Integer, String> m = new TreeMap<>();
        for (int key = 10; key <= 50; key += 10) m.put(key, "v" + key);
        return m;
    }

    public static void main(String[] args) {
        System.out.println("-- descendingSet() is a view of the same tree --");
        TreeSet<Integer> s = set();
        NavigableSet<Integer> ds = s.descendingSet();
        ds.add(25);
        show("set after ds.add(25)", s);
        s.add(35);
        show("ds after set.add(35)", ds);
        ds.remove(20);
        show("set after ds.remove(20)", s);
        show("ds", ds);
        show("ds.descendingSet() == set", ds.descendingSet().equals(s));
        show("ds.descendingSet().comparator()", ds.descendingSet().comparator());

        System.out.println("\n-- descendingSet() traversal and navigation --");
        show("ds.first() / ds.last()", ds.first() + " / " + ds.last());
        show("ds.floor(30) / ds.lower(30)", ds.floor(30) + " / " + ds.lower(30));
        show("ds.ceiling(30) / ds.higher(30)", ds.ceiling(30) + " / " + ds.higher(30));
        show("ds.floor(5) / ds.ceiling(100)", ds.floor(5) + " / " + ds.ceiling(100));
        show("ds.comparator()", ds.comparator());
        show("ds", ds);
        System.out.print("ds iteration:");
        for (int value : ds) System.out.print(" " + value);
        System.out.println();

        System.out.println("\n-- descendingSet() range methods flip orientation --");
        TreeSet<Integer> s2 = set();
        NavigableSet<Integer> d2 = s2.descendingSet();
        show("d2.headSet(30)", d2.headSet(30));
        show("d2.headSet(30,true)", d2.headSet(30, true));
        show("d2.tailSet(30)", d2.tailSet(30));
        show("d2.subSet(40,true,20,false)", d2.subSet(40, true, 20, false));
        attempt("d2.subSet(20,40)", () -> d2.subSet(20, 40));
        attempt("d2.headSet(100)", () -> d2.headSet(100));
        show("d2.subSet(40,false,20,false)", d2.subSet(40, false, 20, false));
        NavigableSet<Integer> dsub = d2.subSet(40, true, 20, false);
        dsub.add(30);
        show("set after dsub.add(30)", s2);
        attempt("dsub.add(45)", () -> dsub.add(45));
        attempt("dsub.add(15)", () -> dsub.add(15));
        show("dsub.first() / dsub.last()", dsub.first() + " / " + dsub.last());
        show("dsub.floor(25) / dsub.ceiling(25)", dsub.floor(25) + " / " + dsub.ceiling(25));
        show("dsub.pollFirst()", dsub.pollFirst());
        show("dsub.pollLast()", dsub.pollLast());
        show("set after the polls", s2);

        System.out.println("\n-- a range view can be seen descending too --");
        TreeSet<Integer> s3 = set();
        NavigableSet<Integer> window = s3.subSet(20, true, 40, false);
        NavigableSet<Integer> windowDesc = window.descendingSet();
        show("windowDesc", windowDesc);
        show("windowDesc.first() / last()", windowDesc.first() + " / " + windowDesc.last());
        attempt("windowDesc.add(45)", () -> windowDesc.add(45));
        windowDesc.add(35);
        show("tree after windowDesc.add(35)", s3);
        attempt("window.descendingSet().add(40)", () -> windowDesc.add(40));
        show("windowDesc.subSet(35,true,20,false)", windowDesc.subSet(35, true, 20, false));
        attempt("windowDesc.subSet(45,true,20,false)", () -> windowDesc.subSet(45, true, 20, false));
        show("windowDesc.headSet(35,true)", windowDesc.headSet(35, true));
        show("windowDesc.tailSet(35,true)", windowDesc.tailSet(35, true));
        show("windowDesc.descendingSet()", windowDesc.descendingSet());
        System.out.print("windowDesc iteration:");
        for (int value : windowDesc) System.out.print(" " + value);
        System.out.println();
        System.out.print("windowDesc.descendingIterator():");
        Iterator<Integer> dit = windowDesc.descendingIterator();
        while (dit.hasNext()) System.out.print(" " + dit.next());
        System.out.println();

        System.out.println("\n-- descendingMap() is a view of the same map --");
        TreeMap<Integer, String> m = map();
        NavigableMap<Integer, String> dm = m.descendingMap();
        dm.put(25, "v25");
        show("map after dm.put(25)", m.keySet());
        m.put(35, "v35");
        show("dm after map.put(35)", dm.keySet());
        show("dm.firstKey() / dm.lastKey()", dm.firstKey() + " / " + dm.lastKey());
        show("dm.floorKey(30) / dm.ceilingKey(30)", dm.floorKey(30) + " / " + dm.ceilingKey(30));
        show("dm.floorKey(5) / dm.ceilingKey(100)", dm.floorKey(5) + " / " + dm.ceilingKey(100));
        show("dm.comparator()", dm.comparator());
        show("dm.descendingMap() == map", dm.descendingMap().keySet().equals(m.keySet()));
        show("dm.subMap(40,true,20,false)", dm.subMap(40, true, 20, false).keySet());
        attempt("dm.subMap(20,40)", () -> dm.subMap(20, 40));
        show("dm.headMap(30,true)", dm.headMap(30, true).keySet());
        show("dm.tailMap(30,true)", dm.tailMap(30, true).keySet());
        show("dm.navigableKeySet()", dm.navigableKeySet());
        show("dm.descendingKeySet()", dm.descendingKeySet());
        attempt("dm.navigableKeySet().add(1)", () -> dm.navigableKeySet().add(1));

        System.out.println("\n-- navigableKeySet() and descendingKeySet() --");
        TreeMap<Integer, String> m2 = map();
        NavigableSet<Integer> keys = m2.navigableKeySet();
        show("keys", keys);
        keys.remove(20);
        show("map after keys.remove(20)", m2.keySet());
        show("keys.first() / keys.last()", keys.first() + " / " + keys.last());
        show("keys.floor(25) / keys.ceiling(25)", keys.floor(25) + " / " + keys.ceiling(25));
        show("keys.subSet(30,true,50,true)", keys.subSet(30, true, 50, true));
        show("keys.descendingSet()", keys.descendingSet());
        show("m2.descendingKeySet()", m2.descendingKeySet());
        show("m2.keySet() order", new ArrayList<>(m2.keySet()));

        System.out.println("\n-- a map range view seen descending --");
        TreeMap<Integer, String> m3 = map();
        NavigableMap<Integer, String> msub = m3.subMap(20, true, 40, false);
        NavigableMap<Integer, String> msubDesc = msub.descendingMap();
        show("msubDesc.keySet()", msubDesc.keySet());
        show("msubDesc.firstKey() / lastKey()", msubDesc.firstKey() + " / " + msubDesc.lastKey());
        attempt("msubDesc.put(45,x)", () -> msubDesc.put(45, "x"));
        msubDesc.put(35, "v35");
        show("map after msubDesc.put(35)", m3.keySet());
        show("msubDesc.navigableKeySet()", msubDesc.navigableKeySet());
        show("msubDesc.descendingKeySet()", msubDesc.descendingKeySet());
        show("msub.keySet() order", new ArrayList<>(msub.keySet()));
        show("msubDesc.subMap(35,true,20,false)", msubDesc.subMap(35, true, 20, false).keySet());
        attempt("msubDesc.subMap(45,true,20,false)", () -> msubDesc.subMap(45, true, 20, false));
        show("msubDesc.headMap(35,true)", msubDesc.headMap(35, true).keySet());
        show("msubDesc.tailMap(35,true)", msubDesc.tailMap(35, true).keySet());
        show("msubDesc.floorKey(25) / ceilingKey(25)", msubDesc.floorKey(25) + " / " + msubDesc.ceilingKey(25));
        show("msubDesc.floorKey(15) / ceilingKey(45)", msubDesc.floorKey(15) + " / " + msubDesc.ceilingKey(45));

        System.out.println("\n-- a descending key set on a range view --");
        show("msubDesc.navigableKeySet().first() / last()",
             msubDesc.navigableKeySet().first() + " / " + msubDesc.navigableKeySet().last());
        show("msubDesc.descendingKeySet().first() / last()",
             msubDesc.descendingKeySet().first() + " / " + msubDesc.descendingKeySet().last());
        attempt("msubDesc.descendingKeySet().add(99)", () -> msubDesc.descendingKeySet().add(99));
        show("msubDesc.descendingKeySet().comparator()", msubDesc.descendingKeySet().comparator());
        show("msubDesc.navigableKeySet().comparator()", msubDesc.navigableKeySet().comparator());
    }
}
