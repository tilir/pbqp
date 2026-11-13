# Implementation layout

The public interface is the C header `include/pbqp.h`. Internal C++ code belongs
to namespace `pbqp` and the named module `pbqp`; the primary module exports no
C++ implementation names. Physical directories describe responsibilities without
changing partition names or making private partitions available to clients.

| Directory | Responsibility |
| --- | --- |
| `src/` | Primary module and library assembly in CMake |
| `src/api/` | C entry points, validation boundaries, exception translation, diagnostics |
| `src/core/` | Structural validation, ownership, reference evaluation, internal failures |
| `src/io/` | Tokenization, parsing helpers, cost serialization |
| `src/search/` | Prefix jobs, dispatch, percentage boundaries, covered-space accumulation |
| `src/solvers/naive/` | Sequential and parallel exhaustive enumeration |
| `src/solvers/bnb/` | Bounds, incumbent, prefix planning, worker DFS, orchestration |
| `src/solvers/greedy/` | Cost-priority assignment |
| `src/solvers/reduction/` | Reduction graph, candidate worklist, propagation and reconstruction |

C entry points validate borrowed data, invoke the implementation, translate
exceptions, and transfer independent output ownership. Text I/O entry points
stay in `api/`; stream mechanics stay in `io/`. oneTBB headers and scheduler
calls stay in `search/parallel.cpp` because some TBB helpers cannot be serialized
in a GCC module partition.

`core` and shared `search` must not import concrete solvers. The B&B orchestrator
may invoke greedy and reduction for incumbent seeds; its bounds and workers do
not depend on either heuristic. Shared mechanisms include prefix descriptors,
dispatch, progress, and reference evaluation. Winner selection stays specific to
the algorithm: naive preserves the first lexicographic minimum, while B&B shares
an any-optimum incumbent.

Every subsystem lists its exact sources in its local `CMakeLists.txt`, registers
those same paths for formatting, and propagates the aggregate lists upward.
`src/CMakeLists.txt` attaches the collected partitions to one PRIVATE module file
set on the `pbqp` target. Modules are compiled once, with no generated mirrors or
exported implementation helpers. Test and benchmark sources are registered for
formatting even when those targets are disabled.

`tests/search_test_support.cpp` is a test-only implementation unit of `pbqp`,
compiled in the owning library target when `BUILD_TESTING` is enabled. It imports
private partitions and checks preparation invariants, including whether a seed
was attempted and how many prefix jobs were produced. A namespaced C++ linkage
bridge lets the ordinary test executable invoke these checks without exposing
internal types or extending the C API. Production builds with testing disabled
do not include the bridge.

See [AGENTS.md](../AGENTS.md) for development rules and [solvers.md](solvers.md)
for algorithm, numerical, and progress contracts.
