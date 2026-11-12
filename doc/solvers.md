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
