# Solvers

Select a solver with `pbqp --solve=NAME INPUT`; `pbqp --help` lists the names.
All solvers report costs in the reference evaluation order: unary terms in
node ID order, then pairwise terms in stored edge order, using ordinary float
addition. Input validation and solution ownership rules are in `include/pbqp.h`.

## Naive exhaustive search

`--solve=naive` enumerates every tuple in lexicographic order. It retains the
first minimum and returns OPTIMAL, or proves INFEASIBLE after exhausting the
space. The number of tuples is the product of the node option counts.

`--numthreads=N` also parallelizes naive search; its default is 1. Each TBB
consumer enumerates disjoint prefix jobs with private tuple/minimum buffers.
No tuple is pruned, even if it contains INF. After joining, minima are merged
by cost and lexicographic assignment, preserving naive's first-optimum contract
independently of scheduling. Each tuple still uses the original sequential
float summation order; worker results are never combined by adding costs.

The C entry point `pbqp_solve_bruteforce_with_options` accepts shared
`pbqp_solve_options` (thread limit, callback, user data). Legacy naive entry
points retain the direct single-thread implementation. Parallel progress counts
all visited tuples using the same exact covered-space accumulator as B&B.

## Branch-and-bound

```sh
pbqp --solve=bnb --numthreads=4 --heartbeat \
  examples/07_exact_beats_heuristics.pbqp
```

`bnb` is exact under the reference float semantics. It returns any optimum;
the winning assignment on a tie can depend on scheduling. `naive` retains its
lexicographic tie rule. `--numthreads=N` is positive, defaults to 1, and is
accepted by both exact solvers. It limits oneTBB arena concurrency, including the caller;
workload size and available TBB resources may reduce actual concurrency.

Preparation checks the root bound first. A root infeasibility proof skips both
heuristics. Otherwise greedy supplies a cheap initial incumbent; if its cost
matches the root lower bound, the optimum is already proved. Reduction runs
only when a better incumbent can still matter, avoiding unnecessary cubic R2
work and fill allocations on trivially solved inputs. Both root proofs retain
full covered-space progress, reported after successful preparation.

Greedy and reduction supply initial feasible incumbents. Failure of either
heuristic supplies no bound and never proves infeasibility. Improvements are
committed under a mutex: recheck the candidate, copy its assignment, then publish
the cost atomically. Frequent pruning reads only the feasibility flag and cost,
not the assignment. An older observed cost is a weaker bound and permits extra
work. With any feasible incumbent G, a bound L >= G closes a subtree. A feasible
incumbent whose accumulated cost is infinity is distinct from no incumbent.

Nodes are assigned in ID order; options are visited by ascending unary cost,
then option ID. For each prefix, forward checking removes unary-INF options
and options forbidden by already assigned neighbors. Independent minima over
the surviving options provide the bound's reference-order terms:

- Assigned unary terms and fully assigned edges use selected input costs.
- Unassigned unary terms use the cheapest remaining allowed option.
- Edges to assigned sources use the selected row's remaining allowed columns.
- Unresolved edges use the minimum over remaining allowed rows and columns.

Cached unary, row, and matrix minima cover unrestricted cases. A missing allowed
term proves infeasibility of the prefix. All original finite term minima are
added in exactly the reference order. Monotonic rounded addition preserves the
bound through negative costs, cancellation, and overflow. Suffix sums are never
regrouped. Explicit INF and an overflowed accumulated sum remain separate.

For uniform regalloc-shaped inputs, B&B also detects register symmetry directly
from the input: nonzero options have zero unary costs, spill rows/columns and
unequal-register pairs are zero, and equal-register pairs are INF. Only after
all nodes and matrices pass this test are new registers introduced in ascending
order. Every discarded naming has a retained renaming with the same reference
terms, so this removes redundant subtrees without changing the optimum. General
PBQP inputs use the unrestricted search.

