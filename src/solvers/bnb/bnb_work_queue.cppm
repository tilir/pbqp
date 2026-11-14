/*
 * Bounded priority work queue for coarse branch-and-bound prefixes.
 * Prefix storage is prepared once. Mutex publication protects jobs; an atomic
 * waiter count is only a hint for optional donation between DFS chunks.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <vector>
module pbqp:bnb_work_queue;
import :search_tasks;
import :failure;

namespace pbqp {
struct BranchAndBoundSettings {
  static constexpr size_t planning_calls = 512;
  static constexpr size_t planning_ms = 25;
  static constexpr size_t quantum = 512;
  size_t jobs;

  explicit BranchAndBoundSettings(size_t threads)
      : jobs(std::max(size_t{32}, 2 * threads)) {}
};

struct RankedPrefix {
  PrefixTask task;
  pbqp_cost_t bound = 0;
  size_t sequence = 0;
};

bool prefix_preferred(pbqp_cost_t a, size_t ai, pbqp_cost_t b,
                      size_t bi) noexcept {
  if (std::isnan(a) != std::isnan(b))
    return !std::isnan(a);
  if (!std::isnan(a) && a != b)
    return a < b;
  return ai < bi;
}

struct AdaptivePlan {
  std::vector<RankedPrefix> tasks;
  std::vector<size_t> covered_depths;
  size_t calls = 0;
  bool call_budget_hit = false, time_budget_hit = false;
};

class CoarseWorkQueue {
  struct Slot {
    std::vector<size_t> ranks;
    size_t length = 0, begin = 0, end = 0, sequence = 0;
    pbqp_cost_t bound = 0;
    double progress_weight = 0;

    explicit Slot(size_t nodes) : ranks(nodes) {}
  };

  std::vector<Slot> slots;
  std::vector<size_t> free_slots, heap;
  std::mutex mutex;
  std::condition_variable changed;
  std::atomic<size_t> waiting{0};
  size_t active = 0, sequence = 0;
  std::atomic<bool> stopped{false};

  bool later(size_t a, size_t b) const noexcept {
    return prefix_preferred(slots[b].bound, slots[b].sequence, slots[a].bound,
                            slots[a].sequence);
  }

  void insert(const size_t *ranks, size_t length, size_t begin, size_t end,
              pbqp_cost_t bound, double progress_weight) noexcept {
    size_t index = free_slots.back();
    free_slots.pop_back();
    auto &s = slots[index];
    std::copy_n(ranks, length, s.ranks.begin());
    s.length = length;
    s.begin = begin;
    s.end = end;
    s.bound = bound;
    s.progress_weight = progress_weight;
    s.sequence = sequence++;
    heap.push_back(index);
    std::push_heap(heap.begin(), heap.end(),
                   [&](size_t a, size_t b) { return later(a, b); });
  }

  void remove(PrefixTask &task, pbqp_cost_t &bound) noexcept {
    std::pop_heap(heap.begin(), heap.end(),
                  [&](size_t a, size_t b) { return later(a, b); });
    size_t index = heap.back();
    heap.pop_back();
    auto &s = slots[index];
    task.ranks.assign(s.ranks.begin(), s.ranks.begin() + s.length);
    task.child_begin = s.begin;
    task.child_end = s.end;
    task.progress_weight = s.progress_weight;
    bound = s.bound;
    free_slots.push_back(index);
    ++active;
  }

public:
  CoarseWorkQueue(size_t nodes, size_t threads, const AdaptivePlan &plan) {
    const auto limit = std::numeric_limits<size_t>::max();
    if (plan.tasks.size() == limit ||
        threads > (limit - plan.tasks.size() - 1) / 2)
      fail(PBQP_OVERFLOW, "work queue size overflow");
    size_t capacity = plan.tasks.size() + 2 * threads + 1;
    slots.reserve(capacity);
    free_slots.reserve(capacity);
    heap.reserve(capacity);
    for (size_t id = 0; id < capacity; ++id) {
      slots.emplace_back(nodes);
      free_slots.push_back(id);
    }
    for (const auto &p : plan.tasks)
      insert(p.task.ranks.data(), p.task.ranks.size(), p.task.child_begin,
             p.task.child_end, p.bound, p.task.progress_weight);
  }

  bool wants_work() const noexcept {
    // No data is published by this hint. Stale reads merely delay donation;
    // actual prefix storage is always published under the queue mutex.
    return waiting.load(std::memory_order_relaxed) != 0;
  }

  // Cancellation publishes no prefix data. The mutex also closes the gap
  // between checking the flag and sleeping on the condition variable.
  void cancel() noexcept {
    std::scoped_lock lock(mutex);
    stopped.store(true);
    changed.notify_all();
  }

  bool cancelled() const noexcept { return stopped.load(); }

  bool take(PrefixTask &task, pbqp_cost_t &bound) {
    std::unique_lock lock(mutex);
    while (heap.empty() && active && !cancelled()) {
      // This relaxed count is only the donation hint described in wants_work;
      // the mutex and condition variable protect task and completion
      // publication.
      waiting.fetch_add(1, std::memory_order_relaxed);
      changed.wait(lock);
      waiting.fetch_sub(1, std::memory_order_relaxed);
    }
    if (cancelled() || heap.empty())
      return false;
    remove(task, bound);
    return true;
  }

  void completed() {
    std::scoped_lock lock(mutex);
    assert(active);
    --active;
    if (!active && heap.empty())
      changed.notify_all();
  }

  bool donate(const size_t *ranks, size_t length, size_t begin, size_t end,
              pbqp_cost_t bound, double progress_weight = 0) {
    std::scoped_lock lock(mutex);
    if (cancelled() || free_slots.empty())
      return false;
    insert(ranks, length, begin, end, bound, progress_weight);
    changed.notify_one();
    return true;
  }
};
} // namespace pbqp
