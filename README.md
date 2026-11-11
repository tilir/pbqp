# PBQP

A PBQP library with a C API and a C++23 implementation. Reads and writes
problems and solves them with exhaustive or greedy search.

Requires CMake 3.28 or newer, Ninja, and Clang 21.1 with `clang-scan-deps`.
Select Clang through `CC`/`CXX` if it is not your default compiler.

```sh
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

List available solvers, solve a problem, or generate a random instance:

```sh
./build/pbqp --help
./build/pbqp --solve=naive examples/1.pbqp
./build/pbqp --solve=naive --heartbeat examples/5.pbqp
./build/pbqp --solve=greedy examples/3.pbqp
./build/pbqp_generate --mode=regalloc --nodes=1000 --edges=3000 \
  --registers=16 --seed=42 --output=/tmp/regalloc.pbqp
./build/pbqp --solve=greedy /tmp/regalloc.pbqp
```

See the [file format](doc/format.md), [generator modes](doc/generation.md),
[C API](include/pbqp.h), and [developer notes](AGENTS.md).

Licensed under [GNU GPL v3](LICENSE).