Preparation grows a frontier of viable prefixes toward roughly 32 coarse jobs
per requested thread. Depth follows available work and heterogeneous domains,
rather than a fixed number of nodes. Very large domains group adjacent child
prefixes; planning also has a finite fan-out expansion budget. Runs of
single-option nodes are appended in place and checked once, without consuming
that budget, so a long forced prefix does not prevent later parallel branching. Prefixes already closed
during planning are retained as progress events. The same planning policy helps
single-thread search, which executes directly without invoking the TBB scheduler.

Each TBB consumer claims jobs from a shared atomic index and runs iterative
sequential DFS within each subtree. Jobs are never created per DFS vertex.
Immutable input/minima are shared; assignments, DFS cursors, bound scratch, and
local best solutions belong to workers. Allocations occur during preparation
and TBB orchestration, outside DFS. C API calls remain synchronous, joining all
work before returning independently owned solution storage. The options-based
C entry point is `pbqp_solve_branch_and_bound_with_options`; legacy entry points
use one thread. Runtime/scheduler exceptions become INTERNAL_ERROR.

The final winner is checked and evaluated against the original problem. Success
returns OPTIMAL; exhausting the remaining space without a feasible incumbent
proves INFEASIBLE. Worst-case search remains exponential, and OPTIMAL refers
to sequential float evaluation rather than an exact real sum.

Example 07 has a feasible zero-cost assignment and nonnegative input terms, so
its optimum is zero. It is available as an optional Google Benchmark case for
1/2/4/8 threads, rather than an exhaustive routine CTest workload. Timing includes
prefix planning and the resulting search; changes in planning and incumbent
arrival can change the amount of work as well as how many CPUs execute it.

The frontier stays fixed after planning. TBB schedules whole consumers, and
cannot steal a descendant from a worker's active DFS. A difficult final job can
therefore leave other workers idle. Before introducing dynamic splitting,
profile bounds evaluated, pruned prefixes, completed jobs, and final-job time
per worker, together with separate preparation, seeding, planning, and search
times. Wall-time speedup alone cannot distinguish parallel execution from a
better order of incumbent discoveries.

Both examples show the benefit of exact search over the heuristics. Example 08
also has enough search work to demonstrate parallel speedup; its benchmark
compares 1/4/16 threads with heartbeat enabled and disabled.

| Example | Greedy | R1/R2/RN | Exact optimum |
| --- | ---: | ---: | ---: |
| [07_exact_beats_heuristics.pbqp](../examples/07_exact_beats_heuristics.pbqp) | 22.1286182 | 22.1286182 | 0 |
| [08_parallel_exact_beats_heuristics.pbqp](../examples/08_parallel_exact_beats_heuristics.pbqp) | 91.4585114 | 54.0987892 | 10.1112299 |

## Greedy heuristic

`--solve=greedy` assigns nodes with the largest finite unary cost range first,
breaking ties by ID. It chooses the first cheapest allowed option using costs
to already assigned nodes. This is cheap and may be suboptimal. A successful
result is FEASIBLE; a dead end is NO_SOLUTION, not proof of INFEASIBLE.

## R1/R2/RN reduction heuristic

```sh
pbqp --solve=greedy examples/06_too_long_for_any_exact.pbqp
pbqp --solve=r1r2rn examples/06_too_long_for_any_exact.pbqp
```

`--solve=r1r2rn` implements the reduction/backpropagation scheme used by LLVM
PBQP. It clones the input graph and repeatedly removes a node:

- R0: disconnect an isolated node, retaining its unary costs.
- R1: remove a degree-one node x. For each option of its neighbor y, add
  `min_x (edge(x,y) + unary(x))` to y's unary cost.
- R2: remove a degree-two node x with neighbors y and z. Add the matrix
  `delta(y,z) = min_x (edge(y,x) + edge(x,z) + unary(x))` to their existing
  edge, or create a fill edge if they were unconnected.
- RN: when no degree-zero/one/two node remains, disconnect a high-degree node
  without propagating its costs. This is the heuristic step.

