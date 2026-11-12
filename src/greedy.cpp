/*
 * Public C entry point for the greedy PBQP heuristic.
 *
 * Validates borrowed inputs, prepares owned search state, and transfers the
 * independent solution to the caller only after successful search.
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
import :problem;
import :greedy;

extern "C" pbqp_status pbqp_solve_greedy(const pbqp_problem *p,
                                         pbqp_solution **out,
                                         pbqp_error *error) {
  if (out)
    *out = nullptr;
  return api_call(error, [&]() -> pbqp_status {
    if (!out)
      return pbqp_fail(error, PBQP_INVALID, 0, "missing output pointer");
    validate(p);

    GreedySearch search(*p);
    auto status = search.prepare(error);
    if (status != PBQP_OK)
      return status;
    status = search.run(error);
    if (status != PBQP_OK)
      return status;
    *out = search.release_solution();
    return PBQP_OK;
  });
}
