/*
 * Sequential and oneTBB prefix-task orchestration for exact PBQP search.
 *
 * Preparation owns immutable bounds, a shared incumbent, and private workers.
 * A bounded-depth mixed-radix queue describes whole prefix subtrees without
 * allocating a task per search node. The default path runs directly on one CPU.
 * The bounds strategy supplies original input, enumeration domains/order, and
 * private workspace. Baseline and optimized B&B share identical result and
 * progress semantics; exhaustive lexicographic winner selection stays separate.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <memory>
#include <vector>

module pbqp:branch_and_bound;
import :bnb_bounds;
import :bnb_incumbent;
import :search_progress;
import :search_tasks;
import :parallel;
import :bnb_worker;
import :bnb_tasks;
import :error;
import :evaluation;
import :greedy;
import :reduction;

namespace pbqp {
template <class Bounds = BranchAndBoundBounds> class BranchAndBoundSearch {
public:
  struct SeedStatistics {
    bool greedy = false;
    bool reduction = false;
  };

private:
  Bounds bounds;
  BranchAndBoundIncumbent incumbent;
  size_t num_threads;
  PrefixPlan plan;
  PrefixQueue queue;
  std::vector<BranchAndBoundWorker<Bounds>> workers;
  pbqp_progress_callback callback;
  void *user_data;
  std::unique_ptr<SubtreeProgress> progress;
  std::vector<SubtreeProgress::Accumulator> progress_workers;
  SeedStatistics seeds;

  void seed_incumbent(pbqp_cost_t root_bound);
  void prepare_plan();
  void consume(size_t index) noexcept;
  template <class Observer, class Completed>
  void consume_tasks(size_t index, Observer observe,
                     Completed completed) noexcept {
    size_t task;
    while (queue.claim(plan.tasks.size(), task)) {
      workers[index].search(plan.tasks[task], observe);
      completed();
    }
  }

public:
  BranchAndBoundSearch(const pbqp_problem &p, size_t threads,
                       pbqp_progress_callback callback, void *data)
      : bounds(p), incumbent(p.num_nodes), num_threads(threads),
        callback(callback), user_data(data) {}
  void prepare();
  const SeedStatistics &seed_statistics() const noexcept { return seeds; }
  pbqp_status run(pbqp_error *error);
  pbqp_solution *release_solution() noexcept {
    return incumbent.release_solution();
  }
};

template <class Bounds>
void BranchAndBoundSearch<Bounds>::consume(size_t index) noexcept {
  if (progress) {
    auto &local = progress_workers[index];
    consume_tasks(
        index, [&](size_t depth) noexcept { local.completed_prefix(depth); },
        [&]() noexcept { local.flush(); });
  } else
    consume_tasks(index, [](size_t) noexcept {}, []() noexcept {});
}

template <class Bounds>
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

template <class Bounds> void BranchAndBoundSearch<Bounds>::prepare_plan() {
  typename Bounds::Workspace root_workspace(bounds);
  pbqp_cost_t root_bound;
  if (!bounds.lower_bound(nullptr, 0, root_workspace, root_bound)) {
    plan.covered_depths.push_back(0);
    return;
  }
  seed_incumbent(root_bound);
  if (!incumbent.can_improve(root_bound)) {
    plan.covered_depths.push_back(0);
    return;
  }
  bounds.prepare_search();
  PrefixPlanner<Bounds> planner(bounds, incumbent);
  plan = planner.build(num_threads);
}

template <class Bounds> void BranchAndBoundSearch<Bounds>::prepare() {
  prepare_plan();
  if (callback)
    progress = std::make_unique<SubtreeProgress>(bounds.domains(), callback,
                                                 user_data);
  size_t count = std::min(num_threads, plan.tasks.size());
  if (progress) {
    progress_workers.reserve(count);
    for (size_t id = 0; id < count; ++id)
      progress_workers.emplace_back(*progress);
  }
  workers.reserve(count);
  for (size_t id = 0; id < count; ++id)
    workers.emplace_back(bounds, incumbent);
}

template <class Bounds>
pbqp_status BranchAndBoundSearch<Bounds>::run(pbqp_error *error) {
  if (progress)
    for (size_t depth : plan.covered_depths)
      progress->completed_prefix(depth);
  run_parallel(workers.size(), [&](size_t index) noexcept { consume(index); });
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
