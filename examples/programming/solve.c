/*
 * Solve a caller-owned PBQP problem using only the C API.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <stdio.h>

int main(void) {
  const pbqp_cost_t costs[] = {1, 0};
  const pbqp_cost_t matrix[] = {0, 0, 0, PBQP_INF};
  const pbqp_node nodes[] = {{2, costs}, {2, costs}};
  const pbqp_edge edges[] = {{0, 1, matrix}};
  const pbqp_problem problem = {2, 1, nodes, edges};
  const pbqp_option entries[] = {pbqp_size_option("num-threads", 1),
                                 pbqp_size_option("optimization-level", 2)};
  const pbqp_solve_options options = {2, entries};
  pbqp_solution *solution = NULL;
  pbqp_error error;
  pbqp_api_version version;
  pbqp_get_api_version(&version);
  if (version.major != PBQP_API_VERSION_MAJOR) {
    fprintf(stderr, "incompatible C API version\n");
    return 1;
  }
  const pbqp_status status =
      pbqp_solve(PBQP_SOLVER_BNB, &problem, &options, &solution, &error);
  if (status != PBQP_OK) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }
  printf("COST %.9g\n", (double)solution->cost);
  for (size_t node = 0; node < solution->num_nodes; ++node)
    printf("NODE %zu %zu\n", node, solution->options[node]);
  pbqp_solution_free(solution);
  /* The stack-backed problem is borrowed: never call pbqp_problem_free here. */
  return 0;
}
