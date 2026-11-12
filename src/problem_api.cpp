/*
 * Problem validation and lifetime management.
 *
 * Implements C entry points using the internal problem validation module.
 * Releases API-owned problem storage and translates validation errors to C
 * statuses.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include "pbqp.h"

module pbqp;
import :api_boundary;
import :ownership;
import :problem;

extern "C" {
void pbqp_problem_free(pbqp_problem *p) {
  delete static_cast<ProblemStorage *>(p);
}

pbqp_status pbqp_validate(const pbqp_problem *p, pbqp_error *error) {
  return api_call(error, [&]() -> pbqp_status {
    validate(p);
    return PBQP_OK;
  });
}
} // extern "C"
