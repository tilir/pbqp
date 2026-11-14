/*
 * Bounded clique preparation and structural order for regalloc-shaped PBQP.
 *
 * Verified uniform regalloc graphs use dynamic word-vector bit sets.
 * Preparation has memory and work budgets. Every retained clique remains valid
 * when discovery stops before finding a maximum.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <vector>

module pbqp:bnbopt_structure;
import :bitset;

namespace pbqp {
class RegallocStructure {
public:
  static constexpr size_t clique_budget = 100000;
  static constexpr size_t max_cliques = 32;
  // Bound auxiliary dense storage on large sparse inputs. This is a resource
  // budget, independent of the bit width of node IDs or register counts.
  static constexpr size_t memory_budget = 64 * 1024 * 1024;
  static constexpr size_t word_budget = 64 * 1024 * 1024;
  static constexpr size_t greedy_starts = 64;

private:
  static bool fits_storage(size_t nodes) noexcept {
    size_t words = BitSet<>::storage_words(nodes);
    size_t row = sizeof(BitSet<>) + words * sizeof(BitSet<>::word_type);
    return nodes < memory_budget / (2 * row);
  }

  const pbqp_problem &problem;
  bool supported;
  bool prepared = false;
  std::vector<BitSet<>> adjacent;
  std::vector<size_t> degrees;
  size_t remaining_work = word_budget;
  std::vector<BitSet<>> clique_sets;
  std::vector<size_t> order;
  std::vector<pbqp_node> ordered_nodes;
  pbqp_problem enumeration{};
  BitSet<> best_clique;
  size_t starts_used = 0, refinement_steps = 0, omitted = 0;
  bool adaptive_tail = true;

  double clique_weight(const BitSet<> &clique) const noexcept;
  bool better_clique(const BitSet<> &candidate,
                     const BitSet<> &incumbent) const noexcept;
  size_t clique_vertex(const BitSet<> &candidates) noexcept;
  void greedy_cliques();
  void refine_clique();
  void prepare_order();

public:
  RegallocStructure(const pbqp_problem &p, bool pure_interference)
      : problem(p), supported(pure_interference && p.num_nodes &&
                              fits_storage(p.num_nodes)),
        best_clique(supported ? p.num_nodes : 0) {}
  RegallocStructure(const RegallocStructure &) = delete;
  RegallocStructure &operator=(const RegallocStructure &) = delete;
  void prepare();
  size_t greedy_starts_used() const noexcept { return starts_used; }
  size_t clique_steps() const noexcept { return refinement_steps; }
  bool clique_budget_hit() const noexcept {
    return refinement_steps == clique_budget;
  }
  bool word_budget_hit() const noexcept { return remaining_work == 0; }
  size_t omitted_cliques() const noexcept { return omitted; }
  bool uses_adaptive_tail() const noexcept { return adaptive_tail; }
  bool eligible() const noexcept { return supported; }
  bool ready() const noexcept { return prepared; }
  size_t node(size_t step) const noexcept {
    return prepared ? order[step] : step;
  }
  std::span<const BitSet<>> adjacency() const noexcept { return adjacent; }
  const BitSet<> &neighbors(size_t node) const noexcept {
    return adjacent[node];
  }
  const std::vector<BitSet<>> &cliques() const noexcept { return clique_sets; }
  const pbqp_problem &domains() const noexcept {
    return prepared ? enumeration : problem;
  }
};

double RegallocStructure::clique_weight(const BitSet<> &clique) const noexcept {
  // This score selects an order only. It is never used as a numerical bound.
  double weight = 0;
  for (size_t node = clique.first(); node < clique.size();
       node = clique.next(node))
    weight += problem.nodes[node].costs[0];
  return weight;
}

bool RegallocStructure::better_clique(
    const BitSet<> &candidate, const BitSet<> &incumbent) const noexcept {
  size_t size = candidate.count(), old_size = incumbent.count();
  if (size != old_size)
    return size > old_size;
  double weight = clique_weight(candidate),
         old_weight = clique_weight(incumbent);
  return weight != old_weight ? weight > old_weight : candidate < incumbent;
}

size_t RegallocStructure::clique_vertex(const BitSet<> &candidates) noexcept {
  assert(candidates.any());
  size_t best = candidates.first(), best_degree = 0;
  bool scored = false;
  for (size_t node = best; node < candidates.size();
       node = candidates.next(node)) {
    if (remaining_work < candidates.word_count()) {
      remaining_work = 0;
      break;
    }
    remaining_work -= candidates.word_count();
    size_t degree = adjacent[node].intersection_count(candidates);
    if (!scored || degree > best_degree ||
        (degree == best_degree &&
         problem.nodes[node].costs[0] > problem.nodes[best].costs[0])) {
      best = node;
      best_degree = degree;
      scored = true;
    }
  }
  return best;
}

void RegallocStructure::greedy_cliques() {
  BitSet<> clique(problem.num_nodes), candidates(problem.num_nodes);
  for (size_t start = 0; start < std::min(problem.num_nodes, greedy_starts);
       ++start) {
    ++starts_used;
    clique.clear();
    clique.set(start);
    candidates.copy_from(adjacent[start]);
    while (candidates.any()) {
      size_t node = clique_vertex(candidates);
      clique.set(node);
      candidates.intersect_with(adjacent[node]);
    }
    clique_sets.push_back(clique);
    if (better_clique(clique, best_clique))
      best_clique.copy_from(clique);
    if (!remaining_work)
      break;
  }
}

void RegallocStructure::refine_clique() {
  std::vector<BitSet<>> candidates(problem.num_nodes + 1,
                                   BitSet<>(problem.num_nodes));
  std::vector<size_t> path(problem.num_nodes);
  BitSet<> clique(problem.num_nodes);
  candidates[0].fill();
  size_t depth = 0, steps = 0, best_size = best_clique.count();
  while (steps < clique_budget && remaining_work) {
    if (depth + candidates[depth].count() <= best_size) {
      if (!depth)
        break;
      clique.reset(path[--depth]);
      continue;
    }
    ++steps;
    size_t node = clique_vertex(candidates[depth]);
    candidates[depth].reset(node);
    clique.set(node);
    if (better_clique(clique, best_clique)) {
      best_clique.copy_from(clique);
      best_size = depth + 1;
    }
    path[depth] = node;
    candidates[depth + 1].assign_intersection(candidates[depth],
                                              adjacent[node]);
    ++depth;
  }
  refinement_steps = steps;
}

void RegallocStructure::prepare_order() {
  order.reserve(problem.num_nodes);
  BitSet<> remaining(problem.num_nodes), chosen(problem.num_nodes);
  std::vector<size_t> connections(problem.num_nodes);
  auto append = [&](size_t node) {
    order.push_back(node);
    chosen.set(node);
    remaining.reset(node);
    for (size_t neighbor = adjacent[node].first(); neighbor < problem.num_nodes;
         neighbor = adjacent[node].next(neighbor))
      ++connections[neighbor];
  };
  auto preferred = [&](size_t node, size_t best) {
    if (connections[node] != connections[best])
      return connections[node] > connections[best];
    if (degrees[node] != degrees[best])
      return degrees[node] > degrees[best];
    auto cost = problem.nodes[node].costs[0],
         old_cost = problem.nodes[best].costs[0];
    return cost != old_cost ? cost > old_cost : node < best;
  };
  // For large graphs, use a static degree order after the first clique rather
  // than spending quadratic time rescoring every remaining candidate.
  adaptive_tail = problem.num_nodes <= word_budget / problem.num_nodes;
  for (size_t group = 0; group < 2; ++group) {
    if (group == 0)
      remaining.copy_from(clique_sets.front());
    else {
      remaining.fill();
      remaining.subtract(chosen);
    }
    if (group && !adaptive_tail) {
      std::vector<size_t> tail;
      for (size_t node = remaining.first(); node < remaining.size();
           node = remaining.next(node))
        tail.push_back(node);
      std::sort(tail.begin(), tail.end(), preferred);
      for (size_t node : tail)
        append(node);
      break;
    }
    while (remaining.any()) {
      size_t best = remaining.first();
      for (size_t node = remaining.next(best); node < remaining.size();
           node = remaining.next(node))
        if (preferred(node, best))
          best = node;
      append(best);
    }
  }
  ordered_nodes.reserve(problem.num_nodes);
  for (size_t node : order)
    ordered_nodes.push_back(problem.nodes[node]);
  enumeration = {problem.num_nodes, 0, ordered_nodes.data(), nullptr};
}

void RegallocStructure::prepare() {
  if (prepared || !supported)
    return;
  adjacent.assign(problem.num_nodes, BitSet<>(problem.num_nodes));
  degrees.resize(problem.num_nodes);
  for (size_t id = 0; id < problem.num_edges; ++id) {
    const auto &edge = problem.edges[id];
    adjacent[edge.src].set(edge.tgt);
    adjacent[edge.tgt].set(edge.src);
    ++degrees[edge.src];
    ++degrees[edge.tgt];
  }
  clique_sets.reserve(std::min(problem.num_nodes, greedy_starts) + 1);
  greedy_cliques();
  refine_clique();
  clique_sets.push_back(best_clique);
  std::sort(clique_sets.begin(), clique_sets.end());
  clique_sets.erase(std::unique(clique_sets.begin(), clique_sets.end()),
                    clique_sets.end());
  std::sort(clique_sets.begin(), clique_sets.end(),
            [&](const BitSet<> &a, const BitSet<> &b) {
              return better_clique(a, b);
            });
  if (clique_sets.size() > max_cliques) {
    omitted = clique_sets.size() - max_cliques;
    clique_sets.resize(max_cliques);
  }
  prepare_order();
  prepared = true;
}
} // namespace pbqp
