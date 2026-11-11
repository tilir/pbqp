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
#include <new>

module pbqp;
import :problem;
import :failure;
import :error;

extern "C" {
void pbqp_problem_free(pbqp_problem *p) {
  if (!p)
    return;
  if (p->nodes)
    for (size_t k = 0; k < p->num_nodes; ++k)
      delete[] p->nodes[k].costs;
  if (p->edges)
    for (size_t k = 0; k < p->num_edges; ++k)
      delete[] p->edges[k].costs;
  delete[] p->nodes;
  delete[] p->edges;
  delete p;
}

pbqp_status pbqp_validate(const pbqp_problem *p, pbqp_error *error) {
  pbqp_clear_error(error);
  try {
    validate(p);
    return PBQP_OK;
  } catch (const Failure &f) {
    return pbqp_fail(error, f.status, f.line, f.message);
  } catch (const std::bad_alloc &) {
    return pbqp_fail(error, PBQP_NO_MEMORY, 0, "allocation failed");
  }
}
} // extern "C"
