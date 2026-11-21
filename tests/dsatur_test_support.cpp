/*
 * Private DSATUR budget and cached-model regressions without exported modules.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <chrono>
#include <stdexcept>
#include <thread>
module pbqp;
import :dsatur;
import :interference;
import :deadline;
import :evaluation;

namespace pbqp {
extern "C++" void run_dsatur_internal_tests() {
  const pbqp_cost_t costs[] = {1, 0};
  const pbqp_node node{2, costs};
  const pbqp_problem problem{1, 0, &node, nullptr};
  DsaturSettings settings;
  settings.work_budget = 0;
  DsaturSearch search(problem, settings);
  if (!search.prepare(check_interference_structure(problem)))
    throw std::runtime_error("DSATUR budget test preparation failed");
  search.run();
  if (search.work_used() || search.has_solution() ||
      search.status() != DsaturState::WorkBudget)
    throw std::runtime_error("DSATUR zero work budget produced a candidate");
  DsaturSearch expired(problem, {}, Deadline(1));
  expired.prepare(check_interference_structure(problem));
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  expired.run();
  if (expired.has_solution() || expired.status() != DsaturState::Expired)
    throw std::runtime_error("DSATUR ignored its enclosing deadline");

  const pbqp_node nodes[] = {{2, costs}, {2, costs}, {2, costs}};
  const pbqp_cost_t matrix[] = {0, 0, 0, PBQP_INF};
  const pbqp_edge edges[] = {{0, 1, matrix}, {1, 2, matrix}, {0, 2, matrix}};
  const pbqp_problem triangle{3, 3, nodes, edges};
  settings.work_budget = 64;
  DsaturSearch bounded(triangle, settings);
  bounded.prepare(check_interference_structure(triangle));
  bounded.run();
  pbqp_cost_t cost;
  if (bounded.work_used() > settings.work_budget || !bounded.has_solution() ||
      !evaluate_assignment(triangle, bounded.result().options, cost) ||
      cost != bounded.result().cost)
    throw std::runtime_error(
        "DSATUR did not retain its complete budgeted candidate");
}
} // namespace pbqp
