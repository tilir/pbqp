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
  return ok ? 0 : 1;
}
