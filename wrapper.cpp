#include <dlfcn.h>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <utility>
#include <mutex>

static const char* real_lib_name() {
#if defined(__APPLE__)
    return "./libfoobar.dylib";
#else
    return "./libfoobar.so";
#endif
}

struct FuncMaps {
    std::unordered_map<std::string, std::string> exact;
    std::vector<std::pair<std::string, std::string>> patterns; // preserve order
};

static bool has_wildcard(const std::string& s) {
    return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
}

static bool glob_match(const char* pat, const char* str) {
    // simple glob: '*' matches any sequence, '?' matches one char
    const char *s = str, *p = pat;
    const char *star = nullptr, *ss = nullptr;
    while (*s) {
        if (*p == '?' || *p == *s) { ++s; ++p; continue; }
        if (*p == '*') { star = p++; ss = s; continue; }
        if (star) { p = star + 1; s = ++ss; continue; }
        return false;
    }
    while (*p == '*') ++p;
    return *p == '\0';
}

static FuncMaps& func_maps() {
    static FuncMaps maps;
    static std::once_flag loaded;
    std::call_once(loaded, [](){
        const char* path = std::getenv("WRAP_MAP");
        if (!path) return; // default identity mapping
        std::ifstream in(path);
        if (!in) return;
        std::stringstream buf; buf << in.rdbuf();
        std::string s = buf.str();
        // Very tiny JSON string-to-string map parser: looks for "key":"value" pairs
        size_t i = 0;
        while (true) {
            size_t k1 = s.find('"', i); if (k1 == std::string::npos) break;
            size_t k2 = s.find('"', k1+1); if (k2 == std::string::npos) break;
            std::string key = s.substr(k1+1, k2-k1-1);
            size_t c1 = s.find(':', k2); if (c1 == std::string::npos) break;
            size_t v1 = s.find('"', c1); if (v1 == std::string::npos) break;
            size_t v2 = s.find('"', v1+1); if (v2 == std::string::npos) break;
            std::string val = s.substr(v1+1, v2-v1-1);
            if (!key.empty()) {
                if (has_wildcard(key)) maps.patterns.emplace_back(key, val);
                else maps.exact[key] = val;
            }
            i = v2 + 1;
        }
    });
    return maps;
}

static std::string mapped_target(const char* name, const char* default_target) {
    auto& maps = func_maps();
    auto it = maps.exact.find(name);
    if (it != maps.exact.end() && !it->second.empty()) return it->second;
    for (const auto& kv : maps.patterns) {
        if (glob_match(kv.first.c_str(), name)) {
            if (!kv.second.empty()) return kv.second;
            break;
        }
    }
    return std::string(default_target);
}

extern "C" int foo(int a, int b) {
    std::cout << "wrapper to foo" << std::endl;
    using foo_fn = int(*)(int,int);
    static foo_fn real_foo = nullptr;
    if (!real_foo) {
        std::string tgt = mapped_target("foo", "foo");
        real_foo = (foo_fn)dlsym(RTLD_NEXT, tgt.c_str());
        if (!real_foo) {
            void* h = dlopen(real_lib_name(), RTLD_NOW | RTLD_LOCAL);
            if (!h) {
                std::cerr << "Failed to dlopen real lib for foo: " << dlerror() << std::endl;
                std::abort();
            }
            real_foo = (foo_fn)dlsym(h, tgt.c_str());
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
        std::string tgt = mapped_target("bar", "bar");
        real_bar = (bar_fn)dlsym(RTLD_NEXT, tgt.c_str());
        if (!real_bar) {
            void* h = dlopen(real_lib_name(), RTLD_NOW | RTLD_LOCAL);
            if (!h) {
                std::cerr << "Failed to dlopen real lib for bar: " << dlerror() << std::endl;
                std::abort();
            }
            real_bar = (bar_fn)dlsym(h, tgt.c_str());
            if (!real_bar) {
                std::cerr << "Failed to resolve original bar: " << dlerror() << std::endl;
                std::abort();
            }
        }
    }
    return real_bar(s);
}
