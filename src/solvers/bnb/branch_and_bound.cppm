/*
 * Synchronous exact-search orchestration with budgeted coarse work sharing.
 * Preparation owns immutable bounds, private DFS workspaces, and a bounded
 * priority queue. oneTBB consumers donate siblings to waiting consumers only
 * between DFS chunks. Progress counts each closed prefix exactly once.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <memory>
#include <vector>
module pbqp:branch_and_bound;
import :bnb_bounds;
import :bnb_incumbent;
import :bnb_work_queue;
import :bnb_worker;
import :bnb_tasks;
import :search_progress;
import :parallel;
import :error;
import :evaluation;
import :greedy;
import :reduction;
import :diagnostics;
namespace pbqp {
template <typename Bounds = BranchAndBoundBounds> class BranchAndBoundSearch {
public:
  struct SeedStatistics {
    bool greedy = false, reduction = false;
  };

private:
  Bounds bounds;
  BranchAndBoundIncumbent incumbent;
  size_t requested_threads, num_threads;
  Diagnostics diagnostics;
  BranchAndBoundSettings settings;
  AdaptivePlan plan;
  std::unique_ptr<CoarseWorkQueue> queue;
  std::vector<BranchAndBoundWorker<Bounds>> workers;
  pbqp_progress_callback callback;
  void *user_data;
  std::unique_ptr<SubtreeProgress> progress;
  std::vector<SubtreeProgress::Accumulator> progress_workers;
  SeedStatistics seeds;

  void seed_incumbent(pbqp_cost_t root_bound);
  void prepare_plan();
  void finish(size_t index) {
    queue->completed();
    if (progress)
      progress_workers[index].flush();
  }
  template <typename Observer> void consume(size_t index, Observer observe) {
    auto &worker = workers[index];
    pbqp_cost_t bound;
    while (queue->take(worker.task, bound)) {
      worker.start(bound);
      while (!queue->cancelled()) {
        if (worker.chunk(BranchAndBoundSettings::quantum, observe)) {
          finish(index);
          break;
        }
        if (num_threads > 1)
          worker.share(*queue);
      }
    }
  }

public:
  BranchAndBoundSearch(const pbqp_problem &p, size_t threads,
                       pbqp_progress_callback cb, void *data,
                       Diagnostics log = {})
      : bounds(p), incumbent(p.num_nodes), requested_threads(threads),
        num_threads(parallel_concurrency(threads)), diagnostics(log),
        settings(num_threads), callback(cb), user_data(data) {}
  const SeedStatistics &seed_statistics() const noexcept { return seeds; }
  void prepare();
  pbqp_status run(pbqp_error *error);
  pbqp_solution *release_solution() noexcept {
    return incumbent.release_solution();
  }
};

template <typename Bounds>
void BranchAndBoundSearch<Bounds>::seed_incumbent(pbqp_cost_t root_bound) {
  const auto &problem = bounds.input();
  seeds.greedy = true;
  GreedySearch greedy(problem);
  if (greedy.prepare(nullptr) == PBQP_OK && greedy.run(nullptr) == PBQP_OK) {
    const auto &seed = greedy.get_solution();
    incumbent.update(seed.options, seed.cost);
  }
  if (!incumbent.can_improve(root_bound))
    return;
  seeds.reduction = true;
  ReductionSearch reduction(problem);
  reduction.prepare();
  if (reduction.run(nullptr) == PBQP_OK) {
    const auto &seed = reduction.get_solution();
    incumbent.update(seed.options, seed.cost);
  }
}

template <typename Bounds> void BranchAndBoundSearch<Bounds>::prepare_plan() {
  diagnostics.line(
      "search: requested threads=", requested_threads,
      "; available concurrency=", num_threads,
      "; planner target jobs=", settings.jobs,
      "; bound-check budget=", BranchAndBoundSettings::planning_calls,
      "; planning time budget ms=", BranchAndBoundSettings::planning_ms,
      "; DFS quantum=", BranchAndBoundSettings::quantum);
  if constexpr (requires { bounds.describe_input(diagnostics); })
    bounds.describe_input(diagnostics);
  typename Bounds::Workspace initial(bounds);
  pbqp_cost_t lower;
  if (!bounds.lower_bound(nullptr, 0, initial, lower)) {
    diagnostics.line("search: initial bound proves infeasibility; later "
                     "preparation unnecessary");
    plan.covered_depths.push_back(0);
    return;
  }
  seed_incumbent(lower);
  if (!incumbent.can_improve(lower)) {
    diagnostics.line("search: initial bound and heuristic seed prove "
                     "optimality; later preparation unnecessary");
    plan.covered_depths.push_back(0);
    return;
  }
  bounds.prepare_search();
  if constexpr (requires { bounds.describe_structure(diagnostics); })
    bounds.describe_structure(diagnostics);
  if constexpr (requires { bounds.prepare_capacity(); }) {
    // Cheap structural bounds may already prove the heuristic seed optimal.
    // Keep expensive complete-pattern enumeration and pricing behind that test.
    typename Bounds::Workspace structural(bounds);
    if (!bounds.lower_bound(nullptr, 0, structural, lower) ||
        !incumbent.can_improve(lower)) {
      diagnostics.line("search: structural bound closes root; capacity "
                       "preparation unnecessary");
      plan.covered_depths.push_back(0);
      return;
    }
    bounds.prepare_capacity(diagnostics);
  }
  PrefixPlanner<Bounds> planner(bounds, incumbent);
  plan = planner.build(settings);
  diagnostics.line("search: planned jobs=", plan.tasks.size(),
                   "; bound checks=", plan.calls,
                   "; closed planning prefixes=", plan.covered_depths.size());
  if (plan.call_budget_hit)
    diagnostics.line(
        "search: planning bound-check budget reached; unchecked ranges queued");
  if (plan.time_budget_hit)
    diagnostics.line(
        "search: planning time budget reached; unchecked ranges queued");
}

template <typename Bounds> void BranchAndBoundSearch<Bounds>::prepare() {
  prepare_plan();
  if (callback)
    progress = std::make_unique<SubtreeProgress>(bounds.domains(), callback,
                                                 user_data);
  size_t count = plan.tasks.empty() ? 0 : num_threads;
  queue = std::make_unique<CoarseWorkQueue>(
      count ? bounds.input().num_nodes : 0, count, plan);
  diagnostics.line("search: prepared workers=", count,
                   "; initial jobs=", plan.tasks.size());
  workers.reserve(count);
  if (progress)
    progress_workers.reserve(count);
  for (size_t id = 0; id < count; ++id) {
    workers.emplace_back(bounds, incumbent);
    if (progress)
      progress_workers.emplace_back(*progress);
  }
}

template <typename Bounds>
pbqp_status BranchAndBoundSearch<Bounds>::run(pbqp_error *error) {
  if (progress)
    for (size_t depth : plan.covered_depths)
      progress->completed_prefix(depth);
  run_parallel(
      workers.size(),
      [&](size_t id) {
        if (progress)
          consume(id, [&](size_t depth) noexcept {
            progress_workers[id].completed_prefix(depth);
          });
        else
          consume(id, [](size_t) noexcept {});
      },
      [&]() noexcept { queue->cancel(); });
  if (!incumbent.found())
    return set_error(error, PBQP_INFEASIBLE, 0, "no feasible tuple");
  auto &solution = incumbent.result();
  if (!evaluate_assignment(bounds.input(), solution.options, solution.cost))
    return set_error(error, PBQP_INVALID, 0,
                     "branch-and-bound produced a forbidden assignment");
  solution.quality = PBQP_SOLUTION_OPTIMAL;
  return PBQP_OK;
}
} // namespace pbqp
