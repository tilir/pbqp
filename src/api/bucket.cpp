/*
 * C boundary for exact bucket elimination.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp;
import :api_boundary;
import :bucket_api;

extern "C" pbqp_status pbqp_solve_bucket(const pbqp_problem *p,
                                         const pbqp_solve_options *options,
                                         pbqp_solution **out,
                                         pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, pbqp::solve_bucket, p, options, out, error);
}
