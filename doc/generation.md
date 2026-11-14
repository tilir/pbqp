# Generator modes

`pbqp_generate` writes a PBQP 2 instance with exactly the requested node and
edge counts. All options accept `--name=value` or `--name value`. A fixed seed and sampling protocol
reproduce task data across supported standard libraries. Unknown or mode-inapplicable option
tokens warn and are ignored; `-Werror` makes them errors before writing output.
Unknown tokens do not consume a following value; keep one attached with `=`.

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
is forbidden. This is the cost structure used by `examples/pbqp/02_greedy_exact.pbqp` and
`examples/pbqp/03_greedy_inexact.pbqp`. Assigning
spill to every node is always allowed, and the greedy solver always finds an
assignment in this family. Greedy may spill expensive nodes unnecessarily;
the exhaustive solver is suitable for comparisons on small instances only.
Greedy prioritizes larger unary cost ranges, which in this mode means larger
spill costs first. Equal priorities use node ID order.

## Graph sampling

Edges are undirected, distinct, and written in endpoint order. The generator
samples pair ranks without replacement using Floyd sampling, selecting the
complement for dense graphs. Sparse generation does not scan absent edges;
mapping selected ranks to endpoints takes O(M log N) time. Random/planted
matrices require the full product of endpoint option counts in memory. Regalloc
stores a single shared matrix in memory and in the PBQP 2 table; its sparse
encoding has default zero and one INF entry per register.


## Reproducibility and provenance

`pbqp_generate --version` prints the project release plus a SHA-256 fingerprint
of the generation, sampling, argument-handling, and serialization sources. The
fingerprint follows source edits, including uncommitted ones, through CMake's
configure dependencies. It does not include the compiler, standard library,
checkout path, build timestamp, or test-only sources. Matching fingerprints are
conservative: even a comment-only source edit changes the identity.

Every generated file ends with comments like:

```text
END
# generator: pbqp_generate 2.0.0+<source-fingerprint>
# random: mt19937_64; sampling protocol 1
# command: ./build/pbqp_generate --mode=regalloc --nodes=10 --edges=30 --registers=4 --seed=42 --output=sample.pbqp
```

The command records actual arguments, including the executable path. Unsafe
arguments use Bash ANSI-C quoting; control characters use three-digit octal
escapes, keeping the command on one physical comment line. Copy the text after
`# command: ` into Bash to replay it. Argument order, option spelling, and output
paths affect provenance text but not task data. Other `pbqp_write` calls do not
retain these comments.

For the same protocol and effective parameters, fresh generators produce the
same endpoints and binary32 costs across standard libraries on supported
IEEE binary32/binary64 platforms with round-to-nearest arithmetic and without
fast-math. Platform size limits still apply. Text is locale-independent and
round-trips all costs; the semantic guarantee concerns node domains, costs, and
stored edge order. Regression fixtures pin those values by a numeric
fingerprint independently of text formatting. Existing checked-in examples were
converted losslessly, not regenerated under a guessed seed or generator version.

### Sampling protocol 1

The engine is the standard [`std::mt19937_64`](https://eel.is/c++draft/rand.predef),
initialized directly from the unsigned 64-bit seed. Its stream is standardized;
standard-library distributions specify probability laws rather than a portable
sampling algorithm ([distribution requirements](https://eel.is/c++draft/rand.req.dist)).
PBQP therefore consumes engine outputs directly through two fixed conversions.

**Inclusive integer U(m).** Let `s = m + 1` in unsigned 64-bit arithmetic.
If `s == 0`, return one raw engine output. Otherwise let
`t = (0 - s) % s`, again unsigned. Draw until `x >= t`, then return `x % s`.
The accepted count is a multiple of `s`, removing modulo bias. Even `U(0)`
consumes one draw. No standard-library integer distribution is used.

**Spill cost in [a, b].** Always consume one raw output `x`, including when
`a == b`. Take its high 24 bits, `q = x >> 40`, and form the exact binary64
fraction `u = q * 2^-24`. Compute binary64 `w = double(b) - double(a)`, then
`float(std::fma(w, u, double(a)))`. The explicit correctly rounded FMA prevents
compiler contraction choices from changing the result; the final binary32
rounding is intentional. A rounded result can equal either endpoint. No
standard-library real distribution is used.

Draw order is part of the protocol:

1. Sample endpoints first using Floyd's algorithm. For `P=N*(N-1)/2`, use
   complement sampling exactly when `M > P/2`, with `K=min(M,P-M)` selected
   ranks. Visit `j=P-K,...,P-1`; draw `v=U(j)` and insert `j` if `v` was already
   selected, otherwise insert `v`. Sort selected ranks, or enumerate their
   complement. Ranks correspond to lexicographic `(src,tgt)` pairs.
2. Process nodes in ID order. Random/planted domains use `1+U(D-1)`. For each
   unary entry draw `U(9)`; zero produces INF with no further draw, otherwise
   draw `(int(U(80))-40)/4`. Planted mode then chooses `U(d-1)` and replaces
   that unary entry with a fresh `(int(U(80))-40)/4`. Regalloc instead draws
   one spill cost and assigns zero register costs, without a domain draw.
3. Process edges in endpoint order, costs in row-major order. Random/planted
   entries use the same INF/quarter-step rule. Planted mode replaces the hidden
   pair with zero after generating the full matrix, without another draw.
   Regalloc matrices consume no draws.

`generate_problem(options)` starts a fresh engine from `options.seed`.
`ProblemGenerator(seed).generate(options)` continues its owned engine stream
across calls; it does not reseed from `options.seed`. Changing a conversion or
its draw order requires a new sampling protocol and updated golden vectors.
