#include <dlfcn.h>
#include <iostream>
#include <cstdlib>

static const char* real_lib_name() {
#if defined(__APPLE__)
    return "./libfoobar.dylib";
#else
    return "./libfoobar.so";
#endif
}

extern "C" int foo(int a, int b) {
    std::cout << "wrapper to foo" << std::endl;
    using foo_fn = int(*)(int,int);
    static foo_fn real_foo = nullptr;
    if (!real_foo) {
        real_foo = (foo_fn)dlsym(RTLD_NEXT, "foo");
        if (!real_foo) {
            void* h = dlopen(real_lib_name(), RTLD_NOW | RTLD_LOCAL);
            if (!h) {
                std::cerr << "Failed to dlopen real lib for foo: " << dlerror() << std::endl;
                std::abort();
            }
            real_foo = (foo_fn)dlsym(h, "foo");
            if (!real_foo) {
                std::cerr << "Failed to resolve original foo: " << dlerror() << std::endl;
                std::abort();
            }
        }
    }
    return real_foo(a, b);
}

extern "C" int bar(const char* s) {
    std::cout << "wrapper to bar" << std::endl;
    using bar_fn = int(*)(const char*);
    static bar_fn real_bar = nullptr;
    if (!real_bar) {
        real_bar = (bar_fn)dlsym(RTLD_NEXT, "bar");
        if (!real_bar) {
            void* h = dlopen(real_lib_name(), RTLD_NOW | RTLD_LOCAL);
            if (!h) {
                std::cerr << "Failed to dlopen real lib for bar: " << dlerror() << std::endl;
                std::abort();
            }
            real_bar = (bar_fn)dlsym(h, "bar");
            if (!real_bar) {
                std::cerr << "Failed to resolve original bar: " << dlerror() << std::endl;
                std::abort();
            }
        }
    }
    return real_bar(s);
}
