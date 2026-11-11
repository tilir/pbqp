# Development guide

## Project conventions

- Keep the public API strictly C-compatible in `include/pbqp.h`; implement it
  in C++23. The C test must compile as C and link against the implementation.
- Use CMake 3.28 or newer with Ninja and CTest. Named module dependencies
  are scanned by `clang-scan-deps`. Do not add a handwritten Makefile.
- Use Clang 21.1 (`clang-21` and `clang++-21`) for development builds.
  Keep its build tree in `build-clang-modules`; compiler selection belongs in the
  configure command, not in project-wide forced CMake settings. Do not change
  compilers inside an existing build tree configured with another compiler.
- Write all documentation and code comments in English.
- Start every source and header with a comment explaining its purpose and
  an SPDX `GPL-3.0-only` notice referring to `LICENSE`.
- Keep internal C++ declarations and implementations in non-exported module
  partitions (`module pbqp:name;`, stored as `.cppm`). Use module linkage
  instead of a `pbqp_detail` namespace or exported helper declarations.
  The primary `pbqp` interface exports no internal C++ names.
- Module definitions are compiled once, so small cohesive implementations can
  live directly in `.cppm` files without header-style ODR workarounds. Keep
  short accessors in their class definitions and longer methods nearby.
- Register module units with CMake `FILE_SET CXX_MODULES`. The primary `pbqp`
  unit and its internal partitions belong to a PRIVATE file set. The separate
  `pbqp.generator` interface is PUBLIC for tools and tests.
- Import private partitions as `import :name;` only from within module `pbqp`.
  Do not export the partitions to make internal helpers available to clients.
- Include the C API and standard headers in the global module fragment before
  the module declaration. Do not attach standard library headers to a named
  module, rely on transitive imports, or require `import std`. Each source must
  include standard headers for the library names it uses.
- C API definitions live in `.cpp` implementation units of module `pbqp`.
  Wrap their definitions explicitly in `extern "C"` so they match the global
  declarations from `pbqp.h`. The public header remains includable from C;
  callers do not need to import modules.
- Apply const correctness to views and accessors. Do not use `const_cast` to
  expose mutable storage from a const object.
- Use C++ streams for file and terminal I/O, and standard library ownership
  and containers internally. Avoid C stdio and allocation functions in C++ code.
- Do not introduce helper macros. Use functions, templates, and typed constants.
- Follow LLVM formatting using `clang-format-21` and the root `.clang-format`:
  two-space indentation, no tabs. Format C, C++, and module interface files.
  Do not use a different formatter version for automated changes.
- Keep the README short: purpose, build, basic usage, and documentation links.
  Put format details in `doc/format.md`, API contracts in the public header,
  and implementation guidance here. Keep these documents consistent.

## Source layout

- `include/pbqp.h`: public types, inline cost algebra, API declarations, and
  contracts for validation, ownership, diagnostics, and solution quality.
- `src/pbqp.cppm`: primary module interface with no exported internal names.
- `src/text_reader.cppm`: private `:text_reader` partition containing the stream
  tokenizer, comment handling, line tracking, and numeric parser.
- `src/problem.cppm`: private `:problem` partition containing structural
  validation and dimension checks.
- `src/problem_api.cpp`: C problem destruction and validation entry points.
- `src/io.cpp`: C text input/output entry points and error translation.
- `src/error.cppm`: private `:error` partition with non-throwing diagnostics.
- `src/failure.cppm`: private `:failure` partition for parsing/validation errors.
- `src/ownership.cppm`: private `:ownership` partition with RAII aliases.
- `src/bruteforce.cpp`: exhaustive reference solver; uses no exceptions.
- `tools/pbqp.cpp`: named solver selection and solver CLI.
- `tools/command_line.cppm`: shared `pbqp.command_line` module for named options,
  help flags, duplicate detection, and positional input paths.
- `tools/generator.cppm`: public `pbqp.generator` module containing generator
  options, owning `RandomProblem`, and `ProblemGenerator` with implementations.
  `view() const noexcept` exposes a borrowed, read-only C problem view.
- `tools/generate.cpp`: generator CLI argument handling.
- `tests/test_pbqp.cpp`: examples, round trips, invalid input, arithmetic,
  diagnostics, quality, and comparison with independent recursive search.
- `tests/test_c_api.c`: C compatibility and linkage checks.
- `tests/cli.cmake`: end-to-end command-line usage, help, and diagnostic checks.

## Public API contracts

A `pbqp_problem` can describe caller-owned arrays or be owned by the parser.
Solvers borrow the problem and all arrays only during a call, do not modify
or retain them, and return solutions with independently owned `options`.
Keep borrowed inputs alive and unchanged until the call returns.

Only `pbqp_read` results may be passed to `pbqp_problem_free`, apart from
`NULL`. Treat parsed objects and their arrays as read-only: preserve pointers,
counts, dimensions, and node/edge metadata because destruction uses these
fields. Caller-owned views must never be passed to this deleter.

Only solver-returned solutions may be passed to `pbqp_solution_free`, apart
from `NULL`. Preserve their original `options` pointer. Solutions outlive
their input problems.

Public write and solver entry points validate input themselves. Validation
checks positive option counts, non-NULL arrays for positive counts, endpoint
bounds with `src < tgt`, duplicate edges, matrix dimensions, and cost values.
For a zero count, its array pointer may be NULL or non-NULL and is not read.
Actual pointer validity and allocation lengths cannot be inferred: callers
must supply readable arrays with at least the declared lengths.

No C++ exception may escape the C API. Parsing and validation may use internal
exceptions translated at the boundary. Do not add `-fno-exceptions`; modules
use consistent compiler options. The solver itself must use status returns;
do not add exceptions, allocation, or validation inside the tuple loop.

