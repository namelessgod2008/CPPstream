// EXPECTED TO FAIL.
//
// A Stream owns a single-use cursor, so it is move-only. A copyable stream would
// silently let two pipelines share one source.
#include <cppstream/cppstream.h>

int main() {
    cppstream::Stream<int> original = cppstream::Stream<int>::of({1, 2, 3});
    cppstream::Stream<int> copy = original;
    (void)copy;
}
