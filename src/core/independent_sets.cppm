/*
 * Budgeted maximal-independent-set catalogue for simple undirected graphs.
 * Enumerates maximal sets directly with iterative pivoted Bron-Kerbosch on
 * the complement, without constructing or retaining every independent subset.
 * Incomplete catalogues are discarded, never exposed as capacity certificates.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>
module pbqp:independent_sets;
import :bitset;

namespace pbqp {
enum class IndependentSetStatus {
  NotPrepared,
  Complete,
  MemoryBudget,
  SetBudget,
  WordBudget
};

class IndependentSets {
public:
  static constexpr size_t memory_budget = 16 * 1024 * 1024;
  static constexpr size_t word_budget = 8 * 1024 * 1024;
  static constexpr size_t pattern_limit = 10000;

private:
  class MaximalSearch;
  std::vector<BitSet<>> sets;
  size_t entries = 0, maximum = 0, examined = 0, work = 0;
  IndependentSetStatus state = IndependentSetStatus::NotPrepared;
  void prepare_maximal(std::span<const BitSet<>> adjacency);

  bool append(const BitSet<> &set, size_t limit) {
    if (sets.size() == limit) {
      state = limit < pattern_limit ? IndependentSetStatus::MemoryBudget
                                    : IndependentSetStatus::SetBudget;
      return false;
    }
    size_t size = set.count();
    entries += size;
    maximum = std::max(maximum, size);
    sets.push_back(set);
    return true;
  }

public:
  // Rows have matching size, no loops, and symmetric edges. A completed
  // maximal catalogue is reusable for this graph only. Omit the empty set.
  void prepare(std::span<const BitSet<>> adjacency);

  bool complete() const noexcept {
    return state == IndependentSetStatus::Complete;
  }

  IndependentSetStatus status() const noexcept { return state; }

  size_t num_examined() const noexcept { return examined; }

  size_t work_used() const noexcept { return work; }

  auto begin() const noexcept { return sets.cbegin(); }

  auto end() const noexcept { return sets.cend(); }

  size_t size() const noexcept { return sets.size(); }

  bool empty() const noexcept { return sets.empty(); }

  size_t num_entries() const noexcept { return entries; }

  size_t maximum_size() const noexcept { return maximum; }
};

void IndependentSets::prepare(std::span<const BitSet<>> adjacency) {
  if (complete())
    return;
  sets = std::vector<BitSet<>>{};
  entries = maximum = examined = work = 0;
  state = IndependentSetStatus::NotPrepared;
  if (adjacency.empty())
    state = IndependentSetStatus::Complete;
  else
    prepare_maximal(adjacency);
  examined = sets.size();
  if (!complete()) {
    sets.clear();
    entries = maximum = 0;
  }
}

class IndependentSets::MaximalSearch {
  // Estimated word work for selecting a vertex, building child masks, and
  // updating branch state; a budget charge, not an exact instruction count.
  static constexpr size_t branch_work_factor = 8;

  struct Frame {
    BitSet<> possible, excluded, branches;
    size_t selected = 0;
    bool entered = false;

    explicit Frame(size_t nodes)
        : possible(nodes), excluded(nodes), branches(nodes) {}
  };

  IndependentSets &output;
  const std::span<const BitSet<>> graph;
  const size_t words, limit;
  std::vector<Frame> stack;
  BitSet<> current;

  bool charge(size_t count) noexcept {
    if (count > word_budget - output.work) {
      output.state = IndependentSetStatus::WordBudget;
      return false;
    }
    output.work += count;
    return true;
  }

  bool choose_pivot(const Frame &frame, size_t &pivot) {
    size_t fewest = graph.size() + 1;
    // P and X are disjoint. Minimize the original-neighbor count in P plus
    // self-membership, equivalently maximizing complement neighbors in P.
    for (const auto *pool : {&frame.possible, &frame.excluded}) {
      for (size_t node = pool->first(); node < graph.size();
           node = pool->next(node)) {
        if (!charge(3 * words))
          return false;
        size_t blocked = frame.possible.intersection_count(graph[node]) +
                         frame.possible.test(node);
        if (blocked < fewest) {
          fewest = blocked;
          pivot = node;
        }
        if (!fewest)
          return true;
      }
    }
    return true;
  }

  bool enter(Frame &frame) {
    // Cover leaf checks, branch-mask construction, and later backtracking.
    if (!charge(6 * words))
      return false;
    frame.entered = true;
    frame.branches.clear();
    if (!frame.possible.any())
      return frame.excluded.any() || output.append(current, limit);
    size_t pivot = graph.size();
    if (!choose_pivot(frame, pivot))
      return false;
    // P minus complement-neighbors(pivot) = P intersect original-neighbors,
    // plus pivot itself when it belongs to P. No complement graph is stored.
    frame.branches.assign_intersection(frame.possible, graph[pivot]);
    if (frame.possible.test(pivot))
      frame.branches.set(pivot);
    return true;
  }

public:
  static size_t set_limit(size_t nodes) noexcept {
    size_t payload =
        BitSet<>::storage_words(nodes) * sizeof(BitSet<>::word_type);
    size_t row_bytes = sizeof(BitSet<>) + payload;
    size_t frame_bytes = sizeof(Frame) + 3 * payload;
    if (row_bytes >= memory_budget ||
        nodes >= (memory_budget - row_bytes) / frame_bytes)
      return 0;
    size_t available = memory_budget - row_bytes - (nodes + 1) * frame_bytes;
    return std::min(pattern_limit, available / row_bytes);
  }

  MaximalSearch(IndependentSets &result, std::span<const BitSet<>> adjacency,
                size_t capacity)
      : output(result), graph(adjacency),
        words(BitSet<>::storage_words(adjacency.size())), limit(capacity),
        current(adjacency.size()) {
    stack.reserve(adjacency.size() + 1);
    for (size_t depth = 0; depth <= adjacency.size(); ++depth)
      stack.emplace_back(adjacency.size());
    stack[0].possible.fill();
  }

  void run() {
    size_t depth = 0;
    while (true) {
      auto &frame = stack[depth];
      if (!frame.entered && !enter(frame))
        return;
      if (!frame.branches.any()) {
        if (!depth) {
          output.state = IndependentSetStatus::Complete;
          return;
        }
        current.reset(stack[--depth].selected);
        continue;
      }
      if (!charge(branch_work_factor * words))
        return;
      size_t node = frame.branches.first();
      frame.branches.reset(node);
      frame.selected = node;
      current.set(node);
      auto &child = stack[depth + 1];
      child.possible.assign_difference(frame.possible, graph[node]);
      child.possible.reset(node);
      child.excluded.assign_difference(frame.excluded, graph[node]);
      child.entered = false;
      // Exclude this branch for subsequent siblings only after constructing
      // the child, which must not see its selected vertex in X.
      frame.possible.reset(node);
      frame.excluded.set(node);
      ++depth;
    }
  }
};

void IndependentSets::prepare_maximal(std::span<const BitSet<>> adjacency) {
  size_t limit = MaximalSearch::set_limit(adjacency.size());
  if (!limit) {
    state = IndependentSetStatus::MemoryBudget;
    return;
  }
  sets.reserve(limit);
  MaximalSearch search(*this, adjacency, limit);
  search.run();
}
} // namespace pbqp
