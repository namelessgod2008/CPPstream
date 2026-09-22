// EXPECTED TO FAIL.
//
// Stream.of(values...) requires every argument to already be exactly T. Java would
// infer Stream<String> from Stream.of("a"); C++ does not promote const char* to
// std::string, and silently doing so would make Stream<T> a guessing game.
// See DESIGN.md divergence 25.
#include <cppstream/cppstream.h>

#include <string>

int main() {
    auto stream = cppstream::Stream<std::string>::of("a", "b");
    (void)stream;
}
