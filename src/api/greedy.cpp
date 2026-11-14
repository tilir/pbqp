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
import :diagnostics;
import :problem;
import :greedy;

extern "C" pbqp_status pbqp_solve_greedy(const pbqp_problem *p,
                                         const pbqp_solve_options *options,
                                         pbqp_solution **out,
                                         pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, [&]() -> pbqp_status {
    if (!out)
      return pbqp::set_error(error, PBQP_INVALID, 0, "missing output pointer");
    pbqp::thread_count(options);
    if (options && options->progress)
      return pbqp::set_error(error, PBQP_INVALID, 0,
                             "progress is unsupported for heuristic solvers");
    pbqp::validate(p);
    const pbqp::Diagnostics log(options ? options->diagnostics : nullptr,
                                options ? options->diagnostic_data : nullptr);
    log.line("greedy: preparing single-thread heuristic search");

    pbqp::GreedySearch search(*p);
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
