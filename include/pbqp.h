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
static const pbqp_cost_t PBQP_INF = INFINITY;

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
 * including those in node and edge records. pbqp_problem_free relies on the
 * original pointers and sizes to release these allocations. */
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
  PBQP_INFEASIBLE
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
  /* A feasible assignment with proven optimality. */
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

/* Releases a problem and all its arrays. Only accepts NULL or an unmodified
 * object returned by pbqp_read. Never pass a caller-owned problem view. */
void pbqp_problem_free(pbqp_problem *problem);

/* Releases a solution and its owned options array. Only accepts NULL or an
 * object returned by pbqp_solve_bruteforce with its original options pointer.
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

#ifdef __cplusplus
}
#endif
