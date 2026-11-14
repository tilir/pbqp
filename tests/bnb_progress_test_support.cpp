/*
 * Private B&B progress filtering, cached bounds, and weight-transfer tests.
 *
 * Synthetic bounds expose a wide immediately pruned prefix followed by a
 * small surviving tree, without depending on PBQP arithmetic or wall clocks.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

module pbqp;
import :bnb_incumbent;
import :bnb_progress;
import :bnb_worker;
import :bnb_work_queue;
import :parallel;

namespace {
void check(bool condition,
           std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()));
}

struct ProgressLog {
  unsigned count = 0;
  bool invalid = false;
  static void report(unsigned percent, void *data) noexcept {
    auto &log = *static_cast<ProgressLog *>(data);
    log.invalid |= percent != ++log.count || percent > 100;
  }
};

class FilteredTree {
  std::vector<pbqp_cost_t> costs = std::vector<pbqp_cost_t>(100);
  std::vector<pbqp_node> nodes{{100, costs.data()},
                               {3, costs.data()},
                               {2, costs.data()},
                               {2, costs.data()}};
  pbqp_problem problem{nodes.size(), 0, nodes.data(), nullptr};
  bool forbidden;
  mutable std::vector<std::atomic<unsigned>> calls;

  size_t prefix_count() const noexcept {
    size_t total = 1, count = 1;
    for (const auto &node : nodes) {
      count *= node.num_options;
      total += count;
    }
    return total;
  }

public:
  struct Workspace {
    explicit Workspace(const FilteredTree &) {}
  };
  explicit FilteredTree(bool use_forbidden)
      : forbidden(use_forbidden), calls(prefix_count()) {}
  const pbqp_problem &input() const noexcept { return problem; }
  const pbqp_problem &domains() const noexcept { return problem; }
  size_t node(size_t step) const noexcept { return step; }
  size_t option(size_t, size_t rank) const noexcept { return rank; }
  bool lower_bound(const size_t *tuple, size_t depth, Workspace &,
                   pbqp_cost_t &bound) const noexcept {
    size_t offset = 0, count = 1, index = 0;
    for (size_t step = 0; step < depth; ++step) {
      offset += count;
      count *= nodes[step].num_options;
      index = index * nodes[step].num_options + tuple[step];
    }
    ++calls[offset + index];
    bound = depth == nodes.size() ? -2 : -3;
    if (depth && tuple[0] < 98) {
      bound = 10;
      return !forbidden;
    }
    return true;
  }
  bool checked_at_most_once() const {
    return std::all_of(calls.begin(), calls.end(),
                       [](const auto &count) { return count.load() <= 1; });
  }
};

void test_filtered_children() {
  for (bool forbidden : {false, true}) {
    for (size_t quota : {size_t{1}, size_t{2}, size_t{512}}) {
      FilteredTree bounds(forbidden);
      pbqp::BranchAndBoundIncumbent incumbent(4);
      const size_t seed[] = {98, 0, 0, 0};
      incumbent.update(seed, -1);
      pbqp::BranchAndBoundWorker worker(bounds, incumbent, true);
      worker.task.progress_weight = 1;
      ProgressLog log;
      pbqp::BranchAndBoundProgress progress(ProgressLog::report, &log);
      pbqp::BranchAndBoundProgress::Accumulator pending(progress);
      worker.start(-3);
      if (quota == 1) {
        // Root and the first 98 children: immediate INF or L >= G cuts have
        // no progress weight, even though they cover 98% of the tuple space.
        for (size_t step = 0; step < 99; ++step) {
          check(!worker.chunk(1, pending));
          check(log.count == 0);
        }
      }
      bool first = true;
      while (worker.active()) {
        worker.chunk(quota, pending);
        if (quota == 1 && log.count && first) {
          // The first real leaf carries 1 / (2 * 3 * 2 * 2), not 98%.
          check(log.count == 4);
          first = false;
        }
      }
      pending.flush();
      check(log.count == 99 && !log.invalid);
      check(bounds.checked_at_most_once());
      check(incumbent.result().cost == -2);
      progress.finish();
      check(log.count == 100 && !log.invalid);
    }
  }
}

void test_progress_completion() {
  ProgressLog log;
  pbqp::BranchAndBoundProgress progress(ProgressLog::report, &log);
  pbqp::BranchAndBoundProgress::Accumulator pending(progress);
  pending.discarded(0.75);
  check(log.count == 0);
  pending.completed(0.125);
  check(log.count == 50);
  pending.completed(0.125);
  check(log.count == 99);
  progress.finish();
  check(log.count == 100 && !log.invalid);
  progress.finish();
  check(log.count == 100 && !log.invalid);

  ProgressLog empty;
  pbqp::BranchAndBoundProgress closed(ProgressLog::report, &empty);
  pbqp::BranchAndBoundProgress::Accumulator rejected(closed);
  rejected.discarded(1);
  check(empty.count == 0);
  closed.finish();
  check(empty.count == 100 && !empty.invalid);
}

struct RecordingProgress {
  pbqp::BranchAndBoundProgress::Accumulator pending;
  double work = 0, skipped = 0;
  explicit RecordingProgress(pbqp::BranchAndBoundProgress &progress)
      : pending(progress) {}
  void completed(double weight) noexcept {
    work += weight;
    pending.completed(weight);
  }
  void discarded(double weight) noexcept {
    skipped += weight;
    pending.discarded(weight);
  }
};

void test_empty_jobs() {
  for (bool range : {false, true}) {
    FilteredTree bounds(true);
    pbqp::BranchAndBoundIncumbent incumbent(4);
    const size_t seed[] = {98, 0, 0, 0};
    incumbent.update(seed, range ? -1 : -3);
    pbqp::BranchAndBoundWorker worker(bounds, incumbent, true);
    worker.task.progress_weight = 1;
    if (range)
      worker.task.child_end = 98;
    ProgressLog log;
    pbqp::BranchAndBoundProgress progress(ProgressLog::report, &log);
    RecordingProgress observer(progress);
    worker.start(-3);
    while (!worker.chunk(1, observer)) {
    }
    observer.pending.flush();
    check(observer.skipped == 1 && observer.work == 0);
    check(log.count == 0);
    progress.finish();
    check(log.count == 100 && !log.invalid);
  }
}

void test_new_incumbent() {
  FilteredTree bounds(true);
  pbqp::BranchAndBoundIncumbent incumbent(4);
  const size_t seed[] = {98, 0, 0, 0};
  incumbent.update(seed, -1);
  pbqp::BranchAndBoundWorker worker(bounds, incumbent, true);
  worker.task.progress_weight = 1;
  ProgressLog log;
  pbqp::BranchAndBoundProgress progress(ProgressLog::report, &log);
  RecordingProgress observer(progress);
  worker.start(-3);
  // Cache both surviving children, then simulate a better incumbent arriving
  // before either is entered. These new immediate cuts are excluded too.
  check(!worker.chunk(101, observer));
  incumbent.update(seed, -4);
  while (!worker.chunk(1, observer)) {
  }
  observer.pending.flush();
  check(observer.skipped == 1 && observer.work == 0);
  check(log.count == 0 && bounds.checked_at_most_once());
  progress.finish();
  check(log.count == 100 && !log.invalid);
}

void test_progress_donation() {
  FilteredTree bounds(true);
  pbqp::BranchAndBoundIncumbent incumbent(4);
  const size_t seed[] = {98, 0, 0, 0};
  incumbent.update(seed, -1);
  pbqp::AdaptivePlan plan;
  plan.tasks.resize(1);
  plan.tasks[0].task.progress_weight = 1;
  pbqp::CoarseWorkQueue queue(4, 2, plan);
  std::vector<pbqp::BranchAndBoundWorker<FilteredTree>> workers;
  workers.reserve(2);
  for (size_t id = 0; id < 2; ++id)
    workers.emplace_back(bounds, incumbent, true);
  ProgressLog log;
  pbqp::BranchAndBoundProgress progress(ProgressLog::report, &log);
  std::vector<RecordingProgress> pending;
  pending.reserve(2);
  for (size_t id = 0; id < 2; ++id)
    pending.emplace_back(progress);
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
          while (!worker.chunk(1, pending[id]))
            if (worker.share(queue))
              ++donations[id];
          queue.completed();
          pending[id].pending.flush();
        }
      },
      [&]() noexcept { queue.cancel(); });
  check(donations[0] + donations[1] > 0);
  const double accounted = pending[0].work + pending[0].skipped +
                           pending[1].work + pending[1].skipped;
  check(std::abs(accounted - 1) < 1e-12);
  check(incumbent.result().cost == -2);
  check(log.count < 100 && !log.invalid);
  progress.finish();
  check(log.count == 100 && !log.invalid);
}
} // namespace

namespace pbqp {
extern "C++" void run_bnb_progress_tests() {
  test_filtered_children();
  test_progress_completion();
  test_empty_jobs();
  test_new_incumbent();
  test_progress_donation();
}
} // namespace pbqp
