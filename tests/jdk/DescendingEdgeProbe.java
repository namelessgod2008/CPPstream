// Second descending probe: the exception messages and the remaining corners.
// Companion to DescendingProbe.java; see that file's header for the rationale.

import java.util.*;

public class DescendingEdgeProbe {
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
        System.out.println("-- descending view of a *bounded* window: fence messages --");
        NavigableSet<Integer> wd = set().subSet(20, true, 40, false).descendingSet();
        attempt("wd.headSet(100)", () -> wd.headSet(100));
        attempt("wd.headSet(45)", () -> wd.headSet(45));
        attempt("wd.headSet(40)", () -> wd.headSet(40));
        attempt("wd.headSet(40,true)", () -> wd.headSet(40, true));
        attempt("wd.tailSet(5)", () -> wd.tailSet(5));
        attempt("wd.tailSet(20)", () -> wd.tailSet(20));
        attempt("wd.tailSet(20,true)", () -> wd.tailSet(20, true));
        attempt("wd.subSet(35,true,45,false)", () -> wd.subSet(35, true, 45, false));
        attempt("wd.subSet(45,true,15,false)", () -> wd.subSet(45, true, 15, false));
        attempt("wd.subSet(15,true,5,false)", () -> wd.subSet(15, true, 5, false));

        System.out.println("\n-- descendingIterator on a plain descendingSet --");
        NavigableSet<Integer> ds = set().descendingSet();
        Iterator<Integer> it = ds.descendingIterator();
        System.out.print("ds.descendingIterator():");
        while (it.hasNext()) System.out.print(" " + it.next());
        System.out.println();
        Iterator<Integer> dit = ds.iterator();
        System.out.print("ds.iterator():");
        while (dit.hasNext()) System.out.print(" " + dit.next());
        System.out.println();

        System.out.println("\n-- descendingMap on a bounded window: fence messages --");
        NavigableMap<Integer, String> md = map().subMap(20, true, 40, false).descendingMap();
        attempt("md.headMap(100)", () -> md.headMap(100));
        attempt("md.headMap(45)", () -> md.headMap(45));
        attempt("md.headMap(40)", () -> md.headMap(40));
        attempt("md.headMap(40,true)", () -> md.headMap(40, true));
        attempt("md.tailMap(5)", () -> md.tailMap(5));
        attempt("md.tailMap(20)", () -> md.tailMap(20));
        attempt("md.tailMap(20,true)", () -> md.tailMap(20, true));
        attempt("md.subMap(35,true,45,false)", () -> md.subMap(35, true, 45, false));
        attempt("md.subMap(45,true,15,false)", () -> md.subMap(45, true, 15, false));
        attempt("md.subMap(15,true,5,false)", () -> md.subMap(15, true, 5, false));
        show("md.headMap(35,false)", md.headMap(35, false).keySet());
        show("md.tailMap(25,false)", md.tailMap(25, false).keySet());

        System.out.println("\n-- descending entry navigation --");
        TreeMap<Integer, String> m3 = map();
        NavigableMap<Integer, String> dm = m3.descendingMap();
        show("dm.firstEntry() / lastEntry()", dm.firstEntry() + " / " + dm.lastEntry());
        show("dm.pollFirstEntry()", dm.pollFirstEntry());
        show("dm.pollLastEntry()", dm.pollLastEntry());
        show("map after the polls", m3.keySet());
        show("dm.entrySet() order", new ArrayList<>(dm.entrySet()));
        show("dm.values() order", new ArrayList<>(dm.values()));

        System.out.println("\n-- descendingKeySet is a view --");
        TreeMap<Integer, String> m4 = map();
        NavigableSet<Integer> dks = m4.descendingKeySet();
        dks.remove(30);
        show("map after dks.remove(30)", m4.keySet());
        show("dks.first() / last()", dks.first() + " / " + dks.last());
        show("dks.floor(25) / ceiling(25)", dks.floor(25) + " / " + dks.ceiling(25));
        show("dks.subSet(50,true,20,true)", dks.subSet(50, true, 20, true));
        show("dks.headSet(30,true)", dks.headSet(30, true));
        show("dks.tailSet(30,true)", dks.tailSet(30, true));
        show("dks.pollFirst() / pollLast()", dks.pollFirst() + " / " + dks.pollLast());
        show("map after the polls", m4.keySet());
        show("dks.descendingSet()", dks.descendingSet());

