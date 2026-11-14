# Solvers

Select a solver with `pbqp --solve=NAME INPUT`; `pbqp --help` lists the names.
All solvers report costs in the reference evaluation order: unary terms in
node ID order, then pairwise terms in stored edge order, using ordinary float
addition. Input validation and solution ownership rules are in `include/pbqp.h`.

## C API selection

Each solver has one dedicated entry point with the signature
`(problem, options, out, error)`. Pass NULL options for one thread without
callbacks. `pbqp_solve_options` contains the thread limit, progress callback and
its `user_data`, and diagnostic callback with its independent `diagnostic_data`.
Progress callbacks are supported by exact solvers; heuristics reject them with
`PBQP_INVALID` and always use one thread. A thread limit above one is valid for
a heuristic but does not make it parallel.

`pbqp_solve(solver, problem, options, out, error)` dispatches by `pbqp_solver`:

| Enum | Dedicated entry | Result quality on success |
| --- | --- | --- |
| `PBQP_SOLVER_NAIVE` | `pbqp_solve_bruteforce` | OPTIMAL |
| `PBQP_SOLVER_BNB` | `pbqp_solve_branch_and_bound` | OPTIMAL |
| `PBQP_SOLVER_BNBOPT` | `pbqp_solve_branch_and_bound_optimized` | OPTIMAL |
| `PBQP_SOLVER_GREEDY` | `pbqp_solve_greedy` | FEASIBLE |
| `PBQP_SOLVER_R1R2RN` | `pbqp_solve_reduction` | FEASIBLE |

Unknown enum values return `PBQP_INVALID` without callbacks. Both forms validate
input and options, clear the output pointer on failure, and transfer ownership
of an independent solution on success. For example:

```c
pbqp_solve_options options = {4, NULL, NULL, NULL, NULL};
pbqp_solution *solution = NULL;
pbqp_error error;
pbqp_status status = pbqp_solve(PBQP_SOLVER_BNBOPT, problem, &options,
                               &solution, &error);
/* Inspect status before using solution. NULL is safe to free. */
pbqp_solution_free(solution);
```

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

The sole C entry point `pbqp_solve_bruteforce` accepts shared
`pbqp_solve_options` (thread limit, progress and diagnostic callbacks with
independent contexts). NULL options select the direct single-thread path
without callbacks. Parallel progress counts
all visited tuples using an exact mixed-radix covered-space accumulator.

## Branch-and-bound

```sh
pbqp --solve=bnb --numthreads=4 --heartbeat \
  examples/pbqp/06_exact_beats_heuristics.pbqp
```

`bnb` is exact under the reference float semantics. It returns any optimum;
the winning assignment on a tie can depend on scheduling. `naive` retains its
lexicographic tie rule. `--numthreads=N` is positive, defaults to 1, and is
accepted by all three exact solvers. It limits oneTBB arena concurrency, including the caller;
workload size and available TBB resources may reduce actual concurrency.

Preparation checks the root bound first. A root infeasibility proof skips both
heuristics. Otherwise greedy supplies a cheap initial incumbent; if its cost
matches the root lower bound, the optimum is already proved. Reduction runs
only when a better incumbent can still matter, avoiding unnecessary cubic R2
work and fill allocations on trivially solved inputs. Both root proofs retain
a final completion notification, reported after successful preparation.

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

For the pure interference subset of register-allocation inputs, B&B verifies
a sufficient condition for register symmetry directly from the input: nonzero options have zero unary costs, spill rows/columns and
unequal-register pairs are zero, and equal-register pairs are INF. Only after
all nodes and matrices pass this test are new registers introduced in ascending
order. Every discarded naming has a retained renaming with the same reference
terms, so this removes redundant subtrees without changing the optimum. General
PBQP inputs use the unrestricted search. Coalescing may already change these
matrices in the original input, and R2 need not preserve this shape. Failing
the pure-interference test does not imply absence of register-label symmetry.
The reduction seed works on its own graph copy; its matrices never replace
the original problem used by B&B or its cached structural checks.

Preparation targets `max(32, 2 * available_workers)` coarse jobs and stops after
512 conditioned-bound checks or 25 ms, whichever comes first. A running bound
may exceed the deadline. Both frontier expansion and queued-job selection prefer
lower conditioned bounds, with insertion order breaking ties. Domains determine
prefix depth; unchecked siblings become range jobs on budget exhaustion. Forced
single-option runs are appended in place and checked once. Closed planning
prefixes remain available as preparation statistics; they do not advance the
B&B heartbeat.

