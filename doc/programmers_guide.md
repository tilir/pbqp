# Programmer's guide

PBQP provides a stable [C API 2.5](c_api.md), a convenience
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
const pbqp_option entries[] = {
    pbqp_size_option("num-threads", 1),
    pbqp_size_option("optimization-level", 2),
    pbqp_bool_option("repricing", 1)
};
const pbqp_solve_options options = {3, entries};
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

The map is open: unknown keys are skipped with a warning by solvers that do not
consume them. Install a `warnings` callback and `warning-data` context, or use
the diagnostic sink as fallback. With no sink the C library stays silent.
The BOOL option `warnings-as-errors=true` makes unknown keys INVALID before
preparation/search, even without a sink.
Repeated keys use the last entry. Consumed keys must have the documented tag and
value. Keep entries, keys, strings, and callback contexts alive through the call.
All B&B profiles use `PBQP_SOLVER_BNB`; `optimization-level` defaults to 1.
See the generated header reference for common keys and supported B&B overrides.

Check execution status before reading a solution. FEASIBLE means an allowed
assignment, not a proof of optimality. NO_SOLUTION from a heuristic does not
prove infeasibility. Inspect `pbqp_error` on failure; it needs no initialization.
Use `pbqp_evaluate_assignment` to verify external assignments and distinguish
forbidden INF terms from feasible floating-point overflow.

## C++: RAII, vectors, spans, and exceptions

Include `pbqp.hpp` and link `pbqp`. There is no public module import requirement.
The wrapper is header-only and delegates all algorithms to C API 2.5.

```cpp
const auto problem = pbqp::Problem::read("examples/pbqp/01_basic.pbqp");
pbqp::SolveOptions options;
options.num_threads = 4;
options.parameters["optimization-level"] = std::size_t{2};
options.parameters["weighted-bounds"] = true;
options.progress = [](unsigned percent) { /* Update application progress. */ };
const auto solution = problem.solve(pbqp::Solver::BranchAndBound,
                                    options);
const auto evaluation = problem.evaluate(solution.options);
```

`Problem` is a move-only owner of a parsed problem. Its destructor releases the
native arrays. `view() const` gives a borrowed read-only `pbqp_problem` reference;
a moved-from object's `view()` throws `std::logic_error`. `write(path)` serializes
it. A `Solution` owns a `std::vector<std::size_t>` and remains valid after the
problem is destroyed; it exposes `cost`, `quality`, and `options`.
`Quality::Feasible` and `Quality::Optimal` preserve the C quality distinction.
`SolveOptions::parameters` is an open map of bool, size_t, and string values.
These entries follow the typed convenience fields, so matching keys override
those defaults. Callback functions remain managed by the wrapper.

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
warn and ignore progress callbacks, or fail in native strict mode.

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
requires major API version 2 before using other C layouts.

```python
from pbqp import Library, Solver

library = Library("build-shared/libpbqp.so")
with library.read("examples/pbqp/01_basic.pbqp") as problem:
    solution = problem.solve(Solver.BNB, num_threads=4,
                             options={"optimization-level": 2, "repricing": True})
    evaluation = problem.evaluate(solution.options)
    problem.write("build-shared/copy.pbqp")
print(solution.cost, solution.quality, solution.options)
```

`Problem.close()` is idempotent. The context manager closes on success or error;
shallow copies share a handle, and closing any alias closes the handle for
all aliases. A finalizer frees the handle when its last owner is collected.
Calls on a closed problem fail. Paths containing NUL raise `ValueError`.
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
  result = problem.solve(PBQP::Solver::BNB, num_threads: 4,
                         options: { 'optimization-level' => 2, 'repricing' => true })
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

## Unknown options and warnings

Both command-line tools warn about unknown or inapplicable options on stderr and continue,
independently of `--verbose`. Use `-Werror` to fail before solving or generating:

```sh
pbqp --solve=bnb -fno-typo INPUT
pbqp --solve=bnb -Werror -fno-typo INPUT
```

Unknown tokens do not consume the next argument; use `--name=value` to keep an
unknown value attached to its option. Malformed known options and invalid values for applicable options
remain errors. The C++ wrapper has a separate `warnings` callback, with diagnostic fallback.
Python emits `UserWarning`; Ruby uses `warn`. Both emit language-level warnings
only after returning from C and release any native solution if warning handling
raises. Their open scalar maps accept `warnings-as-errors` for native strict mode.

For example, `pbqp --solve=greedy --heartbeat INPUT` warns and solves without
progress; adding `-Werror` fails before search. Requested heuristic concurrency
above one also warns and falls back to one thread. B&B switches passed to other
algorithms warn as unrecognized solver keys. Mode-inapplicable generator options
warn and are ignored rather than changing the selected generation family.

## Exact-search timeouts and heuristic fallback

