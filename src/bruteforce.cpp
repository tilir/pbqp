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
#include <algorithm>
#include <limits>
#include <memory>
#include <new>

module pbqp;
import :error;
import :ownership;
import :evaluation;
import :progress;

bool advance(const pbqp_problem &p, size_t *tuple) noexcept {
  for (size_t k = p.num_nodes; k > 0; --k) {
    if (++tuple[k - 1] < p.nodes[k - 1].num_options)
      return true;
    tuple[k - 1] = 0;
  }
  return false;
}

template <class Observer>
bool enumerate_assignments(const pbqp_problem &p, pbqp_solution &solution,
                           size_t *tuple, Observer observe) noexcept {
  bool found = false;
  do {
    pbqp_cost_t cost;
    if (evaluate_assignment(p, tuple, cost) &&
        (!found || cost < solution.cost)) {
      found = true;
      solution.cost = cost;
      if (p.num_nodes)
        std::copy_n(tuple, p.num_nodes, solution.options);
    }
    observe(tuple);
  } while (advance(p, tuple));
  return found;
}

extern "C" {
pbqp_status pbqp_solve_bruteforce(const pbqp_problem *p, pbqp_solution **out,
                                  pbqp_error *error) {
  return pbqp_solve_bruteforce_with_progress(p, nullptr, nullptr, out, error);
}

pbqp_status pbqp_solve_bruteforce_with_progress(const pbqp_problem *p,
                                                pbqp_progress_callback progress,
                                                void *user_data,
                                                pbqp_solution **out,
                                                pbqp_error *error) {
  pbqp_clear_error(error);
  if (out)
    *out = nullptr;
  if (!out)
    return pbqp_fail(error, PBQP_INVALID, 0, "missing output pointer");
  auto status = pbqp_validate(p, error);
  if (status != PBQP_OK)
    return status;
  if (p->num_nodes > std::numeric_limits<size_t>::max() / sizeof(size_t))
    return pbqp_fail(error, PBQP_OVERFLOW, 0,
                     "solution allocation size overflow");

  SolutionPtr solution(new (std::nothrow) pbqp_solution{}, solution_deleter);
  if (!solution)
    return pbqp_fail(error, PBQP_NO_MEMORY, 0, "allocation failed");
  solution->num_nodes = p->num_nodes;
  std::unique_ptr<size_t[]> tuple;
  if (p->num_nodes) {
    tuple.reset(new (std::nothrow) size_t[p->num_nodes]{});
    solution->options = new (std::nothrow) size_t[p->num_nodes];
    if (!tuple || !solution->options)
      return pbqp_fail(error, PBQP_NO_MEMORY, 0, "allocation failed");
  }

  std::unique_ptr<size_t[]> boundary;
  if (progress && p->num_nodes) {
    boundary.reset(new (std::nothrow) size_t[p->num_nodes]);
    if (!boundary)
      return pbqp_fail(error, PBQP_NO_MEMORY, 0, "allocation failed");
  }
  bool found;
  if (progress) {
    PercentProgress reporter(*p, progress, user_data, boundary.get());
    found = enumerate_assignments(*p, *solution, tuple.get(),
                                  [&](const size_t *completed) noexcept {
                                    reporter.completed(completed);
                                  });
  } else {
    // The no-op observer is compiled away when progress is disabled.
    found = enumerate_assignments(*p, *solution, tuple.get(),
                                  [](const size_t *) noexcept {});
  }
  if (!found)
    return pbqp_fail(error, PBQP_INFEASIBLE, 0, "no feasible tuple");
  solution->quality = PBQP_SOLUTION_OPTIMAL;
  *out = solution.release();
  return PBQP_OK;
}
} // extern "C"
