/*
 * Exhaustive PBQP reference solver.
 *
 * Enumerates option tuples in lexicographic order and retains the first
 * optimum. The enumeration uses status returns and performs no exception
 * handling.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp;
import :api_boundary;
import :bruteforce_api;

extern "C" pbqp_status pbqp_solve_bruteforce(const pbqp_problem *p,
                                             const pbqp_solve_options *options,
                                             pbqp_solution **out,
                                             pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, pbqp::solve_bruteforce, p, options, out, error);
}
