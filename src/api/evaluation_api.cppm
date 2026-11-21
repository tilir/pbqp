/*
 * Validated assignment evaluation behind the public C ownership boundary.
 * Checks indices before delegating arithmetic to the reference evaluator.
 * Exceptions are translated by the caller's common API boundary.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp:evaluation_api;
import :error;
import :problem;
import :evaluation;

namespace pbqp {
pbqp_status evaluate_problem_assignment(const pbqp_problem *p,
                                        const size_t *options, size_t num_nodes,
                                        pbqp_evaluation *out,
                                        pbqp_error *error) {
  if (!out)
    return set_error(error, PBQP_INVALID, 0, "missing evaluation output");
  validate(p);
  if (num_nodes != p->num_nodes || (num_nodes && !options))
    return set_error(error, PBQP_INVALID, 0,
                     "invalid assignment size or pointer");
  for (size_t k = 0; k < num_nodes; ++k)
    if (options[k] >= p->nodes[k].num_options)
      return set_error(error, PBQP_INVALID, 0,
                       "assignment option index out of range");
  out->is_feasible = evaluate_assignment(*p, options, out->cost);
  return PBQP_OK;
}
} // namespace pbqp
