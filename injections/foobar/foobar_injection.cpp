#include <iostream>
#include <cstring>

extern "C" {
int foo(int a, int b);
int bar(const char* s);
}

// Implementations are provided by the shared library libfoobar

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::cout << "Hello, world!" << std::endl;
    int fr = foo(2, 3);
    int br = bar("example");
    std::cout << "foo returned: " << fr << std::endl;
    std::cout << "bar returned: " << br << std::endl;
    return 0;
}
