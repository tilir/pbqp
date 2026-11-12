/*
 * PBQP: a C API for Partitioned Boolean Quadratic Programming problems.
 *
 * A solution chooses one option per node and minimizes the sum of node costs
 * and edge matrix entries. Missing edges contribute zero; INF forbids a node
 * option or a pair. This header defines the in-memory format, min-plus
 * cost algebra, text I/O, ownership helpers, and an exhaustive reference
 * solver. The implementation uses C++23; all public declarations are
 * C-compatible. See doc/format.md for the PBQP 1 text format.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of version 3 of the GNU General Public License as published
 * by the Free Software Foundation.
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE for the full license text.
 */
#pragma once

#include <math.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Finite costs use float; positive infinity represents a forbidden choice.
 * Input costs must not be NaN or negative infinity. */
typedef float pbqp_cost_t;
#ifdef __cplusplus
inline constexpr pbqp_cost_t PBQP_INF = INFINITY;
#else
static const pbqp_cost_t PBQP_INF = INFINITY;
#endif

typedef struct pbqp_node {
  size_t num_options;
  const pbqp_cost_t *costs;
} pbqp_node;

typedef struct pbqp_edge {
  /* Undirected edge, stored with src < tgt. Matrix rows are options of
   * src; columns are options of tgt. */
  size_t src;
  size_t tgt;
  /* Row-major: costs[a * nodes[tgt].num_options + b]. */
  const pbqp_cost_t *costs;
} pbqp_edge;

/* May describe caller-owned arrays without transferring their ownership.
 * Objects returned by pbqp_read instead own their arrays and must be treated
 * as read-only: do not replace any pointers or change counts or dimensions,
 * including those in node and edge records. The object is a view into storage
 * managed by pbqp_problem_free. */
typedef struct pbqp_problem {
  size_t num_nodes;
  size_t num_edges;
  /* Node IDs are array indices, starting at zero. */
  const pbqp_node *nodes;
  const pbqp_edge *edges;
} pbqp_problem;

typedef enum pbqp_status {
  /* Successful execution; solver result quality is reported separately. */
  PBQP_OK = 0,
  PBQP_INVALID,
  PBQP_IO_ERROR,
  PBQP_NO_MEMORY,
  PBQP_OVERFLOW,
  /* Proven absence of any feasible assignment. Failure to find an
   * assignment without a proof must never be reported as this status. */
  PBQP_INFEASIBLE,
  /* Search ended without an assignment or a proof of infeasibility. */
  PBQP_NO_SOLUTION,
  /* Runtime or scheduler failure; no solution is returned. */
  PBQP_INTERNAL_ERROR
} pbqp_status;

/* Ordinary floating-point addition, with the platform's rounding and
 * overflow behavior. No checking or saturation is performed. */
static inline pbqp_cost_t pbqp_cost_add(pbqp_cost_t a, pbqp_cost_t b) {
  return a + b;
}

static inline pbqp_cost_t pbqp_cost_min(pbqp_cost_t a, pbqp_cost_t b) {
  return a < b ? a : b;
}

/* Optional diagnostic output; no initialization is required before a call.
 * Every public function accepting a non-NULL error pointer leaves message
 * NUL-terminated on return, truncating diagnostic text if necessary.
 * On PBQP_OK, line is zero and message[0] is '\0'. Remaining message bytes
 * are unspecified. On failure, message contains a diagnostic and line is
 * the input line when known, or zero otherwise. */
typedef struct pbqp_error {
  /* Input line, or zero for errors unrelated to an input token. */
  size_t line;
  char message[192];
} pbqp_error;

typedef enum pbqp_solution_quality {
  /* A feasible assignment, without a proof of optimality. */
  PBQP_SOLUTION_FEASIBLE = 0,
  /* Proven optimal under the reference sequential float evaluation order,
   * not necessarily under exact real arithmetic. */
  PBQP_SOLUTION_OPTIMAL
} pbqp_solution_quality;

