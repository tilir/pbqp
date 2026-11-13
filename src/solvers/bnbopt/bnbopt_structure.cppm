/*
 * Bounded clique preparation and structural order for regalloc-shaped PBQP.
 *
 * Only verified uniform regalloc graphs with at most 64 nodes and registers
 * use this representation. Clique discovery has a fixed work budget; every
 * retained clique is valid even when discovery stops before finding a maximum.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <vector>

module pbqp:bnbopt_structure;

namespace pbqp {
class RegallocStructure {
public:
  using Mask = std::uint64_t;
  static constexpr size_t capacity = 64;
  static constexpr size_t clique_budget = 100000;
  static constexpr size_t max_cliques = 32;
  static Mask bit(size_t id) noexcept { return Mask{1} << id; }
  static Mask full(size_t count) noexcept {
    return count == capacity ? ~Mask{0} : (Mask{1} << count) - 1;
  }

private:
  const pbqp_problem &problem;
  bool supported;
  bool prepared = false;
  std::array<Mask, capacity> adjacent{};
  std::vector<Mask> clique_sets;
  std::vector<size_t> order;
  std::vector<pbqp_node> ordered_nodes;
  pbqp_problem enumeration{};
  Mask best_clique = 0;

  double clique_weight(Mask clique) const noexcept;
  bool better_clique(Mask candidate, Mask incumbent) const noexcept;
  size_t clique_vertex(Mask candidates) const noexcept;
  size_t order_vertex(Mask candidates, Mask chosen) const noexcept;
  void greedy_cliques();
  void refine_clique() noexcept;
  void prepare_order();

public:
  RegallocStructure(const pbqp_problem &p, bool regalloc)
      : problem(p),
        supported(regalloc && p.num_nodes && p.num_nodes <= capacity &&
                  p.nodes[0].num_options - 1 <= capacity) {}
  RegallocStructure(const RegallocStructure &) = delete;
  RegallocStructure &operator=(const RegallocStructure &) = delete;
  void prepare();
  bool ready() const noexcept { return prepared; }
  size_t node(size_t step) const noexcept {
    return prepared ? order[step] : step;
  }
  Mask neighbors(size_t node) const noexcept { return adjacent[node]; }
  const std::vector<Mask> &cliques() const noexcept { return clique_sets; }
  const pbqp_problem &domains() const noexcept {
    return prepared ? enumeration : problem;
  }
};

double RegallocStructure::clique_weight(Mask clique) const noexcept {
  // This score selects an order only. It is never used as a numerical bound.
  double weight = 0;
  while (clique) {
    size_t node = std::countr_zero(clique);
    clique &= clique - 1;
    weight += problem.nodes[node].costs[0];
  }
  return weight;
}

bool RegallocStructure::better_clique(Mask candidate,
                                      Mask incumbent) const noexcept {
  int size = std::popcount(candidate), old_size = std::popcount(incumbent);
  if (size != old_size)
    return size > old_size;
  double weight = clique_weight(candidate),
         old_weight = clique_weight(incumbent);
  return weight != old_weight ? weight > old_weight : candidate < incumbent;
}

size_t RegallocStructure::clique_vertex(Mask candidates) const noexcept {
  assert(candidates);
  size_t best = std::countr_zero(candidates);
  for (Mask remaining = candidates; remaining; remaining &= remaining - 1) {
    size_t node = std::countr_zero(remaining);
    int degree = std::popcount(adjacent[node] & candidates);
    int old_degree = std::popcount(adjacent[best] & candidates);
    if (degree > old_degree ||
        (degree == old_degree &&
         problem.nodes[node].costs[0] > problem.nodes[best].costs[0]))
      best = node;
  }
  return best;
}

size_t RegallocStructure::order_vertex(Mask candidates,
                                       Mask chosen) const noexcept {
  assert(candidates);
  size_t best = std::countr_zero(candidates);
  for (Mask remaining = candidates; remaining; remaining &= remaining - 1) {
    size_t node = std::countr_zero(remaining);
    int fixed = std::popcount(adjacent[node] & chosen);
    int old_fixed = std::popcount(adjacent[best] & chosen);
    int degree = std::popcount(adjacent[node]);
    int old_degree = std::popcount(adjacent[best]);
    if (fixed > old_fixed ||
        (fixed == old_fixed &&
         (degree > old_degree ||
          (degree == old_degree &&
           problem.nodes[node].costs[0] > problem.nodes[best].costs[0]))))
      best = node;
  }
  return best;
}

void RegallocStructure::greedy_cliques() {
  for (size_t start = 0; start < problem.num_nodes; ++start) {
    Mask clique = bit(start), candidates = adjacent[start];
    while (candidates) {
      size_t node = clique_vertex(candidates);
      clique |= bit(node);
      candidates &= adjacent[node];
    }
    clique_sets.push_back(clique);
    if (better_clique(clique, best_clique))
      best_clique = clique;
  }
}

void RegallocStructure::refine_clique() noexcept {
  std::array<Mask, capacity + 1> candidates{}, prefix{};
  candidates[0] = full(problem.num_nodes);
  size_t depth = 0, steps = 0;
  while (steps < clique_budget) {
    if (depth + std::popcount(candidates[depth]) <=
        static_cast<size_t>(std::popcount(best_clique))) {
      if (!depth)
        break;
      --depth;
      continue;
    }
    ++steps;
    size_t node = clique_vertex(candidates[depth]);
    candidates[depth] &= ~bit(node);
    Mask clique = prefix[depth] | bit(node);
    if (better_clique(clique, best_clique))
      best_clique = clique;
    prefix[depth + 1] = clique;
    candidates[depth + 1] = candidates[depth] & adjacent[node];
    ++depth;
  }
}

void RegallocStructure::prepare_order() {
  order.reserve(problem.num_nodes);
  Mask chosen = 0;
  for (Mask group : {clique_sets.front(), full(problem.num_nodes)}) {
    Mask remaining = group & ~chosen;
    while (remaining) {
      size_t node = order_vertex(remaining, chosen);
      order.push_back(node);
      chosen |= bit(node);
      remaining &= ~bit(node);
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
  for (size_t id = 0; id < problem.num_edges; ++id) {
    const auto &edge = problem.edges[id];
    adjacent[edge.src] |= bit(edge.tgt);
    adjacent[edge.tgt] |= bit(edge.src);
  }
  clique_sets.reserve(problem.num_nodes + 1);
  greedy_cliques();
  refine_clique();
  clique_sets.push_back(best_clique);
  std::sort(clique_sets.begin(), clique_sets.end());
  clique_sets.erase(std::unique(clique_sets.begin(), clique_sets.end()),
                    clique_sets.end());
  std::sort(clique_sets.begin(), clique_sets.end(),
            [&](Mask a, Mask b) { return better_clique(a, b); });
  if (clique_sets.size() > max_cliques)
    clique_sets.resize(max_cliques);
  prepare_order();
  prepared = true;
}
} // namespace pbqp
