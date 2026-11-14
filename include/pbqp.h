/*
 * PBQP: a C API for Partitioned Boolean Quadratic Programming problems.
 *
 * A solution chooses one option per node and minimizes the sum of node costs
 * and edge matrix entries. Missing edges contribute zero; INF forbids a node
 * option or a pair. This header defines the in-memory format, min-plus
 * cost algebra, text I/O, ownership helpers, and exact/heuristic solvers.
 * C API version 2.0 is described in doc/c_api.md. The implementation uses
 * C++23; all public declarations are C-compatible. See doc/format.md for the
 * PBQP 1/2 text formats.
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

/** @file pbqp.h
 * @brief Stable C API 2.0 for PBQP data, file I/O, and solvers.
 */

/** @mainpage PBQP API reference
 * @ref pbqp.h defines the stable C API: data views, ownership, costs, solver
 * selection, callbacks, and diagnostics. @ref pbqp.hpp provides C++23 RAII,
 * vector results, span evaluation, typed options, and exceptions.
 *
 * The reference is generated from these public headers. Internal implementation
 * modules are not part of the public interface. API 2.x preserves existing
 * signatures, numeric enum values, public layouts, and ownership contracts.
 */

#include <math.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Compile-time C API version. These constants describe this header; query
 * pbqp_get_api_version when loading a library built separately. API 2.x
 * preserves existing signatures, numeric enum values, and public layouts.
 * Text-format versions are independent of the C API version. */
enum {
  PBQP_API_VERSION_MAJOR = 2, /**< Breaking compatibility version. */
  PBQP_API_VERSION_MINOR = 0, /**< Compatible API additions. */
  PBQP_API_VERSION_PATCH = 0  /**< Compatible fixes. */
};

/** Runtime API version; independent of the PBQP text format version. */
typedef struct pbqp_api_version {
  unsigned major; /**< Breaking compatibility version. */
  unsigned minor; /**< Compatible API additions. */
  unsigned patch; /**< Compatible fixes. */
} pbqp_api_version;

/** Writes the linked library's C API version to out; does not allocate or fail.
 * NULL is accepted and ignored. */
void pbqp_get_api_version(pbqp_api_version *out);

/** Finite costs use float; positive infinity represents a forbidden choice.
 * Input costs must not be NaN or negative infinity. */
typedef float pbqp_cost_t;
#ifdef __cplusplus
/** Positive infinity, marking a forbidden unary option or option pair. */
inline constexpr pbqp_cost_t PBQP_INF = INFINITY;
#else
/** Positive infinity, marking a forbidden unary option or option pair. */
static const pbqp_cost_t PBQP_INF = INFINITY;
#endif

/** Node domain and borrowed unary cost vector. */
typedef struct pbqp_node {
  size_t num_options;       /**< Positive domain size. */
  const pbqp_cost_t *costs; /**< At least num_options readable entries. */
} pbqp_node;

/** Undirected edge with a borrowed source-row, target-column cost matrix. */
typedef struct pbqp_edge {
  /** Undirected edge, stored with src < tgt. Matrix rows are options of
   * src; columns are options of tgt. */
  size_t src;
  size_t tgt; /**< Larger endpoint ID, below problem->num_nodes. */
  /** Row-major: costs[a * nodes[tgt].num_options + b]. */
  const pbqp_cost_t *costs;
} pbqp_edge;

/** May describe caller-owned arrays without transferring their ownership.
 * Objects returned by pbqp_read instead own their arrays and must be treated
 * as read-only: do not replace any pointers or change counts or dimensions,
 * including those in node and edge records. The object is a view into storage
 * managed by pbqp_problem_free. */
typedef struct pbqp_problem {
  size_t num_nodes; /**< Node array length. */
  size_t num_edges; /**< Edge array length. */
  /** Node IDs are array indices, starting at zero. */
  const pbqp_node *nodes;
  const pbqp_edge *edges; /**< Borrowed edge records, in evaluation order. */
} pbqp_problem;

