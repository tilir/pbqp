/*
 * C compatibility and linkage test.
 *
 * Includes the public header from C and exercises cost arithmetic and solving.
 * Checks that a C caller can use the C++ implementation through the C API.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"

typedef struct progress_log {
  unsigned count;
  int invalid;
} progress_log;

static void record_progress(unsigned percent, void *data) {
  progress_log *log = (progress_log *)data;
  if (percent != log->count + 1 || percent > 100)
    log->invalid = 1;
  ++log->count;
}

typedef struct diagnostic_log {
  unsigned count;
  int invalid;
} diagnostic_log;

static void record_diagnostic(const char *message, void *data) {
  diagnostic_log *log = (diagnostic_log *)data;
  if (!message || !message[0])
    log->invalid = 1;
  ++log->count;
}

/* Exercise both dedicated entries and enum dispatch from an actual C caller. */
static int test_dispatch(const pbqp_problem *problem) {
  typedef pbqp_status (*solver_fn)(const pbqp_problem *,
                                   const pbqp_solve_options *, pbqp_solution **,
                                   pbqp_error *);

  const struct {
    pbqp_solver kind;
    solver_fn solve;
    int exact;
  } solvers[] = {{PBQP_SOLVER_NAIVE, pbqp_solve_bruteforce, 1},
                 {PBQP_SOLVER_BNB, pbqp_solve_branch_and_bound, 1},
                 {PBQP_SOLVER_BNBOPT, pbqp_solve_branch_and_bound_optimized, 1},
                 {PBQP_SOLVER_GREEDY, pbqp_solve_greedy, 0},
                 {PBQP_SOLVER_R1R2RN, pbqp_solve_reduction, 0}};

  size_t i;
  for (i = 0; i < sizeof(solvers) / sizeof(solvers[0]); ++i) {
    progress_log progress = {0, 0};
    diagnostic_log diagnostics = {0, 0};
    pbqp_solve_options options = {4, solvers[i].exact ? record_progress : NULL,
                                  &progress, record_diagnostic, &diagnostics};
    pbqp_solution *direct = NULL, *dispatched = NULL;
    pbqp_error error;
    if (solvers[i].solve(problem, &options, &direct, &error) != PBQP_OK)
      return 0;
    pbqp_cost_t cost = direct->cost;
    pbqp_solution_quality quality = direct->quality;
    pbqp_solution_free(direct);
    if (cost != -2 ||
        quality != (solvers[i].exact ? PBQP_SOLUTION_OPTIMAL
                                     : PBQP_SOLUTION_FEASIBLE) ||
        diagnostics.count == 0 || diagnostics.invalid || progress.invalid ||
        progress.count != (solvers[i].exact ? 100u : 0u) || error.line ||
        error.message[0])
      return 0;
    progress.count = diagnostics.count = 0;
    if (pbqp_solve(solvers[i].kind, problem, &options, &dispatched, &error) !=
        PBQP_OK)
      return 0;
    int matches = dispatched->cost == cost && dispatched->quality == quality;
    pbqp_solution_free(dispatched);
    if (!matches || diagnostics.count == 0 || diagnostics.invalid ||
        progress.invalid || progress.count != (solvers[i].exact ? 100u : 0u))
      return 0;
    if (pbqp_solve(solvers[i].kind, problem, NULL, &dispatched, NULL) !=
        PBQP_OK)
      return 0;
    pbqp_solution_free(dispatched);
    progress.count = diagnostics.count = 0;
    options.num_threads = 0;
    if (pbqp_solve(solvers[i].kind, problem, &options, &dispatched, &error) !=
            PBQP_INVALID ||
        dispatched || progress.count || diagnostics.count || !error.message[0])
      return 0;
    options.num_threads = 1;
    if (pbqp_solve(solvers[i].kind, NULL, &options, &dispatched, &error) !=
            PBQP_INVALID ||
        dispatched || progress.count || diagnostics.count)
      return 0;
    if (pbqp_solve(solvers[i].kind, problem, &options, NULL, &error) !=
            PBQP_INVALID ||
        progress.count || diagnostics.count)
      return 0;
    if (!solvers[i].exact) {
      options.progress = record_progress;
      if (solvers[i].solve(problem, &options, &dispatched, &error) !=
              PBQP_INVALID ||
          dispatched || progress.count || diagnostics.count ||
          !error.message[0])
        return 0;
    }
  }
  {
    progress_log progress = {0, 0};
    diagnostic_log diagnostics = {0, 0};
    pbqp_solve_options options = {1, record_progress, &progress,
                                  record_diagnostic, &diagnostics};
    pbqp_solution *solution = NULL;
    pbqp_error error;
    if (pbqp_solve((pbqp_solver)7, problem, &options, &solution, &error) !=
            PBQP_INVALID ||
        solution || !error.message[0] || error.line || progress.count ||
        diagnostics.count)
      return 0;
    if (pbqp_solve((pbqp_solver)7, problem, NULL, NULL, NULL) != PBQP_INVALID)
      return 0;
  }
  return 1;
}

