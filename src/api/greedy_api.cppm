/*
 * Internal greedy API orchestration: option checks, validated borrowed input,
 * owned search preparation, and result transfer. Exceptions are translated
 * only by the thin public C entry point.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp:greedy_api;
import :solve_options;
import :error;
import :diagnostics;
import :problem;
import :greedy;

namespace pbqp {
pbqp_status solve_greedy(const pbqp_problem *p,
                         const pbqp_solve_options *options, pbqp_solution **out,
                         pbqp_error *error) {
  if (!out)
    return set_error(error, PBQP_INVALID, 0, "missing output pointer");
  const CommonSolveOptions config(options);
  validate(p);
  if (auto status = config.check_heuristic(error); status != PBQP_OK)
    return status;
  const auto log = config.log;
  log.line("greedy: preparing single-thread heuristic search");

  GreedySearch search(*p);
  auto status = search.prepare(error);
  if (status != PBQP_OK)
    return status;
  status = search.run(error);
  if (status != PBQP_OK)
    return status;
  *out = search.release_solution();
  return PBQP_OK;
}
} // namespace pbqp
