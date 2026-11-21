/*
 * Cooperative deadlines across exact solvers, preparation, and worker shutdown.
 * Checks statuses and ownership rather than asserting machine-dependent timing.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iterator>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool condition,
           std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()));
}

struct Events {
  std::atomic<unsigned> percent{0};
  bool delayed = false;
};

void progress(unsigned percent, void *data) noexcept {
  static_cast<Events *>(data)->percent.store(percent);
}

void delay_preparation(const char *, void *data) noexcept {
  auto &events = *static_cast<Events *>(data);
  if (!events.delayed) {
    events.delayed = true;
    // Callback time counts against the deadline, but callbacks cannot be
    // forcibly interrupted. The next preparation check observes expiry.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void test_timeout(pbqp_solver solver, size_t profile, size_t threads, bool seed,
                  bool delay) {
  // Finite binary pair costs defeat the uniform-regalloc symmetry test.
  // Independent minima stay weak on this complete graph. Proving the optimum
  // requires far more work than the small budget on ordinary test machines.
  const pbqp_cost_t unary[] = {0, 0};
  const pbqp_cost_t matrix[] = {1, 0, 0, 1};
  // Keep the bucket diagnostics test within the dense-table budget.
  const size_t count = solver == PBQP_SOLVER_BUCKET && delay ? 4 : 45;
  std::vector<pbqp_node> nodes(count, pbqp_node{2, unary});
  std::vector<pbqp_edge> edges;
  for (size_t src = 0; src < nodes.size(); ++src)
    for (size_t tgt = src + 1; tgt < nodes.size(); ++tgt)
      edges.push_back({src, tgt, matrix});
  const pbqp_problem problem{nodes.size(), edges.size(), nodes.data(),
                             edges.data()};
  Events events;
  std::vector<pbqp_option> entries{
      pbqp_size_option("timeout-ms", delay ? 1 : 10),
      pbqp_size_option("num-threads", threads),
      pbqp_progress_option("progress",
                           seed || delay || threads == 4 ? progress : nullptr),
      pbqp_pointer_option("progress-data", &events),
      pbqp_diagnostic_option("diagnostics",
                             delay ? delay_preparation : nullptr),
      pbqp_pointer_option("diagnostic-data", &events),
      pbqp_bool_option("warnings-as-errors", 1)};
  if (solver == PBQP_SOLVER_BNB) {
    entries.push_back(pbqp_size_option("optimization-level", profile));
    entries.push_back(pbqp_bool_option("greedy-seed", seed));
    entries.push_back(pbqp_bool_option("reduction-seed", seed));
    if (!seed) {
      // One active subtree leaves other consumers waiting on the queue. Timeout
      // must wake them before joining, even with sibling donation disabled.
      entries.push_back(pbqp_bool_option("prefix-planning", false));
      entries.push_back(pbqp_bool_option("work-donation", false));
    }
  }
  const pbqp_solve_options options{entries.size(), entries.data()};
  pbqp_solution sentinel{};
  pbqp_solution *solution = &sentinel;
  pbqp_error error;
  check(pbqp_solve(solver, &problem, &options, &solution, &error) ==
        PBQP_TIMEOUT);
  check(!solution && error.line == 0 && error.message[0]);
  check(std::find(std::begin(error.message), std::end(error.message), '\0') !=
        std::end(error.message));
  check(events.percent < 100);
  if (delay)
    check(events.delayed && events.percent == 0);

  // Returning from timeout joins every worker and callback. The same borrowed
  // arrays may now be reused, and fallback has an independent owned result.
  check(pbqp_solve(PBQP_SOLVER_R1R2RN, &problem, nullptr, &solution, &error) ==
        PBQP_OK);
  check(solution && solution->quality == PBQP_SOLUTION_FEASIBLE);
  pbqp_evaluation evaluation{};
  check(pbqp_evaluate_assignment(&problem, solution->options,
                                 solution->num_nodes, &evaluation,
                                 &error) == PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == solution->cost);
  check(!error.line && !error.message[0]);
  pbqp_solution_free(solution);
}

void test_options(pbqp_solver solver) {
  const pbqp_problem empty{};
  pbqp_solution *solution = nullptr;
  pbqp_error error;
  for (size_t limit :
       {size_t{0}, size_t{1000}, std::numeric_limits<size_t>::max()}) {
    const pbqp_option entries[] = {
        pbqp_string_option("timeout-ms", "ignored earlier value"),
        pbqp_size_option("timeout-ms", limit),
        pbqp_bool_option("warnings-as-errors", 1)};
    const pbqp_solve_options options{3, entries};
    check(pbqp_solve(solver, &empty, &options, &solution, &error) == PBQP_OK);
    check(solution && solution->cost == 0 &&
          solution->quality == PBQP_SOLUTION_OPTIMAL);
    pbqp_solution_free(solution);
  }
  const pbqp_option wrong = pbqp_bool_option("timeout-ms", 1);
  const pbqp_solve_options invalid{1, &wrong};
  check(pbqp_solve(solver, &empty, &invalid, &solution, &error) ==
        PBQP_INVALID);
  check(!solution && error.message[0]);

  const pbqp_cost_t forbidden[] = {PBQP_INF};
  const pbqp_node node{1, forbidden};
  const pbqp_problem infeasible{1, 0, &node, nullptr};
  const pbqp_option budget = pbqp_size_option("timeout-ms", 1000);
  const pbqp_solve_options options{1, &budget};
  check(pbqp_solve(solver, &infeasible, &options, &solution, &error) ==
        PBQP_INFEASIBLE);
  check(!solution);
}

void test_heuristic_warning() {
  const pbqp_problem empty{};
  const pbqp_option entries[] = {pbqp_size_option("timeout-ms", 10),
                                 pbqp_bool_option("warnings-as-errors", 1)};
  const pbqp_solve_options options{2, entries};
  for (auto solver : {PBQP_SOLVER_GREEDY, PBQP_SOLVER_R1R2RN}) {
    pbqp_solution *solution = nullptr;
    pbqp_error error;
    check(pbqp_solve(solver, &empty, &options, &solution, &error) ==
          PBQP_INVALID);
    check(!solution &&
          std::string(error.message).find("timeout-ms") != std::string::npos);
  }
}
} // namespace

int main() {
  try {
    for (size_t threads : {size_t{1}, size_t{4}}) {
      test_timeout(PBQP_SOLVER_NAIVE, 0, threads, false, false);
      for (size_t profile : {size_t{0}, size_t{1}, size_t{2}}) {
        test_timeout(PBQP_SOLVER_BNB, profile, threads, true, false);
        test_timeout(PBQP_SOLVER_BNB, profile, threads, false, false);
      }
    }
    for (auto solver :
         {PBQP_SOLVER_NAIVE, PBQP_SOLVER_BNB, PBQP_SOLVER_BUCKET}) {
      test_timeout(solver, 2, 4, false, true);
      test_options(solver);
    }
    test_heuristic_warning();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