/** Execution status; separate from feasibility and solution quality. */
typedef enum pbqp_status {
  /** Successful execution; solver result quality is reported separately. */
  PBQP_OK = 0,
  PBQP_INVALID = 1,   /**< Invalid input, options, or selector. */
  PBQP_IO_ERROR = 2,  /**< File input/output failure. */
  PBQP_NO_MEMORY = 3, /**< Allocation failure. */
  PBQP_OVERFLOW =
      4, /**< Unrepresentable input number or size, not float sum overflow. */
  /** Proven absence of any feasible assignment. Failure to find an
   * assignment without a proof must never be reported as this status. */
  PBQP_INFEASIBLE = 5,
  /** Search ended without an assignment or a proof of infeasibility. */
  PBQP_NO_SOLUTION = 6,
  /** Runtime or scheduler failure; no solution is returned. */
  PBQP_INTERNAL_ERROR = 7
} pbqp_status;

/** Ordinary floating-point addition, with the platform's rounding and
 * overflow behavior. No checking or saturation is performed. */
static inline pbqp_cost_t pbqp_cost_add(pbqp_cost_t a, pbqp_cost_t b) {
  return a + b;
}

/** Returns a when a < b, otherwise b; inputs are expected to be valid costs. */
static inline pbqp_cost_t pbqp_cost_min(pbqp_cost_t a, pbqp_cost_t b) {
  return a < b ? a : b;
}

/** Optional diagnostic output; no initialization is required before a call.
 * Every public function accepting a non-NULL error pointer leaves message
 * NUL-terminated on return, truncating diagnostic text if necessary.
 * On PBQP_OK, line is zero and message[0] is '\0'. Remaining message bytes
 * are unspecified. On failure, message contains a diagnostic and line is
 * the input line when known, or zero otherwise. */
typedef struct pbqp_error {
  /** Input line, or zero for errors unrelated to an input token. */
  size_t line;
  char message[192]; /**< Always NUL-terminated; empty on success. */
} pbqp_error;

/** Strength of the returned assignment guarantee. */
typedef enum pbqp_solution_quality {
  /** A feasible assignment, without a proof of optimality. */
  PBQP_SOLUTION_FEASIBLE = 0,
  /** Proven optimal under the reference sequential float evaluation order,
   * not necessarily under exact real arithmetic. */
  PBQP_SOLUTION_OPTIMAL = 1
} pbqp_solution_quality;

/** A solver-returned solution owns its options array. Neither the solution
 * nor that array borrows storage from the input problem; both remain valid
 * after the problem is destroyed. Do not replace the options pointer. */
typedef struct pbqp_solution {
  pbqp_cost_t cost; /**< Reference-order sequential float objective. */
  pbqp_solution_quality quality; /**< Feasible or proven optimal. */
  size_t num_nodes;              /**< Assignment array length. */
  size_t *options; /**< Owned node-ID-indexed choices; preserve this pointer. */
} pbqp_solution;

/** Assignment score and feasibility, independent of whole-problem status. */
typedef struct pbqp_evaluation {
  pbqp_cost_t cost; /**< Reference-order score, or INF for a forbidden tuple. */
  /** Zero for an assignment selecting an explicit INF term; otherwise one.
   * A feasible assignment can still have an infinite accumulated cost. */
  int is_feasible;
} pbqp_evaluation;

/** Reads a complete PBQP 1 or PBQP 2 file at the null-terminated path. Only
 * whitespace/comments may follow END. On success, transfers ownership of the
 * problem and all its arrays to the caller; release them using
 * pbqp_problem_free. Treat the returned object and its arrays as read-only,
 * preserving pointers and sizes. PBQP 2 edges can share one owned, read-only
 * matrix array. Sets *out to NULL on failure. error may be NULL. */
pbqp_status pbqp_read(const char *path, pbqp_problem **out, pbqp_error *error);

/** Borrows the problem and its arrays for this call without modifying them.
 * Validates the input using the pbqp_validate contract before opening the
 * output file; callers do not need to validate it separately.
 * Writes PBQP 2 with shared dense/sparse matrices to the null-terminated path,
 * preserving edge order, float values, and signed zero. Original comments are
 * not retained. Closes the file before returning and
 * reporting any output failure. error may be NULL. */
pbqp_status pbqp_write(const char *path, const pbqp_problem *problem,
                       pbqp_error *error);

