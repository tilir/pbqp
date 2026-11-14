# Implementation layout

The public interface is the C header `include/pbqp.h`. Internal C++ code belongs
to namespace `pbqp` and the named module `pbqp`; the primary module exports no
C++ implementation names. Physical directories describe responsibilities without
changing partition names or making private partitions available to clients.

| Directory | Responsibility |
| --- | --- |
| `src/` | Primary module and library assembly in CMake |
| `src/api/` | C entry points, validation boundaries, exception translation, diagnostics |
| `src/core/` | Structural validation, ownership, reference evaluation, dynamic bitsets, independent sets, internal failures |
| `src/io/` | Tokenization, parsing helpers, cost serialization |
| `src/search/` | Prefix jobs, dispatch, percentage boundaries, covered-space accumulation |
| `src/solvers/naive/` | Sequential and parallel exhaustive enumeration |
| `src/solvers/bnb/` | Baseline bounds and shared exact B&B mechanics |
| `src/solvers/bnbopt/` | Optimized bounds, verified regalloc structure, clique-first order, capacity relaxations, bounded auxiliary pricing |
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
dispatch, progress, and reference evaluation. Baseline and optimized B&B share
identical worker/control semantics through a bounds-strategy template, with
original-ID assignments and ordered search positions. Winner selection stays
specific to the algorithm: naive preserves the first lexicographic minimum,
while B&B shares an any-optimum incumbent.

`BitSet<Word = unsigned>` owns reusable word-vector storage. Its unsigned word
type is independent of bit count. Generic `IndependentSets` accepts adjacency
bitsets; verified register structure and interpreting sets as register classes
belong to `bnbopt`. Pricing is also solver-specific and supplies integer
constraints to the reference-order cost DP.

B&B's work queue owns prepared prefix slots and selects cached lower bounds.
Workers donate sibling ranges only between DFS chunks and remove those ranges
from their private limits. Queue cancellation wakes blocked consumers before
TBB joins on scheduler failure. Shared naive planning remains separate so its
lexicographic minimum and compiled-away sequential progress path are preserved.

Every subsystem lists its exact sources in its local `CMakeLists.txt`, registers
those same paths for formatting, and propagates the aggregate lists upward.
`src/CMakeLists.txt` attaches the collected partitions to one PRIVATE module file
set on the `pbqp` target. Modules are compiled once, with no generated mirrors or
exported implementation helpers. Test and benchmark sources are registered for
formatting even when those targets are disabled.

`tests/search_test_support.cpp` and `tests/bnbopt_test_support.cpp` are test-only
implementation units of `pbqp`, compiled in the owning library target when
`BUILD_TESTING` is enabled. They import private partitions and check preparation
invariants, prefix jobs, structural eligibility, and numerical lower bounds. A namespaced C++ linkage
bridge lets the ordinary test executable invoke these checks without exposing
internal types or extending the C API. Production builds with testing disabled
do not include the bridge.

See [AGENTS.md](../AGENTS.md) for development rules and [solvers.md](solvers.md)
for algorithm, numerical, and progress contracts.