Workers keep private resumable DFS state and run chunks of at most 512 bound
checks. If another consumer is waiting, a worker can donate an unexplored sibling
range and lower its own cursor limit to exclude that range. The current path
stays private. Thus whole subtrees, never individual DFS vertices, enter the
bounded queue. Queue slots and prefix buffers are allocated during preparation;
its mutex publishes jobs, while an atomic waiter count is only a donation hint.
Progress counts closed prefixes in the disjoint remaining ranges.

oneTBB runs fixed consumers synchronously. The requested thread count is a
limit: TBB's available concurrency and any application-wide TBB limit can reduce
the allocated worker count. One worker executes directly. Scheduler failure
cancels the work queue and wakes waiting consumers before joining; no callback
outlives the C API call. Allocation failures become NO_MEMORY and other standard
runtime/scheduler exceptions become INTERNAL_ERROR at the shared API boundary.
Assignments, DFS cursors, scratch, and local best storage remain private to each
worker. Returned solution storage remains independent of the input.

The final winner is checked and evaluated against the original problem. Success
returns OPTIMAL; exhausting the remaining space without a feasible incumbent
proves INFEASIBLE. Worst-case search remains exponential, and OPTIMAL refers
to sequential float evaluation rather than an exact real sum.

Example 06 has a feasible zero-cost assignment and nonnegative input terms, so
its optimum is zero. It is available as an optional Google Benchmark case for
1/2/4/8 threads, rather than an exhaustive routine CTest workload. Timing includes
prefix planning and the resulting search; changes in planning and incumbent
arrival can change the amount of work as well as how many CPUs execute it.

Planning and incumbent arrival can change the amount of work as well as its
parallel distribution. The [optimization guide](bnbopts.md) explains the shared
budgeted scheduling and the additional bounds specific to `bnbopt`. Serial warm
search was tested but did not improve total time and is not enabled.

Both examples show the benefit of exact search over the heuristics. Example 07
also has enough search work to demonstrate parallel speedup; its benchmark
compares 1/4/16 threads with heartbeat enabled and disabled.

| Example | Greedy | R1/R2/RN | Exact optimum |
| --- | ---: | ---: | ---: |
| [06_exact_beats_heuristics.pbqp](../examples/pbqp/06_exact_beats_heuristics.pbqp) | 22.1286182 | 22.1286182 | 0 |
| [07_parallel_exact_beats_heuristics.pbqp](../examples/pbqp/07_parallel_exact_beats_heuristics.pbqp) | 91.4585114 | 54.0987892 | 10.1112299 |

## Optimized branch-and-bound

```sh
pbqp --solve=bnbopt --numthreads=4 --heartbeat \
  examples/pbqp/08_too_long_for_simple_bnb.pbqp
```

`bnbopt` is a separate exact solver with the same C contracts, any-optimum tie
policy, thread limit, and approximate search-tree heartbeat as `bnb`. The baseline retains
its original ID order and independent-term bounds for comparison. Both strategies
share the worker DFS, incumbent, prefix planner, TBB dispatcher, and progress
mechanisms through one bounds-strategy template; naive's different winner
semantics remain separate.

The optimized path requires actual verification of the uniform regalloc shape:
all nodes have the same domain, every register unary is zero, spill rows and
columns are zero, register diagonals are INF, and other pairs are zero. Spill
costs may be negative or INF. Dynamic word-vector bitsets represent nodes and
registers without a machine-word count limit. Dense graph and clique-search
storage has a 64 MiB preparation budget; inputs exceeding this budget and
non-regalloc inputs use ordinary B&B bounds and ID order.
No generator metadata is trusted, and no external MILP library is used.

Preparation occurs after cheap root proofs and heuristic seeding. It constructs
an interference graph, collects greedy cliques, and spends at most 100,000
extensions refining a large clique with iterative search. Up to 32 valid cliques
are retained. Candidate ranking also has a 64-million-word work budget, and
greedy discovery uses at most 64 starting vertices. These limits bound
preparation effort, not input dimensions. For large graphs whose adaptive
ordering would exceed 64 million comparisons, the tail uses a static order.
Finding a maximum is not required for correctness: stopping early
can only weaken the bound. The selected clique goes first in the fixed search
order; remaining nodes prefer connections to the chosen prefix, degree, spill
cost, then ID. Register names are canonicalized by first appearance in this
search order. Preallocated dynamic bitsets check conflicts and allowed registers against assigned
neighbors in either edge orientation.

Assignments remain indexed by original node ID. Prefix ranks and DFS cursors
are indexed by search position. Separate ordered domain views provide the radix
sizes for prefix jobs and progress; they never replace the original evaluator's
node and edge order.

### Clique spill-count bounds

For a clique, let `U` be its unassigned nodes and `A_v` the allowed registers of
node `v` after forward checking. At most `|union A_v|` nodes of `U` can stay in
registers, so at least

