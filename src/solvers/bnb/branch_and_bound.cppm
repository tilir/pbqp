/*
 * Synchronous exact-search orchestration with budgeted coarse work sharing.
 * Preparation owns immutable bounds, private DFS workspaces, and a bounded
 * priority queue. oneTBB consumers donate siblings to waiting consumers only
 * between DFS chunks. Approximate progress weights only branches surviving
 * immediate cuts.
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
import :bnb_work_queue;
import :bnb_worker;
import :bnb_tasks;
import :bnb_progress;
import :parallel;
import :error;
import :evaluation;
import :greedy;
import :reduction;
import :bnbopt_cover;
import :diagnostics;

namespace pbqp {
// Called only after preparation, before workers start. Cached frontier bounds
// cover every remaining subtree, including unchecked child ranges. Closed
// prefixes are infeasible, exhausted, or no better than the current incumbent.
// Capping the frontier minimum by a feasible incumbent therefore bounds the
// global optimum, rather than only the best remaining unexplored assignment.
void describe_preliminary_bounds(const AdaptivePlan &plan,
                                 const BranchAndBoundIncumbent &incumbent,
                                 Diagnostics log) {
  if (!log)
    return;
  if (plan.tasks.empty() && !incumbent.found()) {
    log.line("search: preliminary bounds: infeasible");
    return;
  }
  pbqp_cost_t lower = PBQP_INF;
  for (const auto &prefix : plan.tasks)
    lower = std::min(lower, prefix.bound);
  if (incumbent.found()) {
    const auto upper = incumbent.result().cost;
    lower = std::min(lower, upper);
    log.line("search: preliminary bounds: lower=", lower, "; upper=", upper);
  } else {
    log.line("search: preliminary bounds: lower=", lower,
             "; upper=none (no feasible solution yet)");
  }
}

template <typename Bounds = BranchAndBoundBounds> class BranchAndBoundSearch {
public:
  struct SeedStatistics {
    bool greedy = false, reduction = false, cover = false;
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
  std::unique_ptr<BranchAndBoundProgress> progress;
  std::vector<BranchAndBoundProgress::Accumulator> progress_workers;
  SeedStatistics seeds;

  void seed_incumbent(pbqp_cost_t root_bound);
  void seed_cover(pbqp_cost_t root_bound);
  void prepare_plan();

  void finish(size_t index) {
    queue->completed();
    if (progress)
      progress_workers[index].flush();
  }

  template <typename Observer> void consume(size_t index, Observer &&observe) {
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

template <typename Bounds>
void BranchAndBoundSearch<Bounds>::seed_cover(pbqp_cost_t root_bound) {
  if constexpr (requires { bounds.patterns(); }) {
    if (!bounds.patterns().complete() || bounds.patterns().empty())
      return;
    seeds.cover = true;
    diagnostics.line("bnbopt: catalogue seed limits: restarts=",
                     RegisterCoverSeed::restart_limit,
                     "; pattern-vertex work=", RegisterCoverSeed::work_budget,
                     "; pair candidates=", RegisterCoverSeed::pair_candidates);
    RegisterCoverSeed cover(bounds.input(), bounds.patterns());
    if (!cover.run(root_bound)) {
      if (!cover.enabled())
        diagnostics.line("bnbopt: catalogue seed disabled: finite nonnegative "
                         "spills required");
      else
        diagnostics.line("bnbopt: catalogue seed unavailable: work budget "
                         "exhausted before a complete candidate");
      return;
    }
    const auto &seed = cover.solution();
    incumbent.update(seed.options, seed.cost);
    diagnostics.line("bnbopt: catalogue seed: cost=", seed.cost,
                     "; restarts=", cover.restarts_used(),
                     "; pattern-vertex work=", cover.work_used(), "/",
                     RegisterCoverSeed::work_budget);
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
    typename Bounds::Workspace capacity(bounds);
    if (!bounds.lower_bound(nullptr, 0, capacity, lower) ||
        !incumbent.can_improve(lower)) {
      diagnostics.line(
          "search: capacity bound closes root; further seeds unnecessary");
      plan.covered_depths.push_back(0);
      return;
    }
    seed_cover(lower);
    if (!incumbent.can_improve(lower)) {
      plan.covered_depths.push_back(0);
      return;
    }
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
    progress = std::make_unique<BranchAndBoundProgress>(callback, user_data);
  if (progress && !plan.tasks.empty()) {
    const double weight = 1.0 / plan.tasks.size();
    for (auto &prefix : plan.tasks)
      prefix.task.progress_weight = weight;
  }
  size_t count = plan.tasks.empty() ? 0 : num_threads;
  queue = std::make_unique<CoarseWorkQueue>(
      count ? bounds.input().num_nodes : 0, count, plan);
  diagnostics.line("search: prepared workers=", count,
                   "; initial jobs=", plan.tasks.size());
  workers.reserve(count);
  if (progress)
    progress_workers.reserve(count);
  for (size_t id = 0; id < count; ++id) {
    workers.emplace_back(bounds, incumbent, bool(progress));
    if (progress)
      progress_workers.emplace_back(*progress);
  }
  describe_preliminary_bounds(plan, incumbent, diagnostics);
}

template <typename Bounds>
pbqp_status BranchAndBoundSearch<Bounds>::run(pbqp_error *error) {
  run_parallel(
      workers.size(),
      [&](size_t id) {
        if (progress)
          consume(id, progress_workers[id]);
        else
          consume(id, [](size_t) noexcept {});
      },
      [&]() noexcept { queue->cancel(); });
  if (!incumbent.found()) {
    if (progress)
      progress->finish();
    return set_error(error, PBQP_INFEASIBLE, 0, "no feasible tuple");
  }
  auto &solution = incumbent.result();
  if (!evaluate_assignment(bounds.input(), solution.options, solution.cost))
    return set_error(error, PBQP_INVALID, 0,
                     "branch-and-bound produced a forbidden assignment");
  solution.quality = PBQP_SOLUTION_OPTIMAL;
  if (progress)
    progress->finish();
  return PBQP_OK;
}
} // namespace pbqp