/* A solver-returned solution owns its options array. Neither the solution
 * nor that array borrows storage from the input problem; both remain valid
 * after the problem is destroyed. Do not replace the options pointer. */
typedef struct pbqp_solution {
  pbqp_cost_t cost;
  pbqp_solution_quality quality;
  size_t num_nodes;
  size_t *options;
} pbqp_solution;

typedef struct pbqp_evaluation {
  pbqp_cost_t cost;
  /* Zero for an assignment selecting an explicit INF term; otherwise one.
   * A feasible assignment can still have an infinite accumulated cost. */
  int is_feasible;
} pbqp_evaluation;

/* Reads a complete file at the null-terminated path. Only whitespace/comments
 * may follow END. On success, transfers ownership of the problem and all its
 * arrays to the caller; release them using pbqp_problem_free. Treat the
 * returned object and its arrays as read-only, preserving pointers and sizes.
 * Sets *out to NULL on failure. error may be NULL. */
pbqp_status pbqp_read(const char *path, pbqp_problem **out, pbqp_error *error);

/* Borrows the problem and its arrays for this call without modifying them.
 * Validates the input using the pbqp_validate contract before opening the
 * output file; callers do not need to validate it separately.
 * Writes to the null-terminated path, closing the file before returning and
 * reporting any output failure. error may be NULL. */
pbqp_status pbqp_write(const char *path, const pbqp_problem *problem,
                       pbqp_error *error);

/* Checks structure and cost values without modifying the problem:
 * - problem must not be NULL.
 * - nodes must not be NULL when num_nodes is positive; edges must not be
 *   NULL when num_edges is positive. For a zero count, the corresponding
 *   pointer may be NULL or non-NULL and is not dereferenced.
 * - Each node must have positive num_options and a non-NULL costs pointer.
 * - Every edge must satisfy src < tgt < num_nodes. Duplicate endpoint pairs
 *   are forbidden; edge records may appear in any order.
 * - Every edge must have a non-NULL costs pointer for its row-major matrix
 *   of nodes[src].num_options * nodes[tgt].num_options entries.
 * - All costs must be finite or PBQP_INF; NaN and negative infinity are
 * invalid.
 * - Declared array sizes, matrix products, and byte sizes must fit in size_t.
 *
 * The caller must supply live, readable arrays with at least the declared
 * lengths, including every cost vector and matrix. Pointer validity and
 * actual allocation lengths cannot be checked by this function; validation
 * may read every declared entry and does not make undersized arrays safe.
 *
 * Returns PBQP_OK for valid input, PBQP_INVALID for invalid structure or
 * costs, PBQP_OVERFLOW for unrepresentable sizes, or PBQP_NO_MEMORY if
 * validation cannot allocate temporary storage. error may be NULL.
 * pbqp_write and public solver entry points validate input themselves using
 * this contract; a separate validation call is optional. */
pbqp_status pbqp_validate(const pbqp_problem *problem, pbqp_error *error);

/* Validates the problem and option indices, borrowing all inputs without
 * modification. num_nodes must match problem->num_nodes; options may be NULL
 * only for an empty problem. The caller supplies at least num_nodes entries.
 * out must not be NULL and is cleared on failure. error may be NULL.
 * PBQP_OK means evaluation succeeded, including forbidden assignments:
 * is_feasible is then zero and cost is PBQP_INF. It does not prove that the
 * problem is infeasible. An allowed assignment whose sum overflows to infinity
 * instead has is_feasible == 1. Never returns PBQP_INFEASIBLE.
 * Reference evaluation adds all unary costs in node ID order, then pairwise
 * costs in stored edge order, using pbqp_cost_add. OPTIMAL is defined relative
 * to these sequentially rounded values. Regrouping arithmetic can change the
 * ordering of assignments and cannot by itself establish this quality. */
pbqp_status pbqp_evaluate_assignment(const pbqp_problem *problem,
                                     const size_t *options, size_t num_nodes,
                                     pbqp_evaluation *out, pbqp_error *error);

