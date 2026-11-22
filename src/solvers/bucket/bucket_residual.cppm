/*
 * Shared conditioned PBQP views for bounded bucket refinement.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>
module pbqp:bucket_residual;
import :deadline;

namespace pbqp {
class BucketResidual {
  const pbqp_problem &problem;
  const pbqp_solution &seed;
  std::vector<size_t> ids, local, degree;
  std::vector<bool> fixed;
  std::vector<pbqp_node> nodes;
  std::vector<pbqp_edge> edges;
  std::vector<std::vector<pbqp_cost_t>> unaries, matrices;
  std::vector<std::vector<size_t>> options;

  bool apply_boundary(const pbqp_edge &edge);

public:
  BucketResidual(const pbqp_problem &p, const pbqp_solution &solution)
      : problem(p), seed(solution), local(p.num_nodes), fixed(p.num_nodes) {}

  void restrict_to(std::span<const size_t> active) {
    std::fill(fixed.begin(), fixed.end(), true);
    for (size_t id : active)
      fixed[id] = false;
  }

  pbqp_problem rebuild();
  bool fix_high_degree();
  bool condition(Deadline deadline = Deadline{});
  bool compact(Deadline deadline);
  void merge(const pbqp_solution &residual, pbqp_solution &result) const;
};

pbqp_problem BucketResidual::rebuild() {
  options.clear();
  ids.clear();
  nodes.clear();
  edges.clear();
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    if (fixed[id])
      continue;
    local[id] = ids.size();
    ids.push_back(id);
    nodes.push_back(problem.nodes[id]);
  }
  degree.assign(ids.size(), 0);
  for (const auto &edge : std::span(problem.edges, problem.num_edges)) {
    if (fixed[edge.src] || fixed[edge.tgt])
      continue;
    const size_t src = local[edge.src], tgt = local[edge.tgt];
    edges.push_back({src, tgt, edge.costs});
    ++degree[src];
    ++degree[tgt];
  }
  return {nodes.size(), edges.size(), nodes.data(), edges.data()};
}

bool BucketResidual::fix_high_degree() {
  const auto highest = std::ranges::max_element(degree);
  if (highest == degree.end() || !*highest)
    return false;
  fixed[ids[highest - degree.begin()]] = true;
  return true;
}

bool BucketResidual::apply_boundary(const pbqp_edge &edge) {
  const bool src_fixed = fixed[edge.src];
  const size_t other = src_fixed ? edge.tgt : edge.src;
  const size_t id = local[other];
  const size_t cols = problem.nodes[edge.tgt].num_options;
  auto &costs = unaries[id];
  if (costs.empty()) {
    const auto &node = nodes[id];
    costs.assign(node.costs, node.costs + node.num_options);
    nodes[id].costs = costs.data();
  }
  const size_t row = src_fixed ? seed.options[edge.src] * cols : 0;
  const size_t column = src_fixed ? 0 : seed.options[edge.tgt];
  const size_t stride = src_fixed ? 1 : cols;
  for (size_t option = 0; option < costs.size(); ++option) {
    const auto cost = edge.costs[row + column + option * stride];
    if (costs[option] == PBQP_INF || cost == PBQP_INF) {
      costs[option] = PBQP_INF;
      continue;
    }
    const auto sum = pbqp_cost_add(costs[option], cost);
    if (!std::isfinite(sum))
      return false;
    costs[option] = sum;
  }
  return true;
}

bool BucketResidual::condition(Deadline deadline) {
  unaries.clear();
  unaries.resize(nodes.size());
  for (const auto &edge : std::span(problem.edges, problem.num_edges)) {
    if (fixed[edge.src] == fixed[edge.tgt])
      continue;
    if (deadline.expired() || !apply_boundary(edge))
      return false;
  }
  return true;
}

bool BucketResidual::compact(Deadline deadline) {
  options.resize(nodes.size());
  std::vector<size_t> old_domains;
  old_domains.reserve(nodes.size());
  for (size_t id = 0; id < nodes.size(); ++id) {
    auto &node = nodes[id];
    old_domains.push_back(node.num_options);
    auto &map = options[id];
    const auto costs = std::span(node.costs, node.num_options);
    std::vector<pbqp_cost_t> kept;
    for (size_t option = 0; option < costs.size(); ++option)
      if (costs[option] != PBQP_INF) {
        map.push_back(option);
        kept.push_back(costs[option]);
      }
    if (deadline.expired() || kept.empty())
      return false;
    unaries[id].swap(kept);
    node = {map.size(), unaries[id].data()};
  }
  matrices.clear();
  matrices.resize(edges.size());
  for (size_t i = 0; i < edges.size(); ++i) {
    auto &edge = edges[i];
    const auto &rows = options[edge.src], &columns = options[edge.tgt];
    auto &matrix = matrices[i];
    if (rows.size() == old_domains[edge.src] &&
        columns.size() == old_domains[edge.tgt])
      continue;
    matrix.reserve(rows.size() * columns.size());
    for (size_t row : rows)
      for (size_t col : columns)
        matrix.push_back(edge.costs[row * old_domains[edge.tgt] + col]);
    edge.costs = matrix.data();
  }
  return !deadline.expired();
}

void BucketResidual::merge(const pbqp_solution &residual,
                           pbqp_solution &result) const {
  std::copy_n(seed.options, seed.num_nodes, result.options);
  for (size_t i = 0; i < ids.size(); ++i)
    result.options[ids[i]] =
        options.empty() ? residual.options[i] : options[i][residual.options[i]];
}

} // namespace pbqp
