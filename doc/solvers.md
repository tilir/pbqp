# Solvers

Select a solver with `pbqp --solve=NAME INPUT`; `pbqp --help` lists the names.
All solvers report costs in the reference evaluation order: unary terms in
node ID order, then pairwise terms in stored edge order, using ordinary float
addition. Input validation and solution ownership rules are in `include/pbqp.h`.

## Naive exhaustive search

`--solve=naive` enumerates every tuple in lexicographic order. It retains the
first minimum and returns OPTIMAL, or proves INFEASIBLE after exhausting the
space. The number of tuples is the product of the node option counts.

## Branch-and-bound

```sh
pbqp --solve=bnb --heartbeat examples/05_too_long_for_naive.pbqp
```

`bnb` is exact under the same float semantics and uses the same lexicographic
tie-breaking as `naive`. It performs iterative depth-first search over node ID
prefixes, without recursion or allocation during search. Before branching,
it runs greedy and uses the reference-order cost of a feasible result as an
initial upper bound G. A greedy dead end leaves G at positive infinity and
exact search continues. Better searched assignments lower G.

Forbidden prefixes are always pruned. Before exact search reaches its first
feasible leaf, cost pruning uses `L > G`, where L is the prefix's lower bound.
This lets search find an earlier tied optimum than greedy's assignment.
After that first feasible leaf, pruning uses `L >= G`: subsequent equal-cost
tuples cannot precede the incumbent. Equal-cost subtrees then close without
full enumeration. An infinite bound also follows these rules; accumulated
infinity alone does not prove infeasibility.

For a fixed prefix, each bound term is:

- A node's chosen unary cost if assigned, otherwise its minimum unary cost.
- An edge's selected matrix entry if both endpoints are assigned.
- The minimum of the selected source row if only the source is assigned.
- The whole matrix minimum if neither endpoint is assigned.

Because `src < tgt` and nodes are assigned in ID order, a target is never
assigned before its source. Unary, matrix, and row minima are cached before
search. An INF term with no finite alternative proves that prefix infeasible.

The lower-bound terms are added in exactly the reference order. Every term
is no greater than the corresponding term in any feasible completion;
monotonic rounded addition preserves this inequality. This matters for
negative costs, cancellation, and arithmetic overflow. Summing a suffix
separately and adding it to a prefix would regroup float arithmetic and could
produce an unsafe bound. An infinite accumulated sum is kept separate from
an explicit forbidden term.

Success returns OPTIMAL; failure to find any assignment after completing all
remaining prefixes proves INFEASIBLE. Reported winner costs are recomputed
with the shared reference evaluator. This is not a promise of optimality under
the exact real sum, and worst-case search remains exponential.

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

Naive counts visited tuples. Branch-and-bound counts all tuples covered by
completely searched or pruned prefix subtrees. A cut subtree may therefore
advance several dots immediately. This measures search-space coverage, not
elapsed time or an ETA. Percentages remain valid when the product of domain
sizes exceeds fixed-width integer ranges. Even an infeasibility proof reports
complete progress; errors before search do not report progress.