/* Releases a problem and all its arrays. Only accepts NULL or an unmodified
 * object returned by pbqp_read. Never pass a caller-owned problem view. */
void pbqp_problem_free(pbqp_problem *problem);

/* Releases a solution and its owned options array. Only accepts NULL or an
 * object returned by a public solver with its original options pointer.
 */
void pbqp_solution_free(pbqp_solution *solution);

/* Enumerates tuples in lexicographic order. Ties keep the first tuple.
 * Validates input using the pbqp_validate contract before enumeration;
 * callers do not need to validate it separately.
 * Borrows the problem and all its arrays for the duration of the call without
 * modifying them or retaining any pointers to them. The caller must keep
 * these inputs alive and unchanged until the call returns.
 * The returned solution owns an independent options array and remains valid
 * after the problem is destroyed; release it using pbqp_solution_free.
 * Sets *out to NULL on failure. error may be NULL.
 * On PBQP_OK, *out contains a PBQP_SOLUTION_OPTIMAL solution.
 * PBQP_INFEASIBLE means all tuples were exhausted without a feasible one.
 * An empty problem has cost zero. INF terms make a tuple infeasible.
 * Adds node costs in ID order, then edge costs in stored order, using
 * pbqp_cost_add with ordinary floating-point rounding and overflow.
 * No pruning assumptions are made about negative costs. */
pbqp_status pbqp_solve_bruteforce(const pbqp_problem *problem,
                                  pbqp_solution **out, pbqp_error *error);

/* Called synchronously after each completed integer percentage, from 1
 * through 100, at most once per percentage. Several percentages may complete
 * on one tuple or pruned subtree. user_data is borrowed for the call.
 * Notifications are serialized and may run on TBB workers during a parallel
 * call. Workers may merge counts in batches: notifications can lag completed
 * work, but never precede their exact thresholds. All completed percentages
 * are reported before return. A callback must not throw or modify the problem
 * or its arrays. */
typedef void (*pbqp_progress_callback)(unsigned percent, void *user_data);

/* Initialize all fields, e.g. {1, NULL, NULL}. num_threads must be positive
 * and fit in int, the oneTBB arena's concurrency type. The limit includes the
 * calling thread. Task count or available TBB resources may reduce concurrency.
 * Options and user_data are borrowed only during the synchronous call. */
typedef struct pbqp_solve_options {
  size_t num_threads;
  pbqp_progress_callback progress;
  void *user_data;
} pbqp_solve_options;

/* Same solver and ownership contract as pbqp_solve_bruteforce, with optional
 * progress reporting. A NULL callback disables reporting. Percentages count
 * all enumerated tuples, including forbidden ones; threshold p is reached
 * after ceil(p * product(num_options) / 100) completed tuples. The first
 * callback is never sent at zero progress. Full search reports 100 even when
 * it proves infeasibility. Errors before enumeration invoke no callbacks.
 * Reporting does not change the tuple order, evaluation policy, or quality. */
pbqp_status pbqp_solve_bruteforce_with_progress(const pbqp_problem *problem,
                                                pbqp_progress_callback progress,
                                                void *user_data,
                                                pbqp_solution **out,
                                                pbqp_error *error);

/* Parallel exhaustive search. NULL options use one thread and no callback.
 * Every tuple is evaluated, including forbidden ones. Workers share immutable
 * input only; private minima are merged by cost, then lexicographic assignment,
 * preserving the same first minimum as single-thread naive search. Progress
 * reports exact tuple counts with serialized callbacks, potentially on TBB
 * workers. The call joins all work before returning an independent solution.
 * Runtime/scheduler failures are PBQP_INTERNAL_ERROR. */
pbqp_status
pbqp_solve_bruteforce_with_options(const pbqp_problem *problem,
                                   const pbqp_solve_options *options,
                                   pbqp_solution **out, pbqp_error *error);

