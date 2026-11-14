# Programmer's guide

PBQP provides a stable [C API 1.0](c_api.md), a convenience
[C++ header](../include/pbqp.hpp), and small Python/Ruby bindings. The native
implementation and module partitions remain private. Runnable programs live in
[examples/programming](../examples/programming); problem data lives in
[examples/pbqp](../examples/pbqp). All examples are GPL-3.0-only, like the library.

## Build and embed

Build the native library and programming examples:

```sh
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DPBQP_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/pbqp_example_c
./build/pbqp_example_cpp examples/pbqp/01_basic.pbqp
```

Use Clang 21.1, CMake 3.28+, Ninja, `clang-scan-deps`, and oneTBB as described
in the [README](../README.md). Select the compiler through CC/CXX when needed.
Do not change the compiler of an existing build tree.

To embed the library in a CMake project:

```cmake
project(client LANGUAGES C CXX)
add_subdirectory(path/to/pbqp pbqp-build)
add_executable(client main.c)
target_link_libraries(client PRIVATE pbqp)
```

CMake supplies the include directory, native library, C++ runtime, and TBB link
dependencies. The library needs C++23 and module scanning; a C caller stays C.
A C++ caller using `pbqp.hpp` should request `cxx_std_23` for its own target.
Set `BUILD_TESTING=OFF` when embedding without PBQP's tests. Examples default
to disabled. Installation/package discovery is not provided; use the CMake
subdirectory target or an explicit library/header path.

## C: borrowed arrays and explicit ownership

[solve.c](../examples/programming/solve.c) builds two nodes and one edge on the
stack, then calls the enum dispatcher. Each node has two options. Choosing
option 1 costs zero, but the edge forbids choosing option 1 at both endpoints,
so the optimum costs one.

```c
pbqp_solve_options options = {1, NULL, NULL, NULL, NULL};
pbqp_solution *solution = NULL;
pbqp_error error;
pbqp_status status = pbqp_solve(PBQP_SOLVER_BNB, &problem, &options,
                               &solution, &error);
if (status == PBQP_OK) {
    /* Read solution->cost, quality, num_nodes, and options here. */
}
pbqp_solution_free(solution);
```

Do not call `pbqp_problem_free` on this stack-backed view. When reading a file
with `pbqp_read`, free that parser-owned problem with `pbqp_problem_free`.
Solvers validate automatically, borrow inputs only during the call, and return
independent solution storage. Pass NULL options for the defaults. The dedicated
solver functions use the same options/output contract as the dispatcher.

Check execution status before reading a solution. FEASIBLE means an allowed
assignment, not a proof of optimality. NO_SOLUTION from a heuristic does not
prove infeasibility. Inspect `pbqp_error` on failure; it needs no initialization.
Use `pbqp_evaluate_assignment` to verify external assignments and distinguish
forbidden INF terms from feasible floating-point overflow.

## C++: RAII, vectors, spans, and exceptions

Include `pbqp.hpp` and link `pbqp`. There is no public module import requirement.
The wrapper is header-only and delegates all algorithms to C API 1.0.

```cpp
const auto problem = pbqp::Problem::read("examples/pbqp/01_basic.pbqp");
pbqp::SolveOptions options;
options.num_threads = 4;
options.progress = [](unsigned percent) { /* Update application progress. */ };
const auto solution = problem.solve(pbqp::Solver::BranchAndBoundOptimized,
                                    options);
const auto evaluation = problem.evaluate(solution.options);
```

`Problem` is a move-only owner of a parsed problem. Its destructor releases the
native arrays. `view() const` gives a borrowed read-only `pbqp_problem` reference;
a moved-from object's `view()` throws `std::logic_error`. `write(path)` serializes
it. A `Solution` owns a `std::vector<std::size_t>` and remains valid after the
problem is destroyed; it exposes `cost`, `quality`, and `options`.
`Quality::Feasible` and `Quality::Optimal` preserve the C quality distinction.

For application-owned C views backed by arrays or vectors, use the free
functions `pbqp::validate(view)`, `pbqp::write(path, view)`,
`pbqp::solve(view, solver, options)`, and `pbqp::evaluate(view, options)`.
Evaluation accepts a `std::span<const std::size_t>`, so arrays and vectors work
without copying. Keep the view and its arrays alive during these calls.

C failures become `pbqp::Error`, which derives from `std::runtime_error` and
preserves `status()`, `line()`, and the diagnostic `what()` message. Normal C++
allocation exceptions can also occur while copying results or preparing
callbacks. The runnable [solve.cpp](../examples/programming/solve.cpp) shows RAII,
vector ownership after problem destruction, diagnostics, and error handling.

