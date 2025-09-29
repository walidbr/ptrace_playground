CC ?= cc
CXX ?= g++
CFLAGS ?= -O2 -fPIC -Wall -Wextra -std=c11
CXXFLAGS ?= -O2 -fPIC -Wall -Wextra -std=c++17
LDFLAGS ?=

OS := $(shell uname -s)
ifeq ($(OS),Darwin)
    SHLIB_EXT = dylib
    SHLIB_LDFLAGS = -dynamiclib -install_name @rpath/libfoobar.$(SHLIB_EXT)
else
    SHLIB_EXT = so
    SHLIB_LDFLAGS = -shared
endif

all: libfoobar.$(SHLIB_EXT)

libfoobar.$(SHLIB_EXT): foobar_injection.cpp foobar_lib.c
	$(CXX) $(CXXFLAGS) $(SHLIB_LDFLAGS) -o $@ foobar_injection.cpp foobar_lib.c $(LDFLAGS)

clean:
	rm -f libfoobar.$(SHLIB_EXT)

.PHONY: all clean