/* Exact branch-and-bound with the same validation, ownership, evaluation,
 * output, and diagnostic contracts as pbqp_solve_bruteforce. Returns any
 * optimum under reference-order float evaluation; tied assignments need not
 * match naive search and can depend on thread scheduling. Greedy and reduction
 * seed feasible incumbents. Bounds condition options on the fixed prefix and
 * sum independent term minima in reference order, without regrouping floats.
 * Prunes L >= G whenever a feasible incumbent exists. On PBQP_OK quality is
 * PBQP_SOLUTION_OPTIMAL; PBQP_INFEASIBLE requires an exhaustive proof.
 * This entry point uses one thread. */
pbqp_status pbqp_solve_branch_and_bound(const pbqp_problem *problem,
                                        pbqp_solution **out, pbqp_error *error);

/* B&B reports exact covered-space percentages, counting tuples in searched or
 * pruned subtrees. Completed subtrees may arrive out of order. Notifications
 * are serialized, monotone 1..100, and may run on a TBB worker thread while the
 * solver call is active. The callback must not throw or mutate input. NULL
 * disables reporting. Before-search errors report nothing; successful search
 * and infeasibility proofs report 100. This entry point uses one thread. */
pbqp_status pbqp_solve_branch_and_bound_with_progress(
    const pbqp_problem *problem, pbqp_progress_callback progress,
    void *user_data, pbqp_solution **out, pbqp_error *error);

/* NULL options select one thread and no progress. Other contracts match the
 * B&B entry points above. All workspaces are private to workers; only immutable
 * input/minima, a synchronized incumbent, and covered-space reporting are
 * shared. No callback outlives the call. Runtime/scheduler exceptions become
 * PBQP_INTERNAL_ERROR; allocation failures remain PBQP_NO_MEMORY. */
pbqp_status pbqp_solve_branch_and_bound_with_options(
    const pbqp_problem *problem, const pbqp_solve_options *options,
    pbqp_solution **out, pbqp_error *error);

/* Validates and borrows inputs with the same ownership rules as brute force.
 * Visits nodes by descending (maximum - minimum) finite unary cost, breaking
 * ties by node ID. In regalloc problems this prioritizes expensive spills.
 * Selects the first allowed option minimizing its unary cost plus edges to
 * already assigned nodes, in stored edge order. Both edge orientations are
 * respected regardless of the endpoint assignment order.
 * No backtracking or optimality proof is attempted. On PBQP_OK, *out owns an
 * independent options array, quality is PBQP_SOLUTION_FEASIBLE, and cost uses
 * the reference evaluation order. The empty problem succeeds with cost zero.
 * A dead end returns PBQP_NO_SOLUTION, never PBQP_INFEASIBLE, even if the
 * problem happens to be infeasible. Sets *out to NULL on failure.
 * error may be NULL. */
pbqp_status pbqp_solve_greedy(const pbqp_problem *problem, pbqp_solution **out,
                              pbqp_error *error);

/* R0/R1/R2/RN reduction heuristic with the same validation, borrowing,
 * ownership, and diagnostic contracts as pbqp_solve_greedy. R1 propagates leaf
 * costs to a neighbor; R2 creates or updates a neighbor-pair matrix. RN removes
 * a high-degree node for later assignment. Options are reconstructed in reverse
 * reduction order, choosing the first cheapest allowed option.
 * RN prioritizes conservatively allocatable nonzero options, then low cost of
 * option zero (the spill preference), degree, and node ID. Arbitrary domains
 * remain valid; option zero need not actually be a spill or always allowed.
 * Reductions regroup float arithmetic. Every successful result has quality
 * PBQP_SOLUTION_FEASIBLE, including empty and R1/R2-only problems; the reported
 * cost is recomputed in reference order. Returns PBQP_NO_SOLUTION on a dead
 * end, never PBQP_INFEASIBLE. Fill matrices are allocated during preparation;
 * backpropagation performs no allocation. */
pbqp_status pbqp_solve_reduction(const pbqp_problem *problem,
                                 pbqp_solution **out, pbqp_error *error);

#ifdef __cplusplus
}
#endif
