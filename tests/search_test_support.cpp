/*
 * Private-module preparation and prefix-frontier regressions.
 *
 * Tests implementation invariants without exporting internal names or changing
 * the C ABI. A test-only bridge uses global-module C++ linkage; this unit is
 * compiled with the library so private partitions remain private.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

module pbqp;
import :branch_and_bound;
import :problem;
import :search_tasks;
import :bnb_work_queue;
import :bnb_worker;
import :bnb_bounds;
import :bnb_tasks;
import :bnb_incumbent;
import :search_progress;
import :parallel;

namespace {
void check(bool condition,
           std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()));
}

void test_singleton_frontier() {
  pbqp_cost_t costs[] = {0, 0};
  std::vector<pbqp_node> nodes(1024, pbqp_node{1, costs});
  nodes.insert(nodes.end(), 8, pbqp_node{2, costs});
  pbqp_problem problem{nodes.size(), 0, nodes.data(), nullptr};
  auto plan = pbqp::plan_prefixes(
      problem, 4, [](const pbqp::PrefixTask &) { return true; });
  check(plan.tasks.size() >= 4);
  for (const auto &task : plan.tasks)
    check(task.ranks.size() > 1024);

  // The compressed run is checked once rather than copying and filtering each
  // singleton prefix. Rejecting it still accounts for the entire subtree.
  size_t calls = 0;
  auto closed = pbqp::plan_prefixes(problem, 4, [&](const pbqp::PrefixTask &p) {
    ++calls;
    return p.ranks.empty();
  });
  check(calls == 2 && closed.tasks.empty());
  check(closed.covered_depths == std::vector<size_t>{1024});
  pbqp::BranchAndBoundBounds bounds(problem);
  pbqp::BranchAndBoundIncumbent incumbent(problem.num_nodes);
  pbqp::PrefixPlanner<pbqp::BranchAndBoundBounds> planner(bounds, incumbent);
  auto bounded = planner.build(pbqp::BranchAndBoundSettings(4));
  check(!bounded.tasks.empty() && bounded.calls <= 512);
  if (bounded.calls > 1)
    for (const auto &prefix : bounded.tasks)
      check(prefix.task.ranks.size() >= 1024);
}

struct Progress {
  unsigned count = 0;
  bool invalid = false;
};
void report(unsigned percent, void *data) noexcept {
  auto &progress = *static_cast<Progress *>(data);
  progress.invalid |= percent != ++progress.count || percent > 100;
}

void check_seed_policy(const pbqp_problem &problem, bool greedy, bool reduction,
                       pbqp_status expected, pbqp_cost_t cost = 0) {
  pbqp::validate(&problem);
  Progress progress;
  pbqp::BranchAndBoundSearch search(problem, 4, report, &progress);
  search.prepare();
  check(search.seed_statistics().greedy == greedy);
  check(search.seed_statistics().reduction == reduction);
  check(progress.count == 0);
  check(search.run(nullptr) == expected);
  check(progress.count == 100 && !progress.invalid);
  if (expected == PBQP_OK) {
    auto deleter = [](pbqp_solution *s) noexcept { pbqp_solution_free(s); };
    std::unique_ptr<pbqp_solution, decltype(deleter)> solution(
        search.release_solution(), deleter);
    check(solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == cost);
  }
}

void test_root_seed_policy() {
  for (size_t domain : {size_t{64}, size_t{128}, size_t{256}}) {
    std::vector<pbqp_cost_t> unary(domain);
    std::vector<pbqp_cost_t> matrix(domain * domain);
    pbqp_node nodes[] = {
        {domain, unary.data()}, {domain, unary.data()}, {domain, unary.data()}};
    pbqp_edge edges[] = {{0, 1, matrix.data()}, {0, 2, matrix.data()}};
    pbqp_problem problem{3, 2, nodes, edges};
    // Greedy and the root bound prove zero; no cubic R2 seed is needed.
    check_seed_policy(problem, true, false, PBQP_OK);
    // Linear input must not trigger a quadratic R2 fill allocation either.
    nodes[0].num_options = 1;
    check_seed_policy(problem, true, false, PBQP_OK);
  }
  pbqp_cost_t zero[] = {0, 0}, forbidden[] = {PBQP_INF};
  pbqp_node blocked_nodes[] = {{1, zero}, {1, zero}};
  pbqp_edge blocked_edge{0, 1, forbidden};
  pbqp_problem blocked{2, 1, blocked_nodes, &blocked_edge};
  check_seed_policy(blocked, false, false, PBQP_INFEASIBLE);

  pbqp_node nodes[] = {{2, zero}, {2, zero}, {2, zero}};
  pbqp_cost_t equal[] = {0, 1, 1, 0}, unequal[] = {1, 0, 0, 1};
  pbqp_edge edges[] = {{0, 1, equal}, {0, 2, unequal}, {1, 2, equal}};
  pbqp_problem loose{3, 3, nodes, edges};
  check_seed_policy(loose, true, true, PBQP_OK, 1);
}
void test_dispatch_failure() {
  pbqp::AdaptivePlan plan;
  plan.tasks.push_back({});
  pbqp::CoarseWorkQueue queue(1, 2, plan);
  pbqp::PrefixTask held;
  held.ranks.reserve(1);
  pbqp_cost_t bound;
  check(queue.take(held, bound));
  std::vector<pbqp::PrefixTask> waiting(2);
  for (auto &task : waiting)
    task.ranks.reserve(1);
  bool caught = false;
  try {
    pbqp::run_parallel(
        2,
        [&](size_t id) {
          if (!id)
            throw std::bad_alloc{};
          pbqp_cost_t cost;
          check(!queue.take(waiting[id], cost));
        },
        [&]() noexcept { queue.cancel(); });
  } catch (const std::bad_alloc &) {
    caught = true;
  }
  check(caught && queue.cancelled());
  std::atomic<size_t> calls{0};
  pbqp::run_parallel(2, [&](size_t) { ++calls; });
  check(calls == 2);
}

void test_runtime_donation() {
  if (pbqp::parallel_concurrency(2) < 2)
    return;
  pbqp_cost_t unary[] = {1, 0, 0};
  pbqp_cost_t matrix[] = {0, 0, 0, 0, PBQP_INF, 0, 0, 0, PBQP_INF};
  std::vector<pbqp_node> nodes(20, pbqp_node{3, unary});
  std::vector<pbqp_edge> edges;
  for (size_t base = 0; base < nodes.size(); base += 5)
    for (size_t v = 0; v < 5; ++v) {
      size_t a = base + v, b = base + (v + 1) % 5;
      edges.push_back({std::min(a, b), std::max(a, b), matrix});
    }
  pbqp_problem problem{nodes.size(), edges.size(), nodes.data(), edges.data()};
  pbqp::BranchAndBoundBounds bounds(problem);
  pbqp::BranchAndBoundIncumbent incumbent(nodes.size());
  pbqp::AdaptivePlan plan;
  plan.tasks.push_back({});
  pbqp::CoarseWorkQueue queue(nodes.size(), 2, plan);
  std::vector<pbqp::BranchAndBoundWorker<pbqp::BranchAndBoundBounds>> workers;
  workers.reserve(2);
  for (size_t id = 0; id < 2; ++id)
    workers.emplace_back(bounds, incumbent);
  struct CallbackLog {
    std::atomic<unsigned> count{0};
    std::atomic<bool> invalid{false}, entered{false};
  } log;
  auto callback = [](unsigned value, void *data) noexcept {
    auto &p = *static_cast<CallbackLog *>(data);
    if (p.entered.exchange(true))
      p.invalid = true;
    if (value != p.count.fetch_add(1) + 1 || value > 100)
      p.invalid = true;
    p.entered = false;
  };
  pbqp::SubtreeProgress progress(problem, callback, &log);
  std::vector<pbqp::SubtreeProgress::Accumulator> accumulators;
  accumulators.reserve(2);
  for (size_t id = 0; id < 2; ++id)
    accumulators.emplace_back(progress);
  std::vector<size_t> donations(2);
  std::atomic<bool> first{true};
  pbqp::run_parallel(
      2,
      [&](size_t id) {
        auto &worker = workers[id];
        pbqp_cost_t bound;
        while (queue.take(worker.task, bound)) {
          if (first.exchange(false))
            while (!queue.wants_work() && !queue.cancelled())
              std::this_thread::yield();
          worker.start(bound);
          while (!queue.cancelled()) {
            if (worker.chunk(1, [&](size_t depth) noexcept {
                  accumulators[id].completed_prefix(depth);
                })) {
              queue.completed();
              accumulators[id].flush();
              break;
            }
            if (worker.share(queue))
              ++donations[id];
          }
        }
      },
      [&]() noexcept { queue.cancel(); });
  check(donations[0] + donations[1] > 0);
  check(incumbent.found() && incumbent.result().cost == 4);
  check(log.count == 100 && !log.invalid);
  pbqp_evaluation evaluation;
  check(pbqp_evaluate_assignment(&problem, incumbent.result().options,
                                 nodes.size(), &evaluation,
                                 nullptr) == PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == 4);
}

} // namespace

namespace pbqp {
extern "C++" void run_search_internal_tests() {
  test_dispatch_failure();
  test_runtime_donation();
  test_singleton_frontier();
  test_root_seed_policy();
}
} // namespace pbqp