For APIs with output pointers, set outputs to NULL on failure. A supplied
`pbqp_error` needs no initialization. Its message must always be NUL-terminated;
truncate diagnostics if needed. Success clears `line` and `message[0]`.
Failures provide a diagnostic and a source line if known, otherwise zero.
Bytes after the message terminator are unspecified.

## Costs and solver results

`pbqp_cost_t` is `float`. Keep `pbqp_cost_add(a, b)` as an inline wrapper
returning ordinary `a + b`, even though it currently needs no abstraction.
Do not add checked, saturating, or exact accumulation. `PBQP_INF` is positive
infinity. Input costs may be finite or positive infinity; reject NaN and
negative infinity. Arithmetic overflow follows floating-point behavior and
does not return `PBQP_OVERFLOW`. That status remains useful for unrepresentable
input numbers and sizes.

Explicit INF terms forbid a tuple. Distinguish these from a feasible tuple
whose sum overflows to infinity. Preserve reference summation order: unary
costs in node ID order, then edge costs in stored order. The reference solver
enumerates tuples lexicographically and keeps the first minimum, including
when multiple costs compare equal. The empty problem has one empty solution
with cost zero.

Execution status and result quality are separate. A solver's `PBQP_OK` returns
a solution; inspect `quality` to distinguish `PBQP_SOLUTION_FEASIBLE` from
`PBQP_SOLUTION_OPTIMAL`. The reference solver sets OPTIMAL after exhaustive
search. `PBQP_INFEASIBLE` requires proof that no feasible assignment exists.
A heuristic failing to find an assignment must not return it. Define a separate
no-result status when introducing a solver that can stop without a solution
or proof; do not conflate that outcome with infeasibility or success.

Input accepts decimal fractions and scientific notation. Keep the classic
locale for serialization and use `max_digits10` precision so finite float
costs round-trip. Preserve the PBQP 1 ordering and comment rules documented
in `doc/format.md`.

## Tools and integration

Use `add_subdirectory` and link the `pbqp` CMake target to embed the library.
C callers need the C++ runtime at final link time; CMake handles this.
The `pbqp_cli` target produces the `pbqp` executable. `pbqp_generator` is the
reusable generator library; `pbqp_generate` is its CLI. Both tools link the
shared `pbqp_command_line` library for argument parsing.

Invoke the solver as `pbqp --solve=naive INPUT`. `pbqp --help` lists available
solvers; currently `naive` selects the exhaustive reference solver. Do not
reintroduce `solve` or `dump` positional subcommands. Problem dumping remains
available to library users through `pbqp_write`.
The solver CLI prints `COST <cost>` and zero-based `NODE <id> <option>` records.
Exit codes are 0 for success, 1 for errors, and 2 for proven infeasibility.
Exhaustive search evaluates a number of tuples equal to the product of all
node option counts and is intended for small instances.

Generator options are `--nodes=N --edges=M --max-options=D --seed=S --output=FILE`;
all are required. `pbqp_generate --help` describes them. Named options accept
either `--name=value` or `--name value` in any order; reject duplicates, unknown
options, missing values, and invalid numbers. The solver accepts `--` before
positional paths starting with `--`. The generator accepts no positional values.
Nodes have 1 through D options, with finite costs sampled in quarter steps
from [-10, 10] and a 10% chance of INF per entry. The graph has exactly the
requested number of distinct edges. Generated problems may be infeasible.
Seeds reproduce output with the same build and standard library; do not
promise identical sampling across different standard library implementations.

## Verification

With `clang-format-21` installed, CMake provides formatting targets:

```sh
cmake --build build-clang-modules --target format
cmake --build build-clang-modules --target check-format
```

`format` updates source files; `check-format` fails if formatting differs.
The formatting file list includes `.cppm` interfaces and the C API test.
The targets are available only when CMake finds `clang-format-21`.

`.github/workflows/ci.yml` runs on pushes, pull requests, and manual dispatch.
It uses Ubuntu 24.04, the official LLVM apt repository, Clang 21, and Ninja.
The Release job checks formatting; both Release and ASan/UBSan jobs build and
run CTest, including the C interoperability test. Sanitizer errors fail CI,
and leak checking remains enabled on GitHub's runner.

IWYU integration is deferred. IWYU 0.25 built against Clang 21 was tested:
it suggests textual includes of imported `.cppm` files and crashes on one
of the module consumers. Do not introduce generated header mirrors, suppress
these failures, or silently omit C++ files to claim full IWYU coverage.
Reconsider integration when named modules are supported reliably.

Run the relevant existing tests for behavior changes:

```sh
cmake -G Ninja -S . -B build-clang-modules -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21
cmake --build build-clang-modules
ctest --test-dir build-clang-modules --output-on-failure
```

Checks remain active in Release builds. Preserve the C interoperability test,
float round trips, and independent recursive comparisons on generated small
problems. Current example optima are 0, 40, and 80 for examples 1, 2, and 3.
When changing float tests, account for summation order and rounding; the random
reference comparisons use quarter-step inputs whose small sums are exact.

For memory-sensitive changes, an additional sanitizer build is useful:

```sh
cmake -G Ninja -S . -B build-clang-modules-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-clang-modules-sanitize
ctest --test-dir build-clang-modules-sanitize --output-on-failure
```

LeakSanitizer cannot run under ptrace in some execution environments. In that
case, run the same tests with `ASAN_OPTIONS=detect_leaks=0` and report that
leak checking was unavailable. Documentation-only changes do not require
rebuilding or rerunning the test suite.