int main(void) {
  pbqp_api_version version;
  pbqp_get_api_version(NULL);
  pbqp_get_api_version(&version);
  if (version.major != PBQP_API_VERSION_MAJOR ||
      version.minor != PBQP_API_VERSION_MINOR ||
      version.patch != PBQP_API_VERSION_PATCH || version.major != 1 ||
      version.minor != 0 || version.patch != 0)
    return 1;
  pbqp_cost_t costs[] = {4, -2};
  pbqp_node node = {2, costs};
  pbqp_problem problem = {1, 0, &node, NULL};
  pbqp_solution *solution = NULL;
  int ok;
  if (pbqp_cost_add(PBQP_INF, -1.0f) != PBQP_INF)
    return 1;
  if (pbqp_cost_add(0.5f, -0.75f) != -0.25f)
    return 1;
  if (pbqp_solve_bruteforce(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  ok = solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == -2 &&
       solution->options[0] == 1;
  pbqp_solution_free(solution);
  if (!ok || pbqp_solve_greedy(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  {
    pbqp_evaluation evaluation;
    ok = solution->quality == PBQP_SOLUTION_FEASIBLE &&
         pbqp_evaluate_assignment(&problem, solution->options,
                                  solution->num_nodes, &evaluation,
                                  NULL) == PBQP_OK &&
         evaluation.is_feasible && evaluation.cost == -2;
  }
  pbqp_solution_free(solution);
  if (!ok ||
      pbqp_solve_branch_and_bound(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  ok = solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == -2 &&
       solution->options[0] == 1;
  pbqp_solution_free(solution);
  if (!ok || pbqp_solve_reduction(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  ok = solution->quality == PBQP_SOLUTION_FEASIBLE && solution->cost == -2 &&
       solution->options[0] == 1;
  pbqp_solution_free(solution);
  {
    progress_log log = {0, 0};
    const pbqp_solve_options options = {1, record_progress, &log, NULL, NULL};
    if (pbqp_solve_bruteforce(&problem, &options, &solution, NULL) != PBQP_OK)
      return 1;
    ok = ok && log.count == 100 && !log.invalid && solution->cost == -2;
    pbqp_solution_free(solution);
  }
  {
    progress_log log = {0, 0};
    const pbqp_solve_options options = {1, record_progress, &log, NULL, NULL};
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
        PBQP_OK)
      return 1;
    ok = ok && log.count == 100 && !log.invalid && solution->cost == -2;
    pbqp_solution_free(solution);
  }
  {
    progress_log log = {0, 0};
    pbqp_solve_options options = {4, record_progress, &log, NULL, NULL};
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
        PBQP_OK)
      return 1;
    ok = ok && solution->quality == PBQP_SOLUTION_OPTIMAL &&
         solution->cost == -2 && log.count == 100 && !log.invalid;
    pbqp_solution_free(solution);
    log.count = 0;
    log.invalid = 0;
    if (pbqp_solve_bruteforce(&problem, &options, &solution, NULL) != PBQP_OK)
      return 1;
    ok = ok && solution->cost == -2 && solution->options[0] == 1 &&
         log.count == 100 && !log.invalid;
    pbqp_solution_free(solution);
    options.num_threads = 0;
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
            PBQP_INVALID ||
        solution)
      return 1;
  }
  if (pbqp_solve_branch_and_bound_optimized(&problem, NULL, &solution, NULL) !=
      PBQP_OK)
    return 1;
  ok = ok && solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == -2;
  pbqp_solution_free(solution);
  {
    progress_log log = {0, 0};
    pbqp_solve_options options = {4, record_progress, &log, NULL, NULL};
    if (pbqp_solve_branch_and_bound_optimized(&problem, &options, &solution,
                                              NULL) != PBQP_OK)
      return 1;
    ok = ok && solution->cost == -2 && log.count == 100 && !log.invalid;
    pbqp_solution_free(solution);
    log.count = 0;
    options.num_threads = 1;
    if (pbqp_solve_branch_and_bound_optimized(&problem, &options, &solution,
                                              NULL) != PBQP_OK)
      return 1;
    ok = ok && log.count == 100 && !log.invalid;
    pbqp_solution_free(solution);
  }
  {
    pbqp_cost_t unary[] = {3, 0};
    pbqp_cost_t pair[] = {0, 0, 0, PBQP_INF};
    pbqp_node nodes[] = {{2, unary}, {2, unary}, {2, unary}};
    pbqp_edge edges[] = {{0, 1, pair}, {0, 2, pair}, {1, 2, pair}};
    pbqp_problem regalloc = {3, 3, nodes, edges};
    if (pbqp_solve_branch_and_bound_optimized(&regalloc, NULL, &solution,
                                              NULL) != PBQP_OK)
      return 1;
    ok =
        ok && solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == 6;
    pbqp_solution_free(solution);
  }
  {
    progress_log progress = {0, 0};
    diagnostic_log diagnostics = {0, 0};
    pbqp_solve_options options = {4, record_progress, &progress,
                                  record_diagnostic, &diagnostics};
    if (pbqp_solve_branch_and_bound_optimized(&problem, &options, &solution,
                                              NULL) != PBQP_OK)
      return 1;
    ok = ok && solution->cost == -2 && progress.count == 100 &&
         !progress.invalid && diagnostics.count > 0 && !diagnostics.invalid;
    pbqp_solution_free(solution);
    diagnostics.count = 0;
    if (pbqp_solve_branch_and_bound_optimized(NULL, &options, &solution,
                                              NULL) != PBQP_INVALID ||
        solution || diagnostics.count)
      return 1;
  }
  return ok && test_dispatch(&problem) ? 0 : 1;
}
