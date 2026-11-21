/*
 * Public assignment evaluation and solution destruction.
 *
 * Checks caller-supplied option indices before using the shared evaluator.
 * Solution storage is independent of the source problem's lifetime.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp;
import :api_boundary;
import :ownership;
import :evaluation_api;

extern "C" {
void pbqp_solution_free(pbqp_solution *solution) {
  delete static_cast<pbqp::SolutionStorage *>(solution);
}

pbqp_status pbqp_evaluate_assignment(const pbqp_problem *p,
                                     const size_t *options, size_t num_nodes,
                                     pbqp_evaluation *out, pbqp_error *error) {
  if (out)
    *out = {};
  return pbqp::api_call(error, pbqp::evaluate_problem_assignment, p, options,
                        num_nodes, out, error);
}
} // extern "C"
