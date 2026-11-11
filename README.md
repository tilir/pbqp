# PBQP

A PBQP library with a C API and a C++23 implementation. Reads and writes
problems and solves small instances by exhaustive search.

Requires CMake 3.28 or newer, Ninja, and Clang 21.1 with `clang-scan-deps`.

```sh
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

List available solvers, solve a problem, or generate a random instance:

```sh
./build/pbqp --help
./build/pbqp --solve=naive examples/1.pbqp
./build/pbqp_generate --nodes=5 --edges=7 --max-options=3 --seed=42 --output=/tmp/random.pbqp
```

See the [file format](doc/format.md), [C API](include/pbqp.h), and
[developer notes](AGENTS.md).

Licensed under [GNU GPL v3](LICENSE).
