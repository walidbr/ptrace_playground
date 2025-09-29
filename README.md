ptrace Function Wrapper Demo
=================================

This small C/C++ project demonstrates launching a program with a preloadable wrapper library that intercepts functions, prints "wrapper" messages, and forwards calls to the original implementations. It also supports remapping functions via a simple JSON file, while keeping the same arguments.

Note: There are two modes:
- Dynamic interpose mode (Linux/macOS): uses the dynamic loader (`LD_PRELOAD` / `DYLD_INSERT_LIBRARIES`).
- Linux ptrace mode (x86_64 and aarch64): sets breakpoints in the target to log and optionally redirect functions, which works for both statically and dynamically linked executables.

Project Layout
--------------

- `foobar.cpp`: Simple program that prints Hello World, calls `foo(2,3)` and `bar("example")`, and prints the results.
- `foobar_lib.c`: C implementations for `foo(int,int)` and `bar(const char*)` built as `libfoobar.(so|dylib)`.
- `wrapper.cpp`: Preload library `libwrap.(so|dylib)` that intercepts `foo` and `bar`, prints "wrapper to ...", then calls the mapped/original symbol with the same arguments.
- `ptrace.cpp`: Launcher binary that runs a child process with the wrapper preloaded and passes the function map JSON path via env.
- `function_map.json`: Simple string→string mapping of intercepted symbol name to target symbol name (must have compatible signatures).
- `Makefile`: Builds everything and provides convenience targets.

Build
-----

- Build all binaries and libraries:
  - `make build`

Artifacts produced:
- `foobar` (executable)
- `ptrace` (launcher)
- `libwrap.(so|dylib)` (wrapper)
- `libfoobar.(so|dylib)` (original implementations)

Notes:
- Linux links the wrapper with `-ldl` for `dlopen`/`dlsym`.
- RPATH is set so `foobar` can find `libfoobar` in the current directory.

Run
---

- Run the demo via the launcher (preloads the wrapper and passes the map):
  - `make run`
  - This runs `./ptrace ./foobar`.
  - On Linux, default is ptrace mode (works with static and dynamic executables). Set `WRAP_MODE=preload` to force LD_PRELOAD mode.

Expected output (abridged):
- Hello, world!
- wrapper to foo
- foo(2, 3) called
- wrapper to bar
- bar("example") called
- foo returned: 5
- bar returned: 7

To run `foobar` without wrapper:
- `./foobar`

Function Mapping (JSON)
-----------------------

File: `function_map.json`

Example (identity mapping):

```
{
  "foo": "foo",
  "bar": "bar"
}
```

- Keys: intercepted symbols (e.g., `foo`, `bar`).
- Values: target symbols to call instead. If omitted, defaults to identity.
- You must ensure the mapped symbol has the same function signature as the intercepted one. Incompatible mappings are undefined behavior and likely to crash.

Change mapping and rerun, e.g., map `foo` to call the original `bar`:

```
{
  "foo": "bar",
  "bar": "bar"
}
```

Then: `make run`

Wildcard Mapping
----------------

- Keys in `function_map.json` may include wildcards:
  - `*` matches any sequence of characters (including empty)
  - `?` matches exactly one character
- Precedence:
  - Exact key match takes priority over patterns.
  - Among patterns, the first listed match in the JSON wins (order matters).
- Example:

```
{
  "foo": "foo",        // exact match preferred
  "ba*": "bar"         // matches bar, baz, ba123 → maps to bar
}
```

Caution: Only map to symbols with the same function signature as the intercepted symbol. Mapping `foo(int,int)` to a function with a different signature is undefined behavior and may crash.

How It Works
------------

- Launcher (`ptrace`):
- Linux ptrace mode (x86_64, aarch64):
  - Starts the child under `ptrace`, reads the ELF symbols from the main executable, and sets breakpoints at mapped function entries.
  - On breakpoint (SIGTRAP), prints `wrapper to <func>`.
  - If mapping redirects (e.g., `foo` → `bar`), sets the program counter to the target function entry, preserving original registers/arguments.
  - Otherwise restores the instruction, single-steps one instruction, and reinstalls the breakpoint.
  - Note: initial implementation targets functions defined in the main executable. Extending to shared libraries is possible by parsing `/proc/<pid>/maps` and their ELF files.

- Dynamic interpose mode:
  - On Linux, sets `LD_PRELOAD=./libwrap.so`.
  - On macOS, sets `DYLD_INSERT_LIBRARIES=./libwrap.dylib` and `DYLD_FORCE_FLAT_NAMESPACE=1`.
  - Sets `WRAP_MAP=./function_map.json` so the wrapper can load the mapping.
  - Forks and `execvp()` the requested program (e.g., `./foobar`).

- Wrapper (`libwrap`):
  - Intercepts `foo` and `bar`.
  - Prints `"wrapper to foo"` / `"wrapper to bar"`.
  - Loads `WRAP_MAP` once (minimal JSON parser for key/value pairs).
  - Resolves the mapped target symbol via `dlsym(RTLD_NEXT, name)`; if not found, falls back to `dlopen(./libfoobar.*)` and `dlsym()` there.
  - Calls the resolved function with the same arguments and returns its result.

Platform Notes
--------------

- Linux:
  - Two modes:
    - ptrace mode (default): works for static and dynamic executables; supports x86_64 and aarch64. Requires `ptrace` permissions (Yama/SELinux may restrict).
    - preload mode: uses `LD_PRELOAD` for interposition; wrapper links with `-ldl`.
- macOS:
  - Uses `DYLD_INSERT_LIBRARIES`; `DYLD_FORCE_FLAT_NAMESPACE=1` is set to make interposition work with flat namespace.
  - Some macOS security settings can restrict injecting into system-signed binaries; this demo targets local binaries in the current directory.

Extending
--------

- Add new intercepted functions:
  - Export the real implementation in `libfoobar` (or your own real library).
  - Add a corresponding interceptor in `wrapper.cpp` with the exact same signature and calling convention.
  - Optionally add a mapping entry in `function_map.json`.

- Port to other projects:
  - Point the wrapper’s fallback `dlopen` to your real library, or ensure `RTLD_NEXT` finds the real symbol.
  - Keep symbol names unmangled (use C functions or `extern "C"`) for simpler mapping.

Troubleshooting
---------------

- "symbol not found" at runtime:
  - Ensure the mapped target symbol exists and matches the signature.
  - Check the wrapper can find the real library (`libfoobar.(so|dylib)` resides beside the binaries).
- On Linux: undefined refs to `dlopen`/`dlsym` at link time → ensure `-ldl` is used (Makefile already does this conditionally).
- On macOS: failures to insert library → verify the `.dylib` path and note that SIP/Gatekeeper may block injection into some system binaries; use local binaries.

Cleaning
--------

- `make clean` removes `foobar`, `ptrace`, `libwrap.*`, and `libfoobar.*`.
