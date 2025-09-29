#include <stdio.h>
#include <string.h>

int foo(int a, int b) {
    printf("foo(%d, %d) called\n", a, b);
    return a + b;
}

int bar(const char* s) {
    printf("bar(\"%s\") called\n", s ? s : "");
    return s ? (int)strlen(s) : 0;
}

