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

build: oxygen libwrap.$(SHLIB_EXT) foobar_test_program
	$(MAKE) -C injections/foobar
	$(MAKE) -C injections/vulkan

foobar_test_program: foobar_test_program.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

ifeq ($(OS),Darwin)
    RPATH_FLAG = -Wl,-rpath,@loader_path
else
    RPATH_FLAG = -Wl,-rpath,\$$ORIGIN
endif

oxygen: oxygen.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

libwrap.$(SHLIB_EXT): wrapper.cpp
	$(CXX) $(CXXFLAGS) $(SHLIB_LDFLAGS) -o $@ $< $(DL_LIB) $(LDFLAGS)

check: build
	./oxygen foobar ./foobar_test_program

clean:
	rm -f oxygen libwrap.so libwrap.dylib
	$(MAKE) -C injections/foobar clean
	$(MAKE) -C injections/vulkan clean

.PHONY: all build run clean
