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
import :greedy_api;

extern "C" pbqp_status pbqp_solve_greedy(const pbqp_problem *p,
                                         const pbqp_solve_options *options,
                                         pbqp_solution **out,
                                         pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, pbqp::solve_greedy, p, options, out, error);
}
