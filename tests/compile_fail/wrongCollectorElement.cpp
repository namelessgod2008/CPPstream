// EXPECTED TO FAIL.
//
// Stream::collect is constrained by CollectorLike, so collecting a Stream<int>
// with a Collector<std::string, ...> is rejected at the call site instead of
// somewhere inside the type-erased pipeline.
#include <cppstream/cppstream.h>

#include <string>

int main() {
    auto result = cppstream::Stream<int>::of({1, 2, 3})
                      .collect(cppstream::Collectors::toList<std::string>());
    (void)result;
}
