/*
 * C boundary for the bounded DSATUR/tabu heuristic with greedy fallback.
 * Both algorithms borrow validated inputs; the winning feasible assignment
 * owns independent storage and carries no optimality or infeasibility claim.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp;
import :api_boundary;
import :dsatur_api;

extern "C" pbqp_status pbqp_solve_dsatur(const pbqp_problem *p,
                                         const pbqp_solve_options *options,
                                         pbqp_solution **out,
                                         pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, pbqp::solve_dsatur, p, options, out, error);
}