/** Checks structure and cost values without modifying the problem:
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

/** Validates the problem and option indices, borrowing all inputs without
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

/** Releases a problem and all its arrays. Only accepts NULL or an unmodified
 * object returned by pbqp_read. Never pass a caller-owned problem view. */
void pbqp_problem_free(pbqp_problem *problem);

/** Releases a solution and its owned options array. Only accepts NULL or an
 * object returned by a public solver with its original options pointer.
 */
void pbqp_solution_free(pbqp_solution *solution);

/** Called synchronously with increasing integer percentages, from 1 through
 * 100, at most once per percentage. Naive reports exact visited-tuple
 * percentages; B&B reports an approximate fraction of the surviving search
 * tree, excluding immediate cuts. Neither is an elapsed-time estimate.
 * Several percentages may be reported together. user_data is borrowed for
 * the call. Notifications are serialized and may run on TBB workers during a
 * parallel call. Workers may batch updates. Successful exhaustion or a proof
 * of infeasibility reports 100; B&B reserves 100 until all workers have joined.
 * A callback must not throw or modify the problem or its arrays. */
typedef void (*pbqp_progress_callback)(unsigned percent, void *user_data);

/** Optional human-readable preparation diagnostics. Each message is a borrowed
 * NUL-terminated string valid only during the callback, without a newline.
 * Calls are synchronous on the calling thread, outside DFS, and may precede
 * success or failure. The callback must not throw, modify the problem, or
 * retain the message pointer. Text is diagnostic, not a stable machine format.
 */
typedef void (*pbqp_diagnostic_callback)(const char *message, void *user_data);

/** Runtime type of an option value. Read only the matching union member. */
typedef enum pbqp_option_type {
  PBQP_OPTION_SIZE = 0,      /**< A size_t integer. */
  PBQP_OPTION_BOOL = 1,      /**< An int equal to zero or one. */
  PBQP_OPTION_STRING = 2,    /**< A borrowed NUL-terminated string. */
  PBQP_OPTION_POINTER = 3,   /**< A borrowed context pointer, possibly NULL. */
  PBQP_OPTION_PROGRESS = 4,  /**< A progress callback, possibly NULL. */
  PBQP_OPTION_DIAGNOSTIC = 5 /**< A diagnostic callback, possibly NULL. */
} pbqp_option_type;

/** Typed payload; its active member is determined by pbqp_option::type. */
typedef union pbqp_option_value {
  size_t size;                         /**< PBQP_OPTION_SIZE payload. */
  int boolean;                         /**< PBQP_OPTION_BOOL payload. */
  const char *string;                  /**< PBQP_OPTION_STRING payload. */
  void *pointer;                       /**< PBQP_OPTION_POINTER payload. */
  pbqp_progress_callback progress;     /**< PBQP_OPTION_PROGRESS payload. */
  pbqp_diagnostic_callback diagnostic; /**< PBQP_OPTION_DIAGNOSTIC payload. */
} pbqp_option_value;

/** One named option. key is a borrowed nonempty NUL-terminated string. */
typedef struct pbqp_option {
  const char *key; /**< Case-sensitive key, without CLI prefixes. */
  int type;        /**< A pbqp_option_type tag; checked for consumed keys. */
  pbqp_option_value value; /**< Borrowed typed value. */
} pbqp_option;