[timeout.c](../examples/programming/timeout.c) demonstrates an O2 B&B call
with a 15-second wall-time budget and a reduction fallback:

```c
const pbqp_option entries[] = {
    pbqp_size_option("optimization-level", 2),
    pbqp_size_option("timeout-ms", 15000)
};
const pbqp_solve_options options = {2, entries};
pbqp_status status = pbqp_solve(PBQP_SOLVER_BNB, problem, &options,
                               &solution, &error);
if (status == PBQP_TIMEOUT)
    status = pbqp_solve(PBQP_SOLVER_R1R2RN, problem, NULL, &solution, &error);
```

This feature requires a linked library with C API 2.2 or later; query
`pbqp_get_api_version` when loading it separately.

The `timeout-ms` SIZE key applies to every exact solver. Zero or an
omitted key means unlimited time. The budget uses a monotonic clock and starts
before input validation/preparation; it includes heuristic seeds, planning,
worker execution, and callbacks. This is cooperative stopping: an allocation,
validation pass, individual bound/reduction operation, callback, scheduler work,
or cleanup can run past the deadline. It does not forcibly interrupt threads.
All workers and callbacks have finished when the call returns, so the borrowed
problem is immediately safe to reuse or destroy.

`PBQP_TIMEOUT` returns no solution, even if B&B found a feasible incumbent.
It neither proves infeasibility nor emits a final 100% progress notification.
If a solver completes its proof without observing expiration, it can return
`PBQP_OK`/OPTIMAL or `PBQP_INFEASIBLE`. Handle other failures separately rather
than interpreting every error as a timeout. Invoke the fallback with its own
options: heuristics do not support `timeout-ms` and follow the normal
unknown-option warning policy for that key.

Run the example with its default budget, or override milliseconds for a shorter
demonstration:

```sh
./build/pbqp_example_timeout examples/pbqp/09_more_bnbopts.pbqp
./build/pbqp_example_timeout examples/pbqp/09_more_bnbopts.pbqp 1
```

The C++ convenience API accepts the same key through
`options.parameters["timeout-ms"] = std::size_t{15000}`; catch `pbqp::Error`
and inspect `error.status() == PBQP_TIMEOUT` before retrying with a heuristic.
Python and Ruby accept `options={"timeout-ms": 15000}` and
`options: {"timeout-ms" => 15000}`, respectively. Their exception status is
`Status.TIMEOUT` / `PBQP::Status::TIMEOUT`.

The CLI spelling is `--timeout=15` (seconds, up to three decimal places).
For example, `pbqp --solve=bnbopt --timeout=0.250 INPUT` requests 250 ms.
Timeout prints a diagnostic, produces no solution records, and exits with code
4. Exit codes 0/1/2/3 retain their success/error/infeasible/no-solution meanings.

## DSATUR heuristic and B&B seed

API 2.3 adds `PBQP_SOLVER_DSATUR` / `pbqp_solve_dsatur`. C++ calls use
`pbqp::Solver::Dsatur`, Python uses `Solver.DSATUR`, and Ruby uses
`PBQP::Solver::DSATUR`. The solver returns FEASIBLE even for a zero-cost result;
inspect the status before using the independently owned assignment.

```c
pbqp_status status = pbqp_solve(PBQP_SOLVER_DSATUR, problem, NULL,
                               &solution, &error);
```

The specialized algorithm requires a verified pure-interference model. General
PBQP inputs, unequal domains, coalescing, and excessive specialized workspace
use a greedy fallback; `diagnostics` / `--verbose` explain which path ran.
See [the solver description](solvers.md#dsaturtabu-heuristic) for construction,
repair, resource budgets, and the distinction between conflicts and PBQP costs.

B&B O2 enables the independent `dsatur-seed` BOOL option by default. It can be
selected on any profile; it requires the verified model but no prepared clique
or independent-set catalogue. It improves the incumbent only; bounds and proof
requirements remain unchanged.

```sh
pbqp --solve=dsatur --verbose INPUT
pbqp --solve=bnb -O1 -fdsatur-seed INPUT
pbqp --solve=bnbopt -fno-dsatur-seed INPUT
```


## Bucket elimination

Use `PBQP_SOLVER_BUCKET` with `pbqp_solve`, or call `pbqp_solve_bucket`
directly. C++ uses `pbqp::Solver::Bucket`, Python uses `Solver.BUCKET`, and
Ruby uses `PBQP::Solver::BUCKET`. This selector requires native API 2.5 or
later. Send `bucket-max-entries` as a SIZE option to set the table budget.
Inspect solution quality after `PBQP_OK`: a failed float-order certificate
emits a warning and yields FEASIBLE on success. Enable `warnings-as-errors`
to reject uncertified inputs before factor computation. An over-budget
elimination plan warns and returns `PBQP_NO_SOLUTION`. The caller selects
any subsequent solver. See [the solver guide](solvers.md#bucket-elimination).
