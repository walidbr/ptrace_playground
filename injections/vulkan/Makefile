CC ?= cc
CXX ?= g++
CFLAGS ?= -O2 -fPIC -Wall -Wextra -std=c11
CXXFLAGS ?= -O2 -fPIC -Wall -Wextra -std=c++17
LDFLAGS ?= -lvulkan

OS := $(shell uname -s)
ifeq ($(OS),Darwin)
    SHLIB_EXT = dylib
    SHLIB_LDFLAGS = -dynamiclib -install_name @rpath/libvulkan_injection.$(SHLIB_EXT)
else
    SHLIB_EXT = so
    SHLIB_LDFLAGS = -shared
endif

all: libvulkan_injection.$(SHLIB_EXT)

libvulkan_injection.$(SHLIB_EXT): vulkan_injection.cpp
	$(CXX) $(CXXFLAGS) $(SHLIB_LDFLAGS) -o $@ vulkan_injection.cpp $(LDFLAGS)

clean:
	rm -f libvulkan_injection.$(SHLIB_EXT)

.PHONY: all clean
