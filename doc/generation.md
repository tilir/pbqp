# Generator modes

`pbqp_generate` writes a PBQP 1 instance with exactly the requested node and
edge counts. All options accept `--name=value` or `--name value`. A fixed seed
reproduces output with the same build and standard library.

`--nodes`, `--edges`, `--seed`, and `--output` are required in every mode.
`--mode=random` is the default. Use `--help` for the complete argument list.

## Unrestricted random costs

```sh
pbqp_generate --mode=random --nodes=5 --edges=7 --max-options=3 --seed=42 --output=random.pbqp
```

Each node has 1 through `--max-options` options. Unary and pairwise costs have
a 10% chance of INF; finite costs are quarter steps in [-10, 10]. Instances
may be infeasible, particularly when they contain many nodes.

## Planted feasible assignment

```sh
pbqp_generate --mode=planted --nodes=5 --edges=7 --max-options=3 --seed=42 --output=planted.pbqp
```

Starts with the same cost family, chooses a hidden option for each node, and
makes its unary and pairwise entries finite. The resulting instance is
guaranteed feasible. Neither a known optimum nor success of greedy search is
guaranteed.

## Register conflicts and spill

```sh
pbqp_generate --mode=regalloc --nodes=1000 --edges=3000 --registers=16 --seed=42 --output=regalloc.pbqp
```

Every node has `--registers + 1` options. Option 0 is spill, with a positive
individual unary cost sampled uniformly between `--spill-cost-min` and
`--spill-cost-max` (defaults 10 and 100). Bounds must be finite and satisfy
`0 < min <= max`. Equal bounds give a fixed spill cost. All register unary
costs are zero. `--registers` is required; `--max-options` is not used here.

Every edge has a square conflict matrix. For three registers it is:

```text
0 0   0   0
0 INF 0   0
0 0   INF 0
0 0   0   INF
```

Spill's row and column are zero; choosing the same register at both endpoints
is forbidden. This is the cost structure used by `examples/02_greedy_exact.pbqp` and
`examples/03_greedy_inexact.pbqp`. Assigning
spill to every node is always allowed, and the greedy solver always finds an
assignment in this family. Greedy may spill expensive nodes unnecessarily;
the exhaustive solver is suitable for comparisons on small instances only.
Greedy prioritizes larger unary cost ranges, which in this mode means larger
spill costs first. Equal priorities use node ID order.

## Graph sampling

Edges are undirected, distinct, and written in endpoint order. The generator
samples pair ranks without replacement using Floyd sampling, selecting the
complement for dense graphs. Sparse generation does not scan absent edges;
mapping selected ranks to endpoints takes O(M log N) time. Matrix storage
still requires the full product of the endpoint option counts for each edge.
