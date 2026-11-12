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

int main(void) {
  pbqp_cost_t costs[] = {4, -2};
  pbqp_node node = {2, costs};
  pbqp_problem problem = {1, 0, &node, NULL};
  pbqp_solution *solution = NULL;
  int ok;
  if (pbqp_cost_add(PBQP_INF, -1.0f) != PBQP_INF)
    return 1;
  if (pbqp_cost_add(0.5f, -0.75f) != -0.25f)
    return 1;
  if (pbqp_solve_bruteforce(&problem, &solution, NULL) != PBQP_OK)
    return 1;
  ok = solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == -2 &&
       solution->options[0] == 1;
  pbqp_solution_free(solution);
  if (!ok || pbqp_solve_greedy(&problem, &solution, NULL) != PBQP_OK)
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
  if (!ok || pbqp_solve_branch_and_bound(&problem, &solution, NULL) != PBQP_OK)
    return 1;
  ok = solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == -2 &&
       solution->options[0] == 1;
  pbqp_solution_free(solution);
  {
    progress_log log = {0, 0};
    if (pbqp_solve_bruteforce_with_progress(&problem, record_progress, &log,
                                            &solution, NULL) != PBQP_OK)
      return 1;
    ok = ok && log.count == 100 && !log.invalid && solution->cost == -2;
    pbqp_solution_free(solution);
  }
  {
    progress_log log = {0, 0};
    if (pbqp_solve_branch_and_bound_with_progress(
            &problem, record_progress, &log, &solution, NULL) != PBQP_OK)
      return 1;
    ok = ok && log.count == 100 && !log.invalid && solution->cost == -2;
    pbqp_solution_free(solution);
  }
  return ok ? 0 : 1;
}