```text
q = max(0, |U| - |union A_v|)
```

must spill. An unavailable spill remains forbidden. A clique whose remaining
nodes cannot provide `q` allowed spills proves that prefix infeasible.

The numerical bound is a DP over the number of spills in `U`, capped at `q`.
It processes *all original nodes in ID order*. Outside `U`, it adds the fixed
or independent minimum unary term. Inside `U`, it can add register cost zero
when a register is allowed, or the finite spill cost while increasing the
spill count. Each transition uses `pbqp_cost_add`; the cheapest rounded prefix
score is kept for each state. Reachability is stored separately, because a
reachable score may overflow to positive or negative infinity.

Every feasible completion induces a DP path with at least `q` spills. Independent
outside terms bound its unary terms, and rounded addition is monotone, so the
DP is a valid reference-order lower bound. Keeping the cheapest prefix per state
is valid for the same reason. All feasible edge terms in this verified shape
are zero. Bounds from different cliques are combined by maximum, never by adding
overlapping costs. There is no regrouped real-sum or double-precision pruning
estimate. The final solution still uses the shared original-order evaluator.

### Global register-capacity bounds

`IndependentSets` in `core` enumerates maximal independent sets of a simple
undirected graph, independently of PBQP or register allocation. All-subset
enumeration exists only as a bounded test reference, not a production mode.
Capacity uses maximal sets by inclusion, not just maximum-cardinality sets.
Every fixed independent class and extension lies inside a maximal set; filtering
that set by allowed pending nodes preserves the extension. With nonnegative
capacity weights, the maximum is unchanged. Original signed spill costs are
handled separately by the cost DP. Preparation is
iterative, with a 16 MiB auxiliary-storage budget, at most 10,000 sets, and an
8-million-word work budget. Exhaustion discards partial results. Only a complete
enumeration can upper-bound register capacity; finding some sets is insufficient.

For verified regalloc inputs, each register can hold one independent set. Given
a fixed prefix, maximize the number of allowed unassigned vertices extending
its assigned color class, independently for each register. The sum overestimates
how many vertices can stay in registers, so the remainder must spill. The same
original-order DP enforces this global spill-count constraint.

For finite nonnegative spill costs, bounded projected accelerated gradient may
also choose auxiliary integer weights. Double arithmetic selects these weights
only; it never supplies a pruning score. For each register, the actual maximum
remaining auxiliary weight is recomputed over the complete maximal catalogue. Any weight
not accommodated by those capacities must spill. A capped-weight DP enforces
this condition while adding original float costs in original node order, with
reachability separate from infinite scores.

Pricing uses four temperatures and at most 2,000 iterations, with a bounded
pattern-vertex visit estimate. Nonpositive heuristic scores or unsupported costs
disable pricing without affecting feasibility. Quantization and scaling bound
total auxiliary weight to 32,768 and the worst-case weighted DP scan to roughly
four million state slots. Round auxiliary weights to nearest integers, reserving
half a unit per node when scaling to fit the budget; check the final sum. Integer sums are checked against those budgets before
conversion or allocation. Negative/INF spill costs retain valid unweighted and
clique bounds. Cheap root/seed and structural proofs precede the extra work.
No external LP library is required.

The sole dedicated C entry point is `pbqp_solve_branch_and_bound_optimized`,
accepting `pbqp_solve_options`, or NULL for defaults.
Optional `BranchAndBoundOpt/Example06`, `/Example07`, `/Example08`, and `/Example09` Google
Benchmark cases compare thread counts and heartbeat overhead. The [optimization guide](bnbopts.md) lists the additional assumptions, bounds,
preparation budgets, and fallbacks, with reproducible benchmark commands.

