/*
 * Public C entry point for the R1/R2/RN reduction heuristic.
 *
 * Validates borrowed input, prepares an independent reduction graph, and
 * transfers solution ownership only after successful backpropagation.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp;
import :api_boundary;
import :reduction_api;

extern "C" pbqp_status pbqp_solve_reduction(const pbqp_problem *problem,
                                            const pbqp_solve_options *options,
                                            pbqp_solution **out,
                                            pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, pbqp::solve_reduction, problem, options, out,
                        error);
}
