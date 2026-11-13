/*
 * Mutable PBQP reduction graph and feasibility-aware intermediate costs.
 *
 * Disconnection preserves the eliminated node's adjacency for backpropagation.
 * Surviving adjacency contains only active edges; R2 fill edges are allocated
 * during preparation. Active edge positions allow swap-and-pop removal; frozen
 * adjacency is sorted by edge ID to retain arithmetic order. Explicit
 * prohibitions remain distinct from float overflow.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

module pbqp:reduction_graph;
import :failure;
import :problem;

namespace pbqp {
struct ReductionCost {
  pbqp_cost_t value = 0;
  bool feasible = true;

  static ReductionCost input(pbqp_cost_t cost) noexcept {
    return {cost, cost != PBQP_INF};
  }
};

ReductionCost add_reduction_costs(ReductionCost a, ReductionCost b) noexcept {
  return {pbqp_cost_add(a.value, b.value), a.feasible && b.feasible};
}

bool better_reduction_cost(ReductionCost a, ReductionCost b) noexcept {
  if (!a.feasible)
    return false;
  if (!b.feasible)
    return true;
  // Regrouping overflowed intermediates can yield NaN. Prefer numerical scores,
  // but retain feasibility and a deterministic first choice if all are NaN.
  if (std::isnan(a.value))
    return false;
  return std::isnan(b.value) || a.value < b.value;
}

class ReductionGraph {
public:
  struct Node {
    std::vector<ReductionCost> costs;
    std::vector<size_t> edges;
    bool active = true;
  };
  struct Edge {
    size_t src;
    size_t tgt;
    size_t src_position;
    size_t tgt_position;
    std::vector<ReductionCost> costs;
  };
  static constexpr size_t no_edge = std::numeric_limits<size_t>::max();

private:
  std::vector<Node> nodes;
  std::vector<Edge> edges;

  size_t &position(size_t edge, size_t node) noexcept {
    return edges[edge].src == node ? edges[edge].src_position
                                   : edges[edge].tgt_position;
  }

public:
  explicit ReductionGraph(const pbqp_problem &problem);
  Node &node(size_t id) noexcept { return nodes[id]; }
  const Node &node(size_t id) const noexcept { return nodes[id]; }
  size_t num_nodes() const noexcept { return nodes.size(); }
  size_t num_edges() const noexcept { return edges.size(); }
  size_t source(size_t edge) const noexcept { return edges[edge].src; }
  size_t target(size_t edge) const noexcept { return edges[edge].tgt; }
  size_t degree(size_t id) const noexcept { return nodes[id].edges.size(); }
  size_t other(size_t edge, size_t id) const noexcept {
    return edges[edge].src == id ? edges[edge].tgt : edges[edge].src;
  }
  ReductionCost cost(size_t edge, size_t node, size_t option,
                     size_t other_option) const noexcept;
  size_t find_edge(size_t src, size_t tgt) const noexcept;
  size_t merge_edge(size_t src, size_t tgt, std::vector<ReductionCost> delta);
  void freeze(size_t id) noexcept;
  void disconnect(size_t id) noexcept;
  static size_t matrix_size(size_t rows, size_t cols);
};

size_t ReductionGraph::matrix_size(size_t rows, size_t cols) {
  if (rows > std::numeric_limits<size_t>::max() / cols)
    fail(PBQP_OVERFLOW, "reduction matrix dimensions overflow");
  check_count<ReductionCost>(rows * cols);
  return rows * cols;
}

ReductionGraph::ReductionGraph(const pbqp_problem &problem)
    : nodes(problem.num_nodes) {
  edges.reserve(problem.num_edges);
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    const auto &input = problem.nodes[id];
    auto &costs = nodes[id].costs;
    costs.reserve(input.num_options);
    for (size_t option = 0; option < input.num_options; ++option)
      costs.push_back(ReductionCost::input(input.costs[option]));
  }
  for (size_t id = 0; id < problem.num_edges; ++id) {
    const auto &input = problem.edges[id];
    size_t count = matrix_size(nodes[input.src].costs.size(),
                               nodes[input.tgt].costs.size());
    std::vector<ReductionCost> costs;
    costs.reserve(count);
    for (size_t index = 0; index < count; ++index)
      costs.push_back(ReductionCost::input(input.costs[index]));
    edges.push_back({input.src, input.tgt, nodes[input.src].edges.size(),
                     nodes[input.tgt].edges.size(), std::move(costs)});
    nodes[input.src].edges.push_back(id);
    nodes[input.tgt].edges.push_back(id);
  }
}

ReductionCost ReductionGraph::cost(size_t edge, size_t node, size_t option,
                                   size_t other_option) const noexcept {
  const auto &e = edges[edge];
  size_t row = e.src == node ? option : other_option;
  size_t col = e.src == node ? other_option : option;
  return e.costs[row * nodes[e.tgt].costs.size() + col];
}

size_t ReductionGraph::find_edge(size_t src, size_t tgt) const noexcept {
  // Lookup is undirected; matrix orientation remains canonical in merge_edge.
  if (nodes[src].edges.size() > nodes[tgt].edges.size())
    std::swap(src, tgt);
  for (size_t edge : nodes[src].edges)
    if (other(edge, src) == tgt)
      return edge;
  return no_edge;
}

size_t ReductionGraph::merge_edge(size_t src, size_t tgt,
                                  std::vector<ReductionCost> delta) {
  // Callers construct row-major fill matrices with src < tgt.
  auto edge = find_edge(src, tgt);
  if (edge == no_edge) {
    size_t id = edges.size();
    edges.push_back({src, tgt, nodes[src].edges.size(), nodes[tgt].edges.size(),
                     std::move(delta)});
    nodes[src].edges.push_back(id);
    nodes[tgt].edges.push_back(id);
    return id;
  }
  auto &costs = edges[edge].costs;
  for (size_t index = 0; index < costs.size(); ++index)
    costs[index] = add_reduction_costs(delta[index], costs[index]);
  return edge;
}

void ReductionGraph::freeze(size_t id) noexcept {
  auto &adjacent = nodes[id].edges;
  std::sort(adjacent.begin(), adjacent.end());
  for (size_t index = 0; index < adjacent.size(); ++index)
    position(adjacent[index], id) = index;
}

void ReductionGraph::disconnect(size_t id) noexcept {
  for (size_t edge : nodes[id].edges) {
    size_t neighbor = other(edge, id);
    auto &adjacent = nodes[neighbor].edges;
    size_t index = position(edge, neighbor);
    size_t moved = adjacent.back();
    adjacent[index] = moved;
    position(moved, neighbor) = index;
    adjacent.pop_back();
  }
  nodes[id].active = false;
}
} // namespace pbqp
