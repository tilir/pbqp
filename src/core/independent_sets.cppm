/*
 * Budgeted complete independent-set enumeration for a simple undirected graph.
 * Preparation is iterative and bounded in memory and work. Budget exhaustion
 * discards partial results; a complete empty graph is distinguished from an
 * incomplete enumeration by the completion flag.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
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
  std::vector<BitSet<>> sets;
  size_t entries = 0, maximum = 0;
  IndependentSetStatus state = IndependentSetStatus::NotPrepared;
  size_t examined = 0;

public:
  // Adjacency rows must have matching size, no loops, and symmetric edges.
  void prepare(std::span<const BitSet<>> adjacency);
  bool complete() const noexcept {
    return state == IndependentSetStatus::Complete;
  }
  IndependentSetStatus status() const noexcept { return state; }
  size_t num_examined() const noexcept { return examined; }
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
  if (adjacency.empty()) {
    state = IndependentSetStatus::Complete;
    return;
  }
  state = IndependentSetStatus::NotPrepared;
  examined = 0;
  size_t nodes = adjacency.size();
  size_t words = BitSet<>::storage_words(nodes);
  size_t row_bytes = sizeof(BitSet<>) + words * sizeof(BitSet<>::word_type);
  size_t rows = memory_budget / row_bytes;
  if (rows <= 1 || nodes >= rows - 1) {
    state = IndependentSetStatus::MemoryBudget;
    return;
  }
  size_t limit = std::min(pattern_limit, rows - nodes - 1);
  std::vector<BitSet<>> candidates(nodes + 1, BitSet<>(nodes));
  std::vector<size_t> path(nodes);
  BitSet<> current(nodes);
  candidates[0].fill();
  size_t depth = 0, remaining = word_budget;
  bool finished = true;
  while (true) {
    if (!candidates[depth].any()) {
      if (!depth)
        break;
      current.reset(path[--depth]);
      continue;
    }
    // Charge candidate scans, set iteration/copying, child construction, and
    // backtracking. The storage bound above also bounds this multiplication.
    if (sets.size() == limit || remaining < 6 * words) {
      state = sets.size() == limit
                  ? (limit < pattern_limit ? IndependentSetStatus::MemoryBudget
                                           : IndependentSetStatus::SetBudget)
                  : IndependentSetStatus::WordBudget;
      finished = false;
      break;
    }
    remaining -= 6 * words;
    size_t node = candidates[depth].first();
    candidates[depth].reset(node);
    current.set(node);
    path[depth] = node;
    size_t size = current.count();
    entries += size;
    maximum = std::max(maximum, size);
    sets.push_back(current);
    candidates[depth + 1].assign_difference(candidates[depth], adjacency[node]);
    ++depth;
  }
  examined = sets.size();
  if (finished)
    state = IndependentSetStatus::Complete;
  if (!finished) {
    sets.clear();
    entries = maximum = 0;
  }
}
} // namespace pbqp