/** Open key-value map borrowed for the synchronous call. NULL options and an
 * empty map use defaults. Positive count requires a readable entries array of
 * that length. Keys must be nonempty. The last entry for a key wins. Wrong tags
 * or values for consumed keys return INVALID. Earlier values of repeated keys
 * are ignored. Each solver skips keys it does not understand, reporting one
 * warning per effective unknown key through the warning sink. Unknown
 * tags/payloads are not inspected. By default unknown keys do not fail a call;
 * warnings-as-errors makes them INVALID before search and progress callbacks.
 * All keys, consumed string values, and callback contexts must remain alive and
 * unchanged until the call returns.
 *
 * Common keys (all solvers):
 * - "num-threads": SIZE, positive and int-representable, default 1. Includes
 * the calling thread; actual concurrency may be smaller. Heuristics use one
 * thread and warn when a larger limit is requested.
 * - "progress": PROGRESS, default NULL. Heuristics warn and ignore non-NULL
 *   callbacks; no progress notification is sent.
 * - "progress-data": POINTER, default NULL.
 * - "diagnostics": DIAGNOSTIC, default NULL, synchronous preparation messages.
 * - "diagnostic-data": POINTER, default NULL, independent of progress-data.
 * - "warnings": DIAGNOSTIC, optional warning callback. If NULL or omitted,
 *   warnings use the diagnostics callback/context. With neither sink they are
 *   not printed; library code never writes terminal output.
 * - "warning-data": POINTER, default NULL, used with a non-NULL warnings sink.
 * - "warnings-as-errors": BOOL, default false. Unknown or unsupported options
 * return INVALID with a diagnostic, even without a sink; no
 * warning/preparation/progress callbacks run on this strict unknown-option
 * failure. Wrong consumed types and values remain errors independently of this
 * flag. Warning callbacks have the same synchronous, calling-thread,
 * borrowed-message contract as diagnostics.
 *
 * B&B keys:
 * - "optimization-level": SIZE, 0/1/2, default 1. O0 keeps basic reference-term
 *   B&B bounds. O1 enables all common search improvements; O2 adds structural
 *   order, clique/cardinality/weighted bounds, repricing, and a coverage seed.
 * - BOOL overrides: "option-order", "forward-checking", "register-symmetry",
 *   "greedy-seed", "reduction-seed", "prefix-planning", "work-donation",
 *   "structural-order", "clique-bounds", "cardinality-bounds",
 *   "weighted-bounds", "repricing", "cover-seed". Overrides apply after the
 *   profile, independent of map order. Repricing additionally requires weighted
 *   bounds. Enabled optimizations still require eligible input and resources.
 *
 * No setting changes correctness, reference float evaluation, or result
 * quality. Disabling forward checking retains mandatory fixed-assignment
 * conflict checks. Callbacks must not throw or modify borrowed inputs or this
 * map.
 */
typedef struct pbqp_solve_options {
  size_t count; /**< Number of entries. */
  const pbqp_option
      *entries; /**< Borrowed array; ignored when count is zero. */
} pbqp_solve_options;

/** Constructs a size_t option with a borrowed key. */
static inline pbqp_option pbqp_size_option(const char *key, size_t value) {
  pbqp_option result;
  result.key = key;
  result.type = PBQP_OPTION_SIZE;
  result.value.size = value;
  return result;
}

/** Constructs a boolean option (zero or one) with a borrowed key. */
static inline pbqp_option pbqp_bool_option(const char *key, int value) {
  pbqp_option result;
  result.key = key;
  result.type = PBQP_OPTION_BOOL;
  result.value.boolean = value;
  return result;
}

/** Constructs an option containing a borrowed string; key and payload stay
 * borrowed. */
static inline pbqp_option pbqp_string_option(const char *key,
                                             const char *value) {
  pbqp_option result;
  result.key = key;
  result.type = PBQP_OPTION_STRING;
  result.value.string = value;
  return result;
}

/** Constructs an option containing a borrowed context; key and payload stay
 * borrowed. */
static inline pbqp_option pbqp_pointer_option(const char *key, void *value) {
  pbqp_option result;
  result.key = key;
  result.type = PBQP_OPTION_POINTER;
  result.value.pointer = value;
  return result;
}

/** Constructs a progress callback option with a borrowed key. */
static inline pbqp_option pbqp_progress_option(const char *key,
                                               pbqp_progress_callback value) {
  pbqp_option result;
  result.key = key;
  result.type = PBQP_OPTION_PROGRESS;
  result.value.progress = value;
  return result;
}

/** Constructs a diagnostic callback option with a borrowed key. */
static inline pbqp_option
pbqp_diagnostic_option(const char *key, pbqp_diagnostic_callback value) {
  pbqp_option result;
  result.key = key;
  result.type = PBQP_OPTION_DIAGNOSTIC;
  result.value.diagnostic = value;
  return result;
}