        System.out.println("\n-- navigableKeySet navigation and views --");
        TreeMap<Integer, String> m5 = map();
        NavigableSet<Integer> nks = m5.navigableKeySet();
        show("nks.comparator()", nks.comparator());
        show("nks.descendingSet().comparator()", nks.descendingSet().comparator());
        show("nks.headSet(30,true)", nks.headSet(30, true));
        show("nks.tailSet(30,false)", nks.tailSet(30, false));
        show("nks.pollFirst()", nks.pollFirst());
        show("nks.pollLast()", nks.pollLast());
        show("map after the polls", m5.keySet());
        attempt("nks.add(1)", () -> nks.add(1));
        show("nks.subSet(30,true,50,true)", nks.subSet(30, true, 50, true));
        attempt("nks.subSet(50,true,30,true)", () -> nks.subSet(50, true, 30, true));

        System.out.println("\n-- a genuinely bounded descending key set --");
        TreeMap<Integer, String> m6 = map();
        NavigableSet<Integer> bks = m6.subMap(20, false, 50, true).descendingKeySet();
        show("bks", bks);
        show("bks.first() / last()", bks.first() + " / " + bks.last());
        attempt("bks.headSet(100)", () -> bks.headSet(100));
        attempt("bks.tailSet(10)", () -> bks.tailSet(10));
        show("bks.floor(15) / ceiling(15)", bks.floor(15) + " / " + bks.ceiling(15));
        show("bks.floor(100) / ceiling(100)", bks.floor(100) + " / " + bks.ceiling(100));
        show("bks.subSet(40,true,30,false)", bks.subSet(40, true, 30, false));

        System.out.println("\n-- accepted-but-empty windows: the [k, k) spelling --");
        NavigableSet<Integer> ew = set().subSet(20, true, 40, false).descendingSet();
        // An exclusive bound only has to lie in the closed range, so both of these
        // are accepted and yield an empty window: [40, 40) and [20, 20).
        show("ew.headSet(40,false)", ew.headSet(40, false));
        show("ew.tailSet(20,false)", ew.tailSet(20, false));
        show("ew.headSet(40,false).size()", ew.headSet(40, false).size());
        show("ew.headSet(40,false).floor(100)", ew.headSet(40, false).floor(100));
        show("ew.headSet(40,false).ceiling(5)", ew.headSet(40, false).ceiling(5));
        attempt("ew.headSet(40,false).first()", () -> ew.headSet(40, false).first());
        attempt("ew.headSet(40,false).last()", () -> ew.headSet(40, false).last());
        show("ew.headSet(40,false).descendingSet()", ew.headSet(40, false).descendingSet());
        show("set().headSet(5,false).descendingSet()", set().headSet(5, false).descendingSet());
        show("set().tailSet(100,true).descendingSet()", set().tailSet(100, true).descendingSet());

        TreeMap<Integer, String> em = map();
        NavigableMap<Integer, String> emv =
            em.subMap(20, true, 40, false).descendingMap().headMap(40, false);
        show("emv keySet", emv.keySet());
        show("emv.size()", emv.size());
        show("emv.firstEntry() / lastEntry()", emv.firstEntry() + " / " + emv.lastEntry());
        show("emv.floorKey(100) / ceilingKey(5)", emv.floorKey(100) + " / " + emv.ceilingKey(5));

        System.out.println("\n-- lookups on a bounded descending key set clamp --");
        TreeMap<Integer, String> m7 = map();
        NavigableSet<Integer> cks = m7.subMap(20, true, 40, false).navigableKeySet();
        show("cks", cks);
        show("cks.floor(100) / ceiling(5)", cks.floor(100) + " / " + cks.ceiling(5));
        show("cks.floor(10) / ceiling(100)", cks.floor(10) + " / " + cks.ceiling(100));
    }
}
