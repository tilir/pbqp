/*
 * Bound exact-search wall time through the C API, then fall back to a
 * heuristic. The same borrowed problem survives both calls; each returned
 * solution is owned.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3) {
    fprintf(stderr, "Usage: pbqp_example_timeout INPUT [TIMEOUT_MS]\n");
    return 1;
  }
  pbqp_api_version version;
  pbqp_get_api_version(&version);
  if (version.major != PBQP_API_VERSION_MAJOR || version.minor < 2) {
    fprintf(stderr, "Timeout requires C API 2.2 or later in the 2.x series\n");
    return 1;
  }
  size_t timeout_ms = 15000;
  if (argc == 3) {
    char *end = NULL;
    errno = 0;
    const uintmax_t parsed = strtoumax(argv[2], &end, 10);
    if (errno || argv[2][0] < '0' || argv[2][0] > '9' || *end ||
        parsed > SIZE_MAX) {
      fprintf(stderr, "TIMEOUT_MS must be a nonnegative size_t integer\n");
      return 1;
    }
    timeout_ms = (size_t)parsed;
  }
  pbqp_problem *problem = NULL;
  pbqp_solution *solution = NULL;
  pbqp_error error;
  pbqp_status status = pbqp_read(argv[1], &problem, &error);
  if (status != PBQP_OK) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }

  const pbqp_option entries[] = {pbqp_size_option("optimization-level", 2),
                                 pbqp_size_option("num-threads", 4),
                                 pbqp_size_option("timeout-ms", timeout_ms)};
  const pbqp_solve_options options = {3, entries};
  status = pbqp_solve(PBQP_SOLVER_BNB, problem, &options, &solution, &error);
  if (status == PBQP_TIMEOUT) {
    /* No workers or callbacks remain active; solution is NULL. Do not pass
     * exact-search options to the heuristic: it does not support timeout-ms. */
    fprintf(stderr, "Exact search timed out; falling back to r1r2rn\n");
    status = pbqp_solve(PBQP_SOLVER_R1R2RN, problem, NULL, &solution, &error);
  }
  if (status == PBQP_OK) {
    printf("COST %.9g\nQUALITY %s\n", (double)solution->cost,
           solution->quality == PBQP_SOLUTION_OPTIMAL ? "optimal" : "feasible");
    for (size_t node = 0; node < solution->num_nodes; ++node)
      printf("NODE %zu %zu\n", node, solution->options[node]);
  } else {
    fprintf(stderr, "%s\n", error.message);
  }
  pbqp_solution_free(solution);
  pbqp_problem_free(problem);
  return status == PBQP_OK ? 0 : 1;
}