Low-degree nodes are processed by ID. RN first prefers the lowest-ID node with
conservatively available nonzero options: either the sum of worst per-neighbor
forbidden-option counts is smaller than its available option count, or one
option is compatible with every allowed neighbor choice. Otherwise RN chooses
the lowest current unary cost for option zero, then lowest degree and ID.
Option zero is the spill preference in regalloc inputs. Other inputs, including
singleton domains and forbidden option zero, are accepted too.

The design follows LLVM's `llvm/include/llvm/CodeGen/PBQP/ReductionRules.h`
and `llvm/include/llvm/CodeGen/RegAllocPBQP.h`. It adapts the allocatability
test to arbitrary domains and explicit prohibitions, including a neighbor's
option zero; it does not depend on LLVM or promise identical assignments.

Active adjacency records each edge's position at both endpoints. Removal uses
swap-and-pop, avoiding repeated scans and shifts on high-degree neighbors.
When a node is selected for reduction, its adjacency is sorted by edge ID and
frozen. This retains input/fill edge order in arithmetic and reconstruction.

Low-degree, conservatively allocatable, and spill candidates have separate
ordered queues. Cached per-edge availability contributions are updated when
an edge is added, changed, or removed. When R1 changes unary feasibility, all
incident contributions are recomputed in both directions; a finite score-only
change refreshes just that node's priority. RN selection therefore does not
rescan unrelated active or eliminated nodes. Queue updates take logarithmic
time; matrix work remains local to affected edges.

Preparation allocates the graph and R2 fill matrices and records the reduction
order. Surviving adjacency excludes removed nodes, while removed nodes retain
their frozen costs and edges to later nodes. Allocation-free backpropagation
visits the order in reverse and chooses the first cheapest allowed option given
already assigned neighbors. There is no backtracking or heartbeat mode.

Intermediate costs retain a separate feasibility flag: a feasible overflow to
infinity must not become a forbidden choice. All additions use `pbqp_cost_add`.
Regrouping overflowed intermediate values can also create NaN; numerical
scores take precedence over NaN scores, with first-option ties when all scores
are NaN. This affects heuristic preferences, never input validation or the
final reference-order evaluation.

Every success returns FEASIBLE, including empty problems and graphs reduced
using only R0/R1/R2. These local reductions are exact in real arithmetic, but
regrouping float sums can change the reference-order optimum. RN can also
prevent a feasible assignment from being reconstructed. A dead end returns
NO_SOLUTION, never INFEASIBLE. The final assignment is checked against the
original problem and its cost is recomputed by the shared reference evaluator.

Example 06 runs through both heuristics in CTest, without starting exact
search. Neither heuristic is guaranteed to dominate the other on all inputs.

## Heartbeat

`--heartbeat` is supported by `naive` and `bnb`. It writes at most 100 flushed
dots to stderr, one per completed integer percentage, with a final newline.
Solution records remain on stdout. No dot is emitted at zero progress.

Naive counts visited tuples; its parallel callbacks are serialized too.
Branch-and-bound adds the sizes of completely
searched or pruned prefix subtrees, including symmetry cuts. Subtrees may close
out of order, so coverage uses mixed-radix addition rather than a lexicographic
frontier. Workers accumulate coverage privately and merge at job completion,
after 256 small closed prefixes, or immediately for a cut covering at least
one percent of the space. This keeps the progress mutex out of most search
steps. Batches preserve exact mixed-radix counts: notifications can lag completed
work but never report an unfinished percentage. Worker tails are flushed before
return, including on an infeasibility proof.
Callbacks are serialized and may run on a TBB worker; no callback
outlives the synchronous solver call. A cut subtree may therefore
advance several dots immediately. This measures search-space coverage, not
elapsed time or an ETA. Percentages remain valid when the product of domain
sizes exceeds fixed-width integer ranges. Even an infeasibility proof reports
complete progress; errors before search do not report progress.
