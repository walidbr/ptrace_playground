#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

typedef int (*puts_func_t)(const char*);

int puts(const char* s) {
    puts_func_t original_puts = (puts_func_t)dlsym(RTLD_NEXT, "puts");
    printf("Wrapped puts: About to print: %s\n", s);
    return original_puts(s);
}