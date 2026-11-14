/*
 * Common C solver dispatcher. Dedicated entries own validation and search;
 * dispatch only selects an algorithm and rejects unknown selectors.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp;
import :error;

extern "C" pbqp_status pbqp_solve(pbqp_solver solver,
                                  const pbqp_problem *problem,
                                  const pbqp_solve_options *options,
                                  pbqp_solution **out, pbqp_error *error) {
  switch (solver) {
  case PBQP_SOLVER_NAIVE:
    return pbqp_solve_bruteforce(problem, options, out, error);
  case PBQP_SOLVER_BNB:
    return pbqp_solve_branch_and_bound(problem, options, out, error);
  case PBQP_SOLVER_BNBOPT:
    return pbqp_solve_branch_and_bound_optimized(problem, options, out, error);
  case PBQP_SOLVER_GREEDY:
    return pbqp_solve_greedy(problem, options, out, error);
  case PBQP_SOLVER_R1R2RN:
    return pbqp_solve_reduction(problem, options, out, error);
  }
  if (out)
    *out = nullptr;
  return pbqp::set_error(error, PBQP_INVALID, 0, "unknown solver");
}