/** Solver selection for the common dispatcher. */
typedef enum pbqp_solver {
  PBQP_SOLVER_NAIVE = 0,  /**< Exhaustive lexicographic enumeration. */
  PBQP_SOLVER_BNB = 1,    /**< Exact branch-and-bound. */
  PBQP_SOLVER_GREEDY = 3, /**< Cost-priority assignment heuristic. */
  PBQP_SOLVER_R1R2RN = 4  /**< Reduction/backpropagation heuristic. */
} pbqp_solver;

/** Dispatches to the selected solver with the same contracts as its dedicated
 * entry point. NULL options select one thread with no callbacks. All solvers
 * validate input and options, borrow input only during the call, and return an
 * independently owned solution. Unknown solver values return PBQP_INVALID,
 * clear *out when supplied, and invoke no callbacks. Inspect solution quality:
 * exact solvers return OPTIMAL; heuristics return FEASIBLE. */
pbqp_status pbqp_solve(pbqp_solver solver, const pbqp_problem *problem,
                       const pbqp_solve_options *options, pbqp_solution **out,
                       pbqp_error *error);

/** Enumerates tuples in lexicographic order. Ties keep the first tuple.
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
 * No pruning assumptions are made about negative costs.
 * NULL options select one thread without callbacks. Parallel workers merge
 * private minima by cost, then lexicographic assignment. Progress counts every
 * tuple, including forbidden tuples, reaching p after ceil(total * p / 100)
 * visits; errors before enumeration invoke no progress callbacks. All work
 * and callbacks complete before return. Scheduler failures report
 * PBQP_INTERNAL_ERROR. */
pbqp_status pbqp_solve_bruteforce(const pbqp_problem *problem,
                                  const pbqp_solve_options *options,
                                  pbqp_solution **out, pbqp_error *error);

/** Exact branch-and-bound with the same validation, ownership, evaluation,
 * output, and diagnostic contracts as pbqp_solve_bruteforce. Returns any
 * optimum under reference-order float evaluation; tied assignments need not
 * match naive search and can depend on thread scheduling. The default O1
 * profile uses greedy and reduction to seed feasible incumbents. O2 adds
 * verified structural/capacity bounds and seeds; O0 disables optional search
 * improvements. BOOL options override profile defaults. Bounds condition
 * options on the fixed prefix and sum independent term minima in reference
 * order, without regrouping floats. Prunes L >= G whenever a feasible incumbent
 * exists. On PBQP_OK quality is PBQP_SOLUTION_OPTIMAL; PBQP_INFEASIBLE requires
 * an exhaustive proof. NULL options select one thread without progress or
 * diagnostics. Otherwise, options control concurrency and optional
 * progress/preparation callbacks. Progress approximates the fraction of the
 * surviving search tree. Immediate infeasibility, symmetry, and bound cuts
 * receive no child weight; remaining children share their parent's weight.
 * Initial frontier jobs have equal weights; jobs closed on arrival are
 * excluded. Subtrees can require unequal work, so percentages can pause or
 * advance abruptly and are not an ETA. Notifications obey the serialized
 * callback contract above. Success and infeasibility proofs report 100; errors
 * before search invoke no progress callbacks. Diagnostics describe planning and
 * worker preparation on the calling thread, outside DFS. Workspaces are private
 * to workers; only immutable input/minima, a synchronized incumbent, and
 * progress reporting are shared. All work and callbacks complete before return.
 * Runtime/scheduler exceptions become PBQP_INTERNAL_ERROR; allocation failures
 * remain PBQP_NO_MEMORY. */
pbqp_status pbqp_solve_branch_and_bound(const pbqp_problem *problem,
                                        const pbqp_solve_options *options,
                                        pbqp_solution **out, pbqp_error *error);

/** Validates and borrows inputs with the same ownership rules as brute force.
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
pbqp_status pbqp_solve_greedy(const pbqp_problem *problem,
                              const pbqp_solve_options *options,
                              pbqp_solution **out, pbqp_error *error);

/** R0/R1/R2/RN reduction heuristic with the same validation, borrowing,
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
                                 const pbqp_solve_options *options,
                                 pbqp_solution **out, pbqp_error *error);

#ifdef __cplusplus
}
#endif
