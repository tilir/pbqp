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
import :error;
import :ownership;
import :problem;
import :evaluation;

extern "C" {
void pbqp_solution_free(pbqp_solution *solution) {
  delete static_cast<pbqp::SolutionStorage *>(solution);
}

pbqp_status pbqp_evaluate_assignment(const pbqp_problem *p,
                                     const size_t *options, size_t num_nodes,
                                     pbqp_evaluation *out, pbqp_error *error) {
  if (out)
    *out = {};
  return pbqp::api_call(error, [&]() -> pbqp_status {
    if (!out)
      return pbqp::set_error(error, PBQP_INVALID, 0,
                             "missing evaluation output");
    pbqp::validate(p);
    if (num_nodes != p->num_nodes || (num_nodes && !options))
      return pbqp::set_error(error, PBQP_INVALID, 0,
                             "invalid assignment size or pointer");
    for (size_t k = 0; k < num_nodes; ++k)
      if (options[k] >= p->nodes[k].num_options)
        return pbqp::set_error(error, PBQP_INVALID, 0,
                               "assignment option index out of range");
    out->is_feasible = pbqp::evaluate_assignment(*p, options, out->cost);
    return PBQP_OK;
  });
}
} // extern "C"
