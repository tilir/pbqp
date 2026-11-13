/*
 * Independent exhaustive checks for optimized bounds and structural ordering.
 *
 * Every canonical prefix is checked against all original-order completions.
 * Covers overflow, forbidden spills, negative costs, clique witnesses, mask
 * boundaries, and the ordinary-bound fallback without exposing private types.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

module pbqp;
import :bnbopt_bounds;
import :problem;

namespace {
std::string context;
void check(bool condition,
           std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()) + " " + context);
}

bool evaluate(const pbqp_problem &problem, const std::vector<size_t> &tuple,
              pbqp_cost_t &cost) {
  cost = 0;
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    auto term = problem.nodes[id].costs[tuple[id]];
    if (term == PBQP_INF)
      return false;
    cost = cost + term;
  }
  for (size_t id = 0; id < problem.num_edges; ++id) {
    const auto &edge = problem.edges[id];
    auto term =
        edge.costs[tuple[edge.src] * problem.nodes[edge.tgt].num_options +
                   tuple[edge.tgt]];
    if (term == PBQP_INF)
      return false;
    cost = cost + term;
  }
  return true;
}

void check_prefix(const pbqp_problem &problem,
                  const pbqp::OptimizedBranchAndBoundBounds &bounds,
                  pbqp::OptimizedBranchAndBoundBounds::Workspace &workspace,
                  std::vector<size_t> &tuple, size_t depth) {
  pbqp_cost_t lower = 0, best = 0;
  bool possible = bounds.lower_bound(tuple.data(), depth, workspace, lower);
  bool found = false;
  std::vector<size_t> best_tuple;
  std::function<void(size_t)> complete = [&](size_t step) {
    if (step == tuple.size()) {
      pbqp_cost_t cost;
      if (evaluate(problem, tuple, cost) && (!found || cost < best)) {
        best = cost;
        best_tuple = tuple;
        found = true;
      }
      return;
    }
    size_t id = bounds.node(step);
    for (size_t option = 0; option < problem.nodes[id].num_options; ++option) {
      tuple[id] = option;
      complete(step + 1);
    }
  };
  complete(depth);
  if (found && (!possible || std::isnan(lower) || lower > best)) {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
                ("pbqp-bnbopt-bound-" + std::to_string(stamp) + ".pbqp");
    pbqp_write(path.string().c_str(), &problem, nullptr);
    std::cerr << context << " nodes=" << problem.num_nodes
              << " edges=" << problem.num_edges << " prefix depth=" << depth
              << " possible=" << possible << " lower=" << lower
              << " expected=" << best << " prefix:";
    for (size_t step = 0; step < depth; ++step)
      std::cerr << ' ' << bounds.node(step) << ':' << tuple[bounds.node(step)];
    std::cerr << " best assignment:";
    for (size_t option : best_tuple)
      std::cerr << ' ' << option;
    std::cerr << " dump=" << path << '\n';
    check(false);
  }
}

void check_graph(const pbqp::OptimizedBranchAndBoundBounds &bounds) {
  const auto &graph = bounds.graph();
  for (auto clique : graph.cliques()) {
    auto vertices = clique;
    while (vertices) {
      size_t node = std::countr_zero(vertices);
      vertices &= vertices - 1;
      check((clique & ~pbqp::RegallocStructure::bit(node) &
             ~graph.neighbors(node)) == 0);
    }
  }
  std::vector<size_t> order;
  for (size_t step = 0; step < bounds.input().num_nodes; ++step)
    order.push_back(bounds.node(step));
  std::sort(order.begin(), order.end());
  for (size_t id = 0; id < order.size(); ++id)
    check(order[id] == id);
}

void test_prefix_bounds() {
  const auto maximum = std::numeric_limits<pbqp_cost_t>::max();
  const std::vector<pbqp_cost_t> pool = {
      0,
      0.1f,
      0.2f,
      -0.1f,
      1.25f,
      -3.5f,
      16777216.0f,
      -16777216.0f,
      maximum,
      -maximum,
      std::numeric_limits<pbqp_cost_t>::denorm_min(),
      PBQP_INF};
  bool reordered = false;
  for (unsigned seed = 0; seed < 60; ++seed) {
    context = "optimized prefix seed=" + std::to_string(seed);
    std::mt19937 random(seed);
    size_t domain = 2 + seed % 3;
    std::vector<std::vector<pbqp_cost_t>> unary(
        5, std::vector<pbqp_cost_t>(domain));
    std::vector<pbqp_node> nodes;
    for (auto &costs : unary) {
      costs[0] = pool[random() % pool.size()];
      nodes.push_back({domain, costs.data()});
    }
    std::vector<pbqp_cost_t> matrix(domain * domain);
    for (size_t color = 1; color < domain; ++color)
      matrix[color * domain + color] = PBQP_INF;
    std::vector<pbqp_edge> edges;
    for (size_t src = 0; src < nodes.size(); ++src)
      for (size_t tgt = src + 1; tgt < nodes.size(); ++tgt)
        if (seed % 3 == 0 || random() % 4 != 0)
          edges.push_back({src, tgt, matrix.data()});
    std::shuffle(edges.begin(), edges.end(), random);
    pbqp_problem problem{nodes.size(), edges.size(), nodes.data(),
                         edges.data()};
    pbqp::validate(&problem);
    pbqp::OptimizedBranchAndBoundBounds bounds(problem);
    bounds.prepare_search();
    check(bounds.optimized());
    check_graph(bounds);
    for (size_t step = 0; step < nodes.size(); ++step)
      reordered |= bounds.node(step) != step;
    pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
    std::vector<size_t> tuple(nodes.size());
    std::function<void(size_t, size_t)> prefix = [&](size_t depth,
                                                     size_t used) {
      check_prefix(problem, bounds, workspace, tuple, depth);
      if (depth == nodes.size())
        return;
      size_t id = bounds.node(depth);
      for (size_t option = 0; option <= std::min(domain - 1, used + 1);
           ++option) {
        tuple[id] = option;
        prefix(depth + 1, std::max(used, option));
      }
    };
    prefix(0, 0);
  }
  check(reordered);
}

void test_mask_limits() {
  for (size_t count : {size_t{63}, size_t{64}, size_t{65}})
    for (size_t registers : {size_t{1}, size_t{64}, size_t{65}}) {
      context = "mask nodes=" + std::to_string(count) +
                " registers=" + std::to_string(registers);
      size_t domain = registers + 1;
      std::vector<pbqp_cost_t> unary(domain);
      unary[0] = 1;
      std::vector<pbqp_node> nodes(count, pbqp_node{domain, unary.data()});
      std::vector<pbqp_cost_t> matrix(domain * domain);
      for (size_t color = 1; color < domain; ++color)
        matrix[color * domain + color] = PBQP_INF;
      pbqp_edge edge{0, count - 1, matrix.data()};
      pbqp_problem problem{nodes.size(), 1, nodes.data(), &edge};
      pbqp::OptimizedBranchAndBoundBounds bounds(problem);
      bounds.prepare_search();
      check(bounds.optimized() == (count <= 64 && registers <= 64));
      pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
      pbqp_cost_t lower;
      auto expected = bounds.optimized() && registers == 1 ? 1 : 0;
      check(bounds.lower_bound(nullptr, 0, workspace, lower) &&
            lower == expected);
      if (bounds.optimized() && registers == 64) {
        std::vector<size_t> tuple(count);
        for (size_t step = 0; step < count; ++step)
          tuple[bounds.node(step)] = step + 1;
        check(bounds.lower_bound(tuple.data(), count, workspace, lower));
        check(lower == 0);
      }
    }
  // Near-regalloc matrices must use the generic path.
  pbqp_cost_t unary[] = {1, 0, 0};
  pbqp_node nodes[] = {{3, unary}, {3, unary}};
  pbqp_cost_t matrix[] = {0, 0, 0, 0, PBQP_INF, 0.25f, 0, 0, PBQP_INF};
  pbqp_edge edge{0, 1, matrix};
  pbqp_problem problem{2, 1, nodes, &edge};
  pbqp::OptimizedBranchAndBoundBounds bounds(problem);
  bounds.prepare_search();
  check(!bounds.optimized());
}

void test_full_clique_capacity() {
  // Exercise the largest useful spill-count DP: 64 mutually interfering nodes
  // and one register require 63 spills, or prove infeasibility if spills are
  // forbidden. No exhaustive 2^64 search is needed to establish these facts.
  for (bool forbidden : {false, true}) {
    pbqp_cost_t unary[] = {forbidden ? PBQP_INF : 1, 0};
    pbqp_cost_t matrix[] = {0, 0, 0, PBQP_INF};
    std::vector<pbqp_node> nodes(64, pbqp_node{2, unary});
    std::vector<pbqp_edge> edges;
    for (size_t src = 0; src < nodes.size(); ++src)
      for (size_t tgt = src + 1; tgt < nodes.size(); ++tgt)
        edges.push_back({src, tgt, matrix});
    pbqp_problem problem{nodes.size(), edges.size(), nodes.data(),
                         edges.data()};
    pbqp::OptimizedBranchAndBoundBounds bounds(problem);
    bounds.prepare_search();
    check(bounds.optimized());
    pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
    pbqp_cost_t lower;
    check(bounds.lower_bound(nullptr, 0, workspace, lower) == !forbidden);
    if (!forbidden)
      check(lower == 63);
  }
}
} // namespace

namespace pbqp {
extern "C++" void run_bnbopt_internal_tests() {
  test_prefix_bounds();
  test_mask_limits();
  test_full_clique_capacity();
}
} // namespace pbqp
