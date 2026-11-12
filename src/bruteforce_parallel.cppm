/*
 * Parallel exhaustive tuple enumeration with lexicographic tie preservation.
 *
 * Workers own tuple and best-assignment buffers, evaluating every tuple in
 * reference order without allocation. Coarse prefix jobs use the shared TBB
 * dispatcher. A final deterministic reduction selects the first global minimum.
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

module pbqp:bruteforce_parallel;
import :error;
import :evaluation;
import :ownership;
import :parallel;
import :search_progress;
import :search_tasks;

namespace pbqp {
class BruteForceWorker {
  const pbqp_problem &problem;
  std::vector<size_t> tuple;
  SolutionPtr best;
  bool feasible = false;

  bool advance(size_t root) noexcept {
    for (size_t id = tuple.size(); id > root; --id) {
      if (++tuple[id - 1] < problem.nodes[id - 1].num_options)
        return true;
      tuple[id - 1] = 0;
    }
    return false;
  }
  template <class Observer>
  void enumerate(size_t root, Observer observe) noexcept {
    std::fill(tuple.begin() + root, tuple.end(), size_t{0});
    do {
      pbqp_cost_t cost;
      if (evaluate_assignment(problem, tuple.data(), cost) &&
          (!feasible || cost < best->cost)) {
        feasible = true;
        best->cost = cost;
        if (!tuple.empty())
          std::copy(tuple.begin(), tuple.end(), best->options);
      }
      observe();
    } while (advance(root));
  }

public:
  explicit BruteForceWorker(const pbqp_problem &p)
      : problem(p), tuple(p.num_nodes), best(make_solution(p.num_nodes)) {}
  bool found() const noexcept { return feasible; }
  const pbqp_solution &result() const noexcept { return *best; }
  template <class Observer>
  void search(const PrefixTask &task, Observer observe) noexcept {
    size_t root = task.ranks.size();
    std::copy(task.ranks.begin(), task.ranks.end(), tuple.begin());
    if (task.child_begin == task.child_end) {
      enumerate(root, observe);
      return;
    }
    for (size_t option = task.child_begin; option < task.child_end; ++option) {
      tuple[root] = option;
      enumerate(root + 1, observe);
    }
  }
};

class ParallelBruteForceSearch {
  const pbqp_problem &problem;
  size_t num_threads;
  PrefixPlan plan;
  PrefixQueue queue;
  std::vector<BruteForceWorker> workers;
  SolutionPtr solution;
  std::unique_ptr<SubtreeProgress> progress;
  std::vector<SubtreeProgress::Accumulator> progress_workers;

  template <class Observer, class Completed>
  void consume_tasks(size_t id, Observer observe,
                     Completed completed) noexcept {
    size_t task;
    while (queue.claim(plan.tasks.size(), task)) {
      workers[id].search(plan.tasks[task], observe);
      completed();
    }
  }
  void consume(size_t id) noexcept {
    if (progress) {
      auto &local = progress_workers[id];
      consume_tasks(
          id, [&]() noexcept { local.completed_prefix(problem.num_nodes); },
          [&]() noexcept { local.flush(); });
    } else
      consume_tasks(id, []() noexcept {}, []() noexcept {});
  }
  bool precedes(const pbqp_solution &candidate) const noexcept {
    return problem.num_nodes &&
           std::lexicographical_compare(
               candidate.options, candidate.options + problem.num_nodes,
               solution->options, solution->options + problem.num_nodes);
  }
  bool collect() noexcept;

public:
  ParallelBruteForceSearch(const pbqp_problem &p, size_t threads,
                           pbqp_progress_callback callback, void *data)
      : problem(p), num_threads(threads), solution(make_solution(p.num_nodes)) {
    if (callback)
      progress = std::make_unique<SubtreeProgress>(p, callback, data);
  }
  void prepare();
  pbqp_status run(pbqp_error *error);
  pbqp_solution *release_solution() noexcept { return solution.release(); }
};

void ParallelBruteForceSearch::prepare() {
  plan = plan_prefixes(problem, num_threads,
                       [](const PrefixTask &) { return true; });
  // Each consumer receives increasing queue indices. Ordered disjoint prefixes
  // therefore preserve lexicographic order within every worker, including ties.
  std::sort(plan.tasks.begin(), plan.tasks.end(),
            [](const PrefixTask &a, const PrefixTask &b) {
              if (a.ranks != b.ranks)
                return a.ranks < b.ranks;
              return a.child_begin < b.child_begin;
            });
  size_t count = std::min(num_threads, plan.tasks.size());
  if (progress) {
    progress_workers.reserve(count);
    for (size_t id = 0; id < count; ++id)
      progress_workers.emplace_back(*progress);
  }
  workers.reserve(count);
  for (size_t id = 0; id < count; ++id)
    workers.emplace_back(problem);
}

bool ParallelBruteForceSearch::collect() noexcept {
  bool found = false;
  for (const auto &worker : workers) {
    if (!worker.found())
      continue;
    const auto &candidate = worker.result();
    if (!found || candidate.cost < solution->cost ||
        (candidate.cost == solution->cost && precedes(candidate))) {
      found = true;
      solution->cost = candidate.cost;
      if (problem.num_nodes)
        std::copy_n(candidate.options, problem.num_nodes, solution->options);
    }
  }
  return found;
}

pbqp_status ParallelBruteForceSearch::run(pbqp_error *error) {
  run_parallel(workers.size(), [&](size_t id) noexcept { consume(id); });
  if (!collect())
    return set_error(error, PBQP_INFEASIBLE, 0, "no feasible tuple");
  solution->quality = PBQP_SOLUTION_OPTIMAL;
  return PBQP_OK;
}
} // namespace pbqp
