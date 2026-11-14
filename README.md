# PBQP

A PBQP library with a C API and a C++23 implementation. Reads and writes
problems and solves them with exact and heuristic algorithms.

Requires CMake 3.28 or newer, Ninja, Clang 21.1 with `clang-scan-deps`,
and oneTBB (`libtbb-dev`).
Select Clang through `CC`/`CXX` if it is not your default compiler.

```sh
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

List available solvers, solve a problem, or generate a random instance:

```sh
./build/pbqp --help
./build/pbqp --solve=naive --numthreads=4 examples/01_basic.pbqp
./build/pbqp --solve=bnbopt --numthreads=4 --heartbeat --verbose \
  examples/09_more_bnbopts.pbqp
./build/pbqp --solve=greedy examples/07_parallel_exact_beats_heuristics.pbqp
./build/pbqp --solve=r1r2rn examples/07_parallel_exact_beats_heuristics.pbqp
./build/pbqp_generate --mode=regalloc --nodes=1000 --edges=3000 \
  --registers=16 --seed=42 --output=build/regalloc.pbqp
./build/pbqp --solve=greedy build/regalloc.pbqp
```

Exact solvers default to one thread; `--numthreads=N` sets their TBB concurrency limit.

Optional Google Benchmark cases measure reduction scaling and exact-solver
thread counts. With `libbenchmark-dev` installed:

```sh
cmake -S . -B build -DPBQP_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/pbqp_benchmark_reduction --benchmark_repetitions=3
./build/pbqp_benchmark_bnb --benchmark_repetitions=3
./build/pbqp_benchmark_naive --benchmark_repetitions=3
```

See the [solvers](doc/solvers.md), [file format](doc/format.md), [generator modes](doc/generation.md),
[C API](include/pbqp.h), [architecture](doc/architecture.md), and [developer notes](AGENTS.md).

Licensed under [GNU GPL v3](LICENSE).
