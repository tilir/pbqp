/*
 * Internal dsatur API orchestration: option checks, validated borrowed input,
 * owned search preparation, and result transfer. Exceptions are translated
 * only by the thin public C entry point.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp:dsatur_api;
import :solve_options;
import :diagnostics;
import :error;
import :problem;
import :interference;
import :greedy;
import :dsatur;
import :dsatur_diagnostics;

namespace pbqp {
pbqp_status solve_dsatur(const pbqp_problem *p,
                         const pbqp_solve_options *options, pbqp_solution **out,
                         pbqp_error *error) {
  if (!out)
    return set_error(error, PBQP_INVALID, 0, "missing output pointer");
  const CommonSolveOptions config(options);
  validate(p);
  if (auto status = config.check_heuristic(error); status != PBQP_OK)
    return status;
  config.log.line(
      "dsatur: single-thread DSATUR/tabu heuristic with greedy fallback");
  GreedySearch greedy(*p);
  const bool fallback =
      greedy.prepare(nullptr) == PBQP_OK && greedy.run(nullptr) == PBQP_OK;
  const auto check = check_interference_structure(*p);
  DsaturSearch search(*p);
  const bool ready = search.prepare(check);
  describe_dsatur(*p, check, search, config.log);
  if (ready) {
    search.run();
    describe_dsatur_result(search, config.log);
  }
  if (search.has_solution() &&
      (!fallback || search.result().cost < greedy.get_solution().cost)) {
    *out = search.release_solution();
  } else if (fallback) {
    config.log.line("dsatur: retaining greedy fallback");
    *out = greedy.release_solution();
  } else {
    return set_error(error, PBQP_NO_SOLUTION, 0,
                     "DSATUR and greedy fallback found no feasible "
                     "assignment; infeasibility is not proven");
  }
  return PBQP_OK;
}
} // namespace pbqp
