# PBQP

A PBQP library with a C API and a C++23 implementation. Reads and writes
problems and solves them with exact and heuristic algorithms.

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
./build/pbqp --solve=naive examples/01_basic.pbqp
./build/pbqp --solve=bnb --heartbeat examples/05_too_long_for_naive.pbqp
./build/pbqp --solve=greedy examples/06_too_long_for_any_exact.pbqp
./build/pbqp --solve=r1r2rn examples/06_too_long_for_any_exact.pbqp
./build/pbqp_generate --mode=regalloc --nodes=1000 --edges=3000 \
  --registers=16 --seed=42 --output=/tmp/regalloc.pbqp
./build/pbqp --solve=greedy /tmp/regalloc.pbqp
```

Optional Google Benchmark cases measure reduction on stars and disjoint K4
graphs. With `libbenchmark-dev` installed:

```sh
cmake -S . -B build -DPBQP_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pbqp_benchmark_reduction
./build/pbqp_benchmark_reduction --benchmark_repetitions=3
```

See the [solvers](doc/solvers.md), [file format](doc/format.md), [generator modes](doc/generation.md),
[C API](include/pbqp.h), and [developer notes](AGENTS.md).

Licensed under [GNU GPL v3](LICENSE).
