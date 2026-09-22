// EXPECTED TO FAIL.
//
// Every intermediate operation is &&-qualified, so the commonest misuse -- keeping
// a named Stream and calling filter() on it -- has to be a compile error rather
// than a runtime surprise. See DESIGN.md section 5.4.
#include <cppstream/cppstream.h>

int main() {
    cppstream::Stream<int> stream = cppstream::Stream<int>::of({1, 2, 3});
    cppstream::Stream<int> filtered = stream.filter([](int value) { return value > 1; });
    (void)filtered;
}