`SolveOptions` uses `std::function` callbacks: `progress(unsigned)` and
`diagnostics(std::string_view)`. Diagnostic text is borrowed for the callback.
Exceptions from either callback are captured, further callbacks suppressed,
and the exception rethrown after the native solve returns. This does not cancel
search and never lets an exception cross the C callback boundary. Heuristics
reject progress callbacks, just as the C API does.

The stable binary boundary is the C API. The C++ wrapper must be recompiled
with the consuming compiler/standard library; it does not promise a cross-toolchain
C++ ABI.

## Python: ctypes, no third-party packages

The reusable binding is [bindings/python/pbqp.py](../bindings/python/pbqp.py).
It requires Python 3.10+ and a shared PBQP library. Build one in a separate tree:

```sh
cmake -G Ninja -S . -B build-shared -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON -DPBQP_BUILD_EXAMPLES=ON
cmake --build build-shared
ctest --test-dir build-shared --output-on-failure
PYTHONPATH=bindings/python python3 examples/programming/solve.py \
  build-shared/libpbqp.so examples/pbqp/01_basic.pbqp
```

The example uses an explicit native-library path; it does not search the system
for an arbitrary PBQP installation. The loader queries the runtime version and
requires major API version 1 before using other C layouts.

```python
from pbqp import Library, Solver

library = Library("build-shared/libpbqp.so")
with library.read("examples/pbqp/01_basic.pbqp") as problem:
    solution = problem.solve(Solver.BNBOPT, num_threads=4)
    evaluation = problem.evaluate(solution.options)
    problem.write("build-shared/copy.pbqp")
print(solution.cost, solution.quality, solution.options)
```

`Problem.close()` is idempotent. The context manager closes on success or error;
a finalizer also frees abandoned problems. Calls on a closed problem fail.
Results contain immutable Python-owned tuples; native solution storage is freed
before `solve` returns. The problem and library stay alive while in use.
`PBQPError` exposes `status` (`Status`) and `line`. Unknown solver selections,
unrepresentable indices, and invalid thread counts raise `ValueError`.

This binding exposes file-backed problems, solving, serialization, and assignment
evaluation. It does not expose raw caller-owned arrays or native callbacks.
Use the C/C++ API for those features. Do not close a problem concurrently with
another operation on it.

## Ruby: Fiddle, no gems

The reusable binding is [bindings/ruby/pbqp.rb](../bindings/ruby/pbqp.rb), using
standard-library `fiddle/import`. Run the Ruby example against the same shared
build:

```sh
ruby -Ibindings/ruby examples/programming/solve.rb \
  build-shared/libpbqp.so examples/pbqp/01_basic.pbqp
```

```ruby
require 'pbqp'

library = PBQP::Library.new('build-shared/libpbqp.so')
solution = library.read('examples/pbqp/01_basic.pbqp') do |problem|
  result = problem.solve(PBQP::Solver::BNBOPT, num_threads: 4)
  evaluation = problem.evaluate(result.options)
  problem.write('build-shared/copy.pbqp')
  result
end
puts solution.cost
```

Block-form `read` closes the problem even when the block raises. Without a block,
call `close` explicitly; a finalizer handles abandoned objects. `dup` and `clone`
share one mutable native handle. Closing any alias closes every alias, and later
operations raise `IOError`. Freezing a public wrapper does not prevent safe,
idempotent closure. Only the shared handle owns finalization, so collecting one
wrapper cannot free storage still referenced by another. Returned solution
records and option arrays are frozen Ruby-owned copies and outlive the native
problem. `PBQP::Error` exposes integer `status` and `line`; the `Status`, `Solver`,
and `Quality` modules define the corresponding C API values. Invalid Ruby options
raise `ArgumentError`; closed handles raise `IOError`.

Like the Python binding, this covers file-backed problems, solving, writing, and
evaluation without callbacks or raw-array construction. Both bindings use the
platform's default C ABI; the shared-library commands above target Linux.
Ruby's binding supports native 32-bit and 64-bit `size_t`. Its executable checks
run when Ruby is available in a shared build with tests and examples enabled.

## Testing and extending clients

C and C++ examples are ordinary CMake executables. Python and Ruby are optional
runtime dependencies: shared builds register their binding and example tests
when the interpreters are found and `PBQP_BUILD_EXAMPLES=ON`. The core build
never downloads language packages or requires either interpreter.

Keep all callback contexts alive until the synchronous call returns. Progress
is serialized covered-space accounting, not wall time or an ETA. Borrowed input
must remain unchanged, and outputs/errors need separate storage for concurrent
calls. For the complete numeric, ownership, version, and diagnostic contracts,
consult the [C API reference](c_api.md) and the public header.
