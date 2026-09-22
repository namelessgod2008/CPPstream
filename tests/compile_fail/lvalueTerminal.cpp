// EXPECTED TO FAIL.
//
// Same rule for terminal operations: a terminal operation consumes the pipeline,
// so it cannot be called on an lvalue that stays alive afterwards.
#include <cppstream/cppstream.h>

int main() {
    cppstream::Stream<int> stream = cppstream::Stream<int>::of({1, 2, 3});
    const std::int64_t total = stream.count();
    (void)total;
}