The optimized solver also builds a bounded independent-set coverage seed and
refines auxiliary prices for the current prefix in private worker storage.
Root weights remain available, and all pruning still uses integer capacities
with reference-order float DP. See [sections 6 and 7 of the optimization
guide](bnbopts.md#6-seed-from-independent-set-coverage) for eligibility, budgets,
and the conditional dual. Baseline `bnb` does not use these structural paths.

## Verbose diagnostics

`--verbose` is a valueless flag for every solver. It writes the solver name,
input dimensions, requested thread limit, heartbeat setting, and returned result
quality to stderr. The normal COST/NODE records on stdout are unchanged.

For `bnbopt`, it also reports cached eligibility and preparation decisions:
input-shape mismatches with node/edge entries, graph and clique budgets,
retained witnesses, independent
set completion or discarded partial results, weighted-pricing eligibility,
iteration/visit/state budgets, weight scaling, and planned/prepared workers.
An early root proof explicitly explains why later preparation was unnecessary.
A limit on one component does not disable unrelated valid optimizations.

Before DFS, both `bnb` and `bnbopt` print `search: preliminary bounds: lower=...;
upper=...`. The lower estimate is the minimum cached bound of remaining prefix
jobs, capped by the incumbent cost; the upper estimate is the cost of an actual
feasible assignment from seeding or planning. Parent bounds for unchecked ranges
can make the estimate conservative. Closed prefixes are infeasible, exhausted,
or no better than the current incumbent. With no remaining jobs, a feasible
incumbent proves equality of the bounds; without one, preparation has proved
infeasibility. If work remains but no assignment is known, `upper=none (no
feasible solution yet)` is distinct from a feasible overflowed `upper=inf`.
The message precedes all DFS/progress callbacks and uses reference-order float
costs, not the pricing stage's real-arithmetic objective or a time estimate.

Library callers can request the same `bnbopt` messages through
`pbqp_solve_branch_and_bound_optimized`, using the `diagnostics` and
`diagnostic_data` fields of `pbqp_solve_options`. The synchronous C callback
receives a borrowed NUL-terminated message without a newline, outside DFS. Diagnostic and progress contexts are independent; messages are not a
stable machine-readable format. No diagnostic callback or terminal output is
added when diagnostics are disabled.

## Greedy heuristic

`--solve=greedy` assigns nodes with the largest finite unary cost range first,
breaking ties by ID. It chooses the first cheapest allowed option using costs
to already assigned nodes. This is cheap and may be suboptimal. A successful
result is FEASIBLE; a dead end is NO_SOLUTION, not proof of INFEASIBLE.

## R1/R2/RN reduction heuristic

```sh
pbqp --solve=greedy examples/pbqp/07_parallel_exact_beats_heuristics.pbqp
pbqp --solve=r1r2rn examples/pbqp/07_parallel_exact_beats_heuristics.pbqp
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

Example 07 runs through both heuristics in CTest, without starting exact
search. Neither heuristic is guaranteed to dominate the other on all inputs.

## Heartbeat

`--heartbeat` is supported by `naive`, `bnb`, and `bnbopt`. It writes at most
100 flushed dots to stderr, with a final newline. Solution records remain on
stdout. No dot is emitted at zero progress. Percentages describe search progress,
not elapsed time or an ETA.

### Naive: exact tuple counts

Naive counts every visited tuple, including forbidden assignments. Integer
percentage `p` is reached after `ceil(total * p / 100)` visits. Parallel workers
accumulate mixed-radix covered-space counts privately and merge at job completion,
after 256 small closures, or immediately for a one-percent-sized closure.
This preserves exact thresholds even when the product of domain sizes exceeds
machine integers. Batched notifications may lag visits but cannot precede their
thresholds. The single-thread path keeps its direct lexicographic counter.

### B&B: an estimate over surviving branches

The original Cartesian space is a poor denominator for B&B. For example,
register-label symmetry can immediately discard most of that space, while the
remaining canonical assignments still require a long proof. A strong bound can
have the same effect. Counting those cuts as completed tuples makes the bar
almost full before the difficult work begins.

With heartbeat enabled, each worker performs one-level lookahead before
entering a node's children. It checks each child's bound and keeps only children
which are feasible under the constraints and can still improve the incumbent.
Infeasible children, noncanonical register names, and immediate `L >= G` cuts
receive no weight. The node's weight is divided equally among surviving children,
rather than among every original option. If no child survives, the searched node
closes with its whole weight. Child bounds are cached: entering a surviving child
rechecks the latest incumbent, without evaluating that bound again.

The surviving initial frontier jobs start with equal weights; prefixes already
closed during planning contribute nothing. An initial or donated job rejected
on arrival (including a range with no surviving children) is removed from the
denominator, as is a cached child closed on entry by a newer incumbent.
Donation transfers the unexplored siblings' weights to the receiving
worker and removes those siblings from the donor. Completed weights are merged
in batches of 256 closures or immediately for a weight of at least 0.01.

These are **approximate percentages**, not a count of all DFS nodes or a promise
that equal percentages take equal time. Children and frontier jobs may contain
very different amounts of work. New incumbents can close branches which survived
an earlier check. Percentages are monotone but can pause or jump; rounding and
very small weights can also defer notifications. The final dot is reserved until
all workers have joined. Empty searches and root proofs still report 1 through
100 after successful preparation.

Lookahead has private preallocated child-bound caches and is resumable within
the usual DFS quantum. It introduces no search-loop allocations. Without a
progress callback, B&B uses the ordinary DFS without these caches or lookahead;
naive's algorithm and exact progress are unchanged.

All callbacks are serialized and may run on TBB workers. Worker tails are flushed
before return. No callback outlives the synchronous solver call. An infeasibility
proof reports completion too; errors before search emit no progress.
