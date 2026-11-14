/*
 * Public C entry points for exact branch-and-bound search.
 *
 * Validates borrowed inputs and transfers independent solution storage only
 * after search has proved optimality. Optional callbacks report approximate
 * progress.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp;
import :bnb_api;

extern "C" pbqp_status
pbqp_solve_branch_and_bound(const pbqp_problem *p,
                            const pbqp_solve_options *options,
                            pbqp_solution **out, pbqp_error *error) {
  return pbqp::solve_bnb(p, options, out, error);
}
