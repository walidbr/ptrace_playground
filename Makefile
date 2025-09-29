CC ?= cc
CXX ?= g++
CFLAGS ?= -O2 -fPIC -Wall -Wextra -std=c11
CXXFLAGS ?= -O2 -fPIC -Wall -Wextra -std=c++17
LDFLAGS ?=

OS := $(shell uname -s)
ifeq ($(OS),Darwin)
    SHLIB_EXT = dylib
    SHLIB_LDFLAGS = -dynamiclib -install_name @rpath/libwrap.$(SHLIB_EXT)
    DL_LIB =
else
    SHLIB_EXT = so
    SHLIB_LDFLAGS = -shared
    DL_LIB = -ldl
endif

all: build

build: foobar ptrace libwrap.$(SHLIB_EXT)

ifeq ($(OS),Darwin)
    RPATH_FLAG = -Wl,-rpath,@loader_path
else
    RPATH_FLAG = -Wl,-rpath,\$$ORIGIN
endif

foobar: foobar.cpp libfoobar.$(SHLIB_EXT)
	$(CXX) $(CXXFLAGS) -o $@ foobar.cpp $(RPATH_FLAG) -L. -lfoobar $(LDFLAGS)

# Static variant links implementations directly into the binary
foobar_lib.o: foobar_lib.c
	$(CC) $(CFLAGS) -c -o $@ $<

foobar_static: foobar.cpp foobar_lib.o
	$(CXX) $(CXXFLAGS) -o $@ foobar.cpp foobar_lib.o $(LDFLAGS)

ifeq ($(OS),Linux)
    PTRACE_SRCS = ptrace.cpp tracer_linux.cpp
else
    PTRACE_SRCS = ptrace.cpp
endif

ptrace: $(PTRACE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(PTRACE_SRCS) $(LDFLAGS)

libwrap.$(SHLIB_EXT): wrapper.cpp
	$(CXX) $(CXXFLAGS) $(SHLIB_LDFLAGS) -o $@ $< $(DL_LIB) $(LDFLAGS)

libfoobar.$(SHLIB_EXT): foobar_lib.c
	$(CC) $(CFLAGS) $(SHLIB_LDFLAGS) -o $@ $< $(LDFLAGS)

run: build
	./ptrace ./foobar

clean:
	rm -f foobar foobar_static foobar_lib.o ptrace libwrap.so libwrap.dylib libfoobar.so libfoobar.dylib

.PHONY: all build run clean
