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
#include <array>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <ranges>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

module pbqp;
import :bitset;
import :bnbopt_bounds;
import :bnb_bounds;
import :independent_sets;
import :bnbopt_pricing;
import :diagnostics;
import :bnbopt_diagnostics;
import :problem;

namespace {
std::string context;
void check(bool condition,
           std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()) + " " + context);
}

struct DiagnosticLog {
  std::string text;
};
void record_diagnostic(const char *message, void *data) noexcept {
  auto &log = *static_cast<DiagnosticLog *>(data);
  log.text += message;
  log.text += '\n';
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
  for (const auto &clique : graph.cliques())
    for (size_t node = clique.first(); node < clique.size();
         node = clique.next(node))
      for (size_t other = clique.next(node); other < clique.size();
           other = clique.next(other))
        check(graph.neighbors(node).test(other));
  std::vector<size_t> order;
  for (size_t step = 0; step < bounds.input().num_nodes; ++step)
    order.push_back(bounds.node(step));
  std::sort(order.begin(), order.end());
  for (size_t id = 0; id < order.size(); ++id)
    check(order[id] == id);
}

void test_prefix_bounds(bool positive = false) {
  const auto maximum = std::numeric_limits<pbqp_cost_t>::max();
  const std::vector<pbqp_cost_t> pool =
      positive ? std::vector<pbqp_cost_t>{0.1f, 0.2f, 1.25f, 3.5f, 10, 100}
               : std::vector<pbqp_cost_t>{
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
  size_t priced = 0;
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
    bounds.prepare_capacity();
    priced += bounds.weights().ready();
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
  if (positive)
    check(priced > 0);
}

void test_bitset_boundaries() {
  for (size_t count : {size_t{63}, size_t{64}, size_t{65}, size_t{127},
                       size_t{128}, size_t{129}})
    for (size_t registers :
         {size_t{1}, size_t{64}, size_t{65}, size_t{128}, size_t{129}}) {
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
      check(bounds.optimized());
      check_graph(bounds);
      pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
      pbqp_cost_t lower;
      auto expected = bounds.optimized() && registers == 1 ? 1 : 0;
      check(bounds.lower_bound(nullptr, 0, workspace, lower) &&
            lower == expected);
      if (registers >= count) {
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
  // Cross multiple storage words in both the clique and spill-count DP.
  // A complete graph with one register needs N-1 spills; forbidden spills
  // prove infeasibility without enumerating its 2^N assignments.
  for (size_t count : {size_t{64}, size_t{65}, size_t{129}})
    for (bool forbidden : {false, true}) {
      context = "clique size=" + std::to_string(count) +
                " forbidden=" + std::to_string(forbidden);
      pbqp_cost_t unary[] = {forbidden ? PBQP_INF : 1, 0};
      pbqp_cost_t matrix[] = {0, 0, 0, PBQP_INF};
      std::vector<pbqp_node> nodes(count, pbqp_node{2, unary});
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
        check(lower == count - 1);
      pbqp_solve_options options{4, nullptr, nullptr, nullptr, nullptr};
      pbqp_solution *solution = nullptr;
      auto status = pbqp_solve_branch_and_bound_optimized(&problem, &options,
                                                          &solution, nullptr);
      check(status == (forbidden ? PBQP_INFEASIBLE : PBQP_OK));
      if (!forbidden) {
        check(solution && solution->quality == PBQP_SOLUTION_OPTIMAL);
        check(solution->cost == static_cast<pbqp_cost_t>(count - 1));
      }
      pbqp_solution_free(solution);
    }
}

void test_storage_budget_fallback() {
  context = "dense preparation storage budget";
  pbqp_cost_t unary[] = {1, 0};
  std::vector<pbqp_node> nodes(100000, pbqp_node{2, unary});
  pbqp_problem problem{nodes.size(), 0, nodes.data(), nullptr};
  pbqp::OptimizedBranchAndBoundBounds bounds(problem);
  bounds.prepare_search();
  check(!bounds.optimized());
  DiagnosticLog log;
  bounds.describe_input(pbqp::Diagnostics(record_diagnostic, &log));
  check(log.text.find("graph storage budget exceeded") != std::string::npos);
  check(&bounds.domains() == &problem);
  pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
  pbqp_cost_t lower;
  check(bounds.lower_bound(nullptr, 0, workspace, lower));
  check(lower == 0);
}

void test_high_register_conflict() {
  context = "conflict across node and register words";
  constexpr size_t count = 130, domain = 130;
  std::vector<pbqp_cost_t> unary(domain);
  unary[0] = 1;
  std::vector<pbqp_cost_t> matrix(domain * domain);
  for (size_t color = 1; color < domain; ++color)
    matrix[color * domain + color] = PBQP_INF;
  std::vector<pbqp_node> nodes(count, pbqp_node{domain, unary.data()});
  std::vector<pbqp_edge> edges;
  for (size_t id = 0; id < count; id += 2)
    edges.push_back({id, id + 1, matrix.data()});
  pbqp_problem problem{count, edges.size(), nodes.data(), edges.data()};
  pbqp::OptimizedBranchAndBoundBounds bounds(problem);
  bounds.prepare_search();
  check(bounds.optimized());
  pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
  std::vector<size_t> tuple(count);
  for (size_t step = 0; step < count - 1; ++step)
    tuple[bounds.node(step)] = step + 1;
  pbqp_cost_t lower;
  check(bounds.lower_bound(tuple.data(), count - 1, workspace, lower));
  check(!workspace.allowed[bounds.node(count - 1)].test(128));
  tuple[bounds.node(count - 1)] = 129;
  check(!bounds.lower_bound(tuple.data(), count, workspace, lower));
  tuple[bounds.node(count - 1)] = 0;
  check(bounds.lower_bound(tuple.data(), count, workspace, lower));
  check(lower == 1);
}

template <typename Word>
concept BitSetWord = requires { typename pbqp::BitSet<Word>; };
static_assert(BitSetWord<unsigned> && !BitSetWord<int> && !BitSetWord<bool> &&
              !BitSetWord<const unsigned>);

template <typename Word> void test_bitsets() {
  pbqp::BitSet<Word> a(9), b(9), empty;
  a.set(0);
  a.set(1);
  b.set(0);
  b.set(2);
  check(a.intersects(b) && !(a == b));
  a.clear();
  b.clear();
  check(!a.intersects(b) && a == b && !(a == empty));
  std::mt19937 random(42);
  for (size_t size : {size_t{0}, size_t{1}, size_t{63}, size_t{64}, size_t{65},
                      size_t{127}, size_t{128}, size_t{129}, size_t{257}}) {
    context = "bitset size=" + std::to_string(size);
    pbqp::BitSet<Word> a(size), b(size), result(size);
    std::vector<bool> x(size), y(size);
    for (size_t round = 0; round < 20; ++round) {
      a.clear();
      b.fill();
      for (size_t id = 0; id < size; ++id) {
        x[id] = random() % 2;
        y[id] = random() % 2;
        if (x[id])
          a.set(id);
        if (!y[id])
          b.reset(id);
        check(a.test(id) == x[id] && b.test(id) == y[id]);
      }
      check(a.count() ==
            static_cast<size_t>(std::count(x.begin(), x.end(), true)));
      size_t visited = 0;
      for (size_t id = a.first(); id < size; id = a.next(id)) {
        check(x[id]);
        ++visited;
      }
      check(visited == a.count());
      result.assign_intersection(a, b);
      check(result.count() == a.intersection_count(b));
      check(result.any() == a.intersects(b));
      check(result.is_subset_of(a) && result.is_subset_of(b));
      check(a.is_subset_of(b) == (result.count() == a.count()));
      for (size_t id = 0; id < size; ++id)
        check(result.test(id) == (x[id] && y[id]));
      result.copy_from(a);
      check(result == a);
      result.intersect_with(b);
      for (size_t id = 0; id < size; ++id)
        check(result.test(id) == (x[id] && y[id]));
      result.assign_difference(a, b);
      for (size_t id = 0; id < size; ++id)
        check(result.test(id) == (x[id] && !y[id]));
      result.copy_from(a);
      result.subtract(b);
      result.unite_with(b);
      for (size_t id = 0; id < size; ++id)
        check(result.test(id) == (x[id] || y[id]));
      result.fill();
      check(result.count() == size && result.next(size) == size);
    }
  }
}

static_assert(std::ranges::random_access_range<pbqp::IndependentSets>);
static_assert(std::ranges::sized_range<const pbqp::IndependentSets>);
static_assert(
    std::same_as<std::ranges::range_reference_t<pbqp::IndependentSets>,
                 const pbqp::BitSet<> &>);

// Test-only historical reference: enumerate all nonempty independent subsets
// by recursive extension. Its cap reproduces the old catalogue limitation;
// production retains only a maximal catalogue. Keep this independent of the
// pivoted Bron-Kerbosch implementation being checked.
struct IndependentSubsetReference {
  std::vector<pbqp::BitSet<>> sets;
  bool complete = true;
};
IndependentSubsetReference
independent_subsets(std::span<const pbqp::BitSet<>> adjacency) {
  constexpr size_t legacy_subset_limit = 10000;
  IndependentSubsetReference result;
  pbqp::BitSet<> current(adjacency.size());
  std::function<bool(size_t)> enumerate = [&](size_t first) {
    for (size_t node = first; node < adjacency.size(); ++node) {
      if (adjacency[node].intersects(current))
        continue;
      if (result.sets.size() == legacy_subset_limit)
        return false;
      current.set(node);
      result.sets.push_back(current);
      if (!enumerate(node + 1))
        return false;
      current.reset(node);
    }
    return true;
  };
  result.complete = enumerate(0);
  if (!result.complete)
    result.sets.clear();
  return result;
}

void test_independent_sets() {
  std::mt19937 random(29);
  for (size_t count = 0; count <= 8; ++count)
    for (size_t trial = 0; trial < 20; ++trial) {
      context = "independent sets n=" + std::to_string(count);
      std::vector<pbqp::BitSet<>> graph(count, pbqp::BitSet<>(count));
      for (size_t src = 0; src < count; ++src)
        for (size_t tgt = src + 1; tgt < count; ++tgt)
          if (random() % 2) {
            graph[src].set(tgt);
            graph[tgt].set(src);
          }
      const auto reference = independent_subsets(graph);
      check(reference.complete);
      std::vector<size_t> expected, actual;
      for (size_t mask = 1; mask < (size_t{1} << count); ++mask) {
        bool independent = true;
        for (size_t src = 0; src < count; ++src)
          for (size_t tgt = src + 1; tgt < count; ++tgt)
            if ((mask & (size_t{1} << src)) && (mask & (size_t{1} << tgt)) &&
                graph[src].test(tgt))
              independent = false;
        if (independent)
          expected.push_back(mask);
      }
      for (const auto &set : reference.sets) {
        size_t mask = 0;
        for (size_t id = set.first(); id < count; id = set.next(id))
          mask |= size_t{1} << id;
        actual.push_back(mask);
      }
      std::sort(actual.begin(), actual.end());
      check(actual == expected);
      check(reference.sets.size() == expected.size());
    }
  std::vector<pbqp::BitSet<>> empty_edges(65, pbqp::BitSet<>(65));
  const auto incomplete = independent_subsets(empty_edges);
  check(!incomplete.complete && incomplete.sets.empty());
}

size_t pattern_mask(const pbqp::BitSet<> &set) {
  size_t mask = 0;
  for (size_t id = set.first(); id < set.size(); id = set.next(id))
    mask |= size_t{1} << id;
  return mask;
}

// Compare the per-register relaxation on every fixed independent class and
// every remaining allowed mask. Costs never enter this nonnegative capacity.
void compare_capacities(std::span<const pbqp::BitSet<>> all,
                        const pbqp::IndependentSets &maximal, size_t count) {
  constexpr size_t weight_period = 3;
  constexpr size_t emphasized_node = 1;
  constexpr size_t emphasized_weight = 20;
  auto make_weights = [count](auto weight) {
    std::vector<size_t> values(count);
    for (size_t id = 0; id < count; ++id)
      values[id] = weight(id);
    return values;
  };
  const std::vector<size_t> unit_weights(count, 1), zero_weights(count, 0);
  const auto periodic_weights =
      make_weights([](size_t id) { return id % weight_period; });
  const auto quadratic_weights =
      make_weights([](size_t id) { return 1 + id * id; });
  const auto emphasized_weights = make_weights(
      [](size_t id) { return id == emphasized_node ? emphasized_weight : 1; });
  const std::array<std::span<const size_t>, 5> weight_sets{
      unit_weights, periodic_weights, quadratic_weights, zero_weights,
      emphasized_weights};

  const size_t full = (size_t{1} << count) - 1;
  const std::span<const pbqp::BitSet<>> maximal_sets(maximal);
  auto capacity = [](std::span<const pbqp::BitSet<>> sets, size_t fixed,
                     size_t allowed, std::span<const size_t> weights) {
    size_t best = 0;
    for (const auto &set : sets) {
      const size_t mask = pattern_mask(set);
      if ((mask & fixed) != fixed)
        continue;
      size_t score = 0;
      for (size_t id = 0; id < weights.size(); ++id)
        if ((mask & allowed) & (size_t{1} << id))
          score += weights[id];
      best = std::max(best, score);
    }
    return best;
  };
  for (size_t fixed = 0; fixed <= full; ++fixed) {
    if (fixed && std::none_of(all.begin(), all.end(), [&](const auto &set) {
          return pattern_mask(set) == fixed;
        }))
      continue;
    size_t pending = full ^ fixed;
    for (size_t allowed = pending;; allowed = (allowed - 1) & pending) {
      for (const auto weights : weight_sets)
        check(capacity(all, fixed, allowed, weights) ==
              capacity(maximal_sets, fixed, allowed, weights));
      if (!allowed)
        break;
    }
  }
}

void test_maximal_sets() {
  std::mt19937 random(731);
  for (size_t count = 0; count <= 8; ++count)
    for (size_t trial = 0; trial < 20; ++trial) {
      context = "maximal independent sets n=" + std::to_string(count) +
                " trial=" + std::to_string(trial);
      std::vector<pbqp::BitSet<>> graph(count, pbqp::BitSet<>(count));
      for (size_t src = 0; src < count; ++src)
        for (size_t tgt = src + 1; tgt < count; ++tgt)
          // Include isolated, complete, disconnected, and mixed-density graphs.
          if (trial == 1 || (trial > 2 && random() % 3 == 0 &&
                             (trial != 3 || src / 3 == tgt / 3))) {
            graph[src].set(tgt);
            graph[tgt].set(src);
          }
      const auto all = independent_subsets(graph);
      pbqp::IndependentSets maximal;
      maximal.prepare(graph);
      check(all.complete && maximal.complete());
      std::vector<size_t> expected, actual;
      for (size_t mask = 1; mask < (size_t{1} << count); ++mask) {
        bool independent = true, is_maximal = true;
        for (size_t id = 0; id < count; ++id) {
          bool has_neighbor = false;
          for (size_t other = 0; other < count; ++other)
            has_neighbor |=
                (mask & (size_t{1} << other)) && graph[id].test(other);
          if (mask & (size_t{1} << id))
            independent &= !has_neighbor;
          else
            is_maximal &= has_neighbor;
        }
        if (independent && is_maximal)
          expected.push_back(mask);
      }
      size_t entries = 0;
      for (const auto &set : maximal) {
        actual.push_back(pattern_mask(set));
        entries += set.count();
      }
      std::sort(actual.begin(), actual.end());
      check(actual == expected && maximal.num_entries() == entries);
      size_t maximum_size = 0;
      for (const auto &set : all.sets)
        maximum_size = std::max(maximum_size, set.count());
      check(maximal.maximum_size() == maximum_size);
      if (count <= 5)
        compare_capacities(all.sets, maximal, count);
    }
  // Maximal is not maximum-cardinality: the center of a path is a valid
  // singleton maximal set and can be the best set for nonnegative weights.
  std::vector<pbqp::BitSet<>> path(3, pbqp::BitSet<>(3));
  path[0].set(1);
  path[1].set(0);
  path[1].set(2);
  path[2].set(1);
  pbqp::IndependentSets sets;
  sets.prepare(path);
  check(sets.size() == 2);
  check(std::any_of(sets.begin(), sets.end(),
                    [](const auto &set) { return pattern_mask(set) == 2; }));
  const auto all = independent_subsets(path);
  check(all.complete && all.sets.size() == 4);

  std::vector<pbqp::BitSet<>> isolated(129, pbqp::BitSet<>(129));
  pbqp::IndependentSets spanning;
  spanning.prepare(isolated);
  check(spanning.complete() && spanning.size() == 1);
  check(spanning.maximum_size() == isolated.size());
}

void test_maximal_bipartite_bound() {
  for (size_t side : {6u, 12u, 13u, 16u}) {
    context = "maximal bipartite side=" + std::to_string(side);
    pbqp_cost_t unary[] = {1, 0}, matrix[] = {0, 0, 0, PBQP_INF};
    std::vector<pbqp_node> nodes(side * 2, pbqp_node{2, unary});
    std::vector<pbqp_edge> edges;
    std::vector<pbqp::BitSet<>> graph(nodes.size(),
                                      pbqp::BitSet<>(nodes.size()));
    for (size_t src = 0; src < side; ++src)
      for (size_t tgt = side; tgt < nodes.size(); ++tgt) {
        edges.push_back({src, tgt, matrix});
        graph[src].set(tgt);
        graph[tgt].set(src);
      }
    const auto all = independent_subsets(graph);
    check(all.complete == (side <= 12));
    pbqp_problem problem{nodes.size(), edges.size(), nodes.data(),
                         edges.data()};
    pbqp::OptimizedBranchAndBoundBounds bounds(problem);
    bounds.prepare_search();
    bounds.prepare_capacity();
    check(bounds.patterns().complete() && bounds.patterns().size() == 2);
    check(bounds.patterns().maximum_size() == side);
    pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
    pbqp_cost_t lower = 0;
    check(bounds.lower_bound(nullptr, 0, workspace, lower));
    check(lower == side);
  }
}

void test_maximal_budgets() {
  context = "maximal independent set-count budget";
  constexpr size_t count = 27;
  pbqp_cost_t unary[] = {1, 0}, matrix[] = {0, 0, 0, PBQP_INF};
  std::vector<pbqp_node> nodes(count, pbqp_node{2, unary});
  std::vector<pbqp_edge> edges;
  for (size_t group = 0; group < count; group += 3)
    for (size_t src = group; src < group + 3; ++src)
      for (size_t tgt = src + 1; tgt < group + 3; ++tgt)
        edges.push_back({src, tgt, matrix});
  pbqp_problem problem{nodes.size(), edges.size(), nodes.data(), edges.data()};
  pbqp::OptimizedBranchAndBoundBounds bounds(problem);
  bounds.prepare_search();
  DiagnosticLog log;
  bounds.prepare_capacity(pbqp::Diagnostics(record_diagnostic, &log));
  check(bounds.patterns().status() == pbqp::IndependentSetStatus::SetBudget);
  check(bounds.patterns().num_examined() == 10000 && bounds.patterns().empty());
  check(bounds.patterns().num_entries() == 0 &&
        bounds.patterns().maximum_size() == 0);
  check(bounds.weights().status() == pbqp::PricingStatus::IncompleteSets);
  check(log.text.find("partial results discarded") != std::string::npos);
  pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
  pbqp_cost_t lower = 0;
  check(bounds.lower_bound(nullptr, 0, workspace, lower));
  check(lower == 2); // Clique-only relaxation; known optimum is 18 spills.

  for (size_t count : {1000u, 10000u}) {
    context = "maximal preparation budget n=" + std::to_string(count);
    std::vector<pbqp::BitSet<>> graph(count, pbqp::BitSet<>(count));
    pbqp::IndependentSets sets;
    sets.prepare(graph);
    check(!sets.complete() && sets.empty() && sets.num_entries() == 0);
    check(sets.status() == (count == 1000
                                ? pbqp::IndependentSetStatus::WordBudget
                                : pbqp::IndependentSetStatus::MemoryBudget));
    check(sets.work_used() <= pbqp::IndependentSets::word_budget);
  }
}

void test_price_quantization() {
  context = "nearest auxiliary price quantization";
  const std::vector<double> prices{0.124999, 0.125, 0.125001,
                                   0.374999, 0.375, 0.375001};
  auto rounded = pbqp::quantize_prices(prices, 32768);
  check(rounded.status == pbqp::PricingStatus::Enabled && rounded.scale == 4);
  check(rounded.weights == std::vector<size_t>({0, 1, 1, 1, 2, 2}));
  auto bounded = pbqp::quantize_prices({1e300, 1e300, 1e300}, 32768);
  check(bounded.status == pbqp::PricingStatus::Enabled);
  check(bounded.total > 0 && bounded.total <= 32768 && bounded.scale < 4);
  for (size_t id = 0; id < bounded.weights.size(); ++id)
    check(std::abs(bounded.weights[id] - 1e300 * bounded.scale) <= 0.5);
  for (size_t limit : {size_t{0}, size_t{1}, size_t{32769}}) {
    auto rejected = pbqp::quantize_prices({1, 1, 1}, limit);
    check(rejected.status == pbqp::PricingStatus::StateBudget);
    check(!rejected.total && rejected.weights.empty());
  }
  for (auto value : {-1.0, std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN()}) {
    auto rejected = pbqp::quantize_prices({value, 1}, 32768);
    check(rejected.status == pbqp::PricingStatus::NonFinite);
    check(!rejected.total && rejected.weights.empty());
  }
  check(pbqp::quantize_prices({0, 0}, 32768).status ==
        pbqp::PricingStatus::ZeroWeights);
  const auto maximum = std::numeric_limits<double>::max();
  check(pbqp::quantize_prices({maximum, maximum}, 32768).status ==
        pbqp::PricingStatus::NonFinite);
}

void test_pricing_limits() {
  constexpr size_t count = 16;
  std::vector<pbqp::BitSet<>> graph(count, pbqp::BitSet<>(count, true));
  for (size_t id = 0; id < count; ++id)
    graph[id].reset(id);
  pbqp::IndependentSets sets;
  sets.prepare(graph);
  check(sets.complete());
  pbqp_cost_t unary[] = {std::numeric_limits<pbqp_cost_t>::max(), 0};
  std::vector<pbqp_node> nodes(count, pbqp_node{2, unary});
  pbqp_cost_t matrix[] = {0, 0, 0, PBQP_INF};
  std::vector<pbqp_edge> edges;
  for (size_t src = 0; src < count; ++src)
    for (size_t tgt = src + 1; tgt < count; ++tgt)
      edges.push_back({src, tgt, matrix});
  pbqp_problem problem{nodes.size(), edges.size(), nodes.data(), edges.data()};
  pbqp::RegallocPricing pricing;
  pricing.prepare(problem, sets);
  check(pricing.ready() && pricing.total_weight() <= 32768);
  size_t sum = 0;
  for (size_t id = 0; id < count; ++id)
    sum += pricing.weight(id);
  check(sum == pricing.total_weight());
  pbqp::OptimizedBranchAndBoundBounds bounds(problem);
  bounds.prepare_search();
  DiagnosticLog log;
  bounds.prepare_capacity(pbqp::Diagnostics(record_diagnostic, &log));
  check(bounds.weights().ready());
  check(log.text.find("auxiliary weights scaled down") != std::string::npos);
  pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
  pbqp_cost_t lower;
  check(bounds.lower_bound(nullptr, 0, workspace, lower) && lower == PBQP_INF);
  for (auto unsupported : {pbqp_cost_t{-1}, PBQP_INF}) {
    unary[0] = unsupported;
    pbqp::RegallocPricing fallback;
    fallback.prepare(problem, sets);
    check(!fallback.ready() && fallback.total_weight() == 0);
    check(fallback.status() == pbqp::PricingStatus::UnsupportedCost);
    DiagnosticLog rejected;
    pbqp::describe_bnbopt_capacity(
        problem, sets, fallback,
        pbqp::Diagnostics(record_diagnostic, &rejected));
    check(rejected.text.find("requires finite nonnegative spills") !=
          std::string::npos);
  }
}

void test_coalescing_fallback() {
  context = "coalescing is not pure interference";
  // Three conflicts force different colors at the ends of this path, while
  // coalescing rewards equal endpoint colors. Spilling costs more than the
  // reward, so the optimum is zero despite an independent-term bound of -3.
  pbqp_cost_t unary[] = {7, 0, 0};
  pbqp_node nodes[] = {{3, unary}, {3, unary}, {3, unary}, {3, unary}};
  pbqp_cost_t conflict[] = {0, 0, 0, 0, PBQP_INF, 0, 0, 0, PBQP_INF};
  pbqp_cost_t coalesce[] = {0, 0, 0, 0, -3, 0, 0, 0, -3};
  pbqp_edge edges[] = {
      {0, 1, conflict}, {1, 2, conflict}, {2, 3, conflict}, {0, 3, coalesce}};
  pbqp_problem problem{4, 4, nodes, edges};
  pbqp::BranchAndBoundBounds ordinary(problem);
  check(!ordinary.is_pure_interference_problem());
  pbqp::OptimizedBranchAndBoundBounds bounds(problem);
  bounds.prepare_search();
  bounds.prepare_capacity();
  check(!bounds.optimized());
  pbqp::OptimizedBranchAndBoundBounds::Workspace workspace(bounds);
  std::vector<size_t> tuple(4);
  std::function<void(size_t)> prefix = [&](size_t depth) {
    check_prefix(problem, bounds, workspace, tuple, depth);
    if (depth == tuple.size())
      return;
    for (size_t option = 0; option < 3; ++option) {
      tuple[depth] = option;
      prefix(depth + 1);
    }
  };
  prefix(0);
  DiagnosticLog log;
  const pbqp_solve_options options{4, nullptr, nullptr, record_diagnostic,
                                   &log};
  pbqp_solution *raw = nullptr;
  check(pbqp_solve_branch_and_bound_optimized(&problem, &options, &raw,
                                              nullptr) == PBQP_OK);
  pbqp_solution_free(raw);
  check(log.text.find("edge (0, 3) matrix entry [1,1]=-3") !=
        std::string::npos);
  check(log.text.find("generic PBQP bounds") != std::string::npos);
  for (auto solve : {pbqp_solve_bruteforce, pbqp_solve_branch_and_bound,
                     pbqp_solve_branch_and_bound_optimized}) {
    pbqp_solution *raw = nullptr;
    auto status = solve(&problem, &options, &raw, nullptr);
    auto deleter = [](pbqp_solution *s) noexcept { pbqp_solution_free(s); };
    std::unique_ptr<pbqp_solution, decltype(deleter)> solution(raw, deleter);
    check(status == PBQP_OK && solution && solution->cost == 0);
    check(solution->quality == PBQP_SOLUTION_OPTIMAL);
  }
}

} // namespace

namespace pbqp {
extern "C++" void run_bnbopt_internal_tests() {
  test_coalescing_fallback();
  test_independent_sets();
  test_maximal_sets();
  test_maximal_bipartite_bound();
  test_maximal_budgets();
  test_pricing_limits();
  test_price_quantization();
  test_bitsets<unsigned char>();
  test_bitsets<unsigned short>();
  test_bitsets<unsigned>();
  test_bitsets<unsigned long>();
  test_bitsets<std::uint64_t>();
  test_prefix_bounds();
  test_prefix_bounds(true);
  test_bitset_boundaries();
  test_high_register_conflict();
  test_storage_budget_fallback();
  test_full_clique_capacity();
}
} // namespace pbqp
