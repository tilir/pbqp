/*
 * Low-degree differential and sparse reduction regressions.
 *
 * Compares trees, forests, and cycles against exhaustive search using exactly
 * representable weights. Exercises frozen adjacency, incremental RN priorities,
 * neighbor feasibility changes, and independently owned solution storage.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
std::string context;

void check(bool condition,
           std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()) + " " + context);
}

using SolutionPtr =
    std::unique_ptr<pbqp_solution, decltype([](pbqp_solution *p) noexcept {
                      pbqp_solution_free(p);
                    })>;

class Fixture {
  std::vector<std::vector<pbqp_cost_t>> unary;
  std::vector<std::vector<pbqp_cost_t>> pairwise;
  std::vector<pbqp_node> nodes;
  std::vector<pbqp_edge> edges;

public:
  explicit Fixture(size_t count) : unary(count), nodes(count) {}

  Fixture(const Fixture &) = delete;
  Fixture &operator=(const Fixture &) = delete;

  void set_node(size_t id, std::vector<pbqp_cost_t> costs) {
    unary[id] = std::move(costs);
    nodes[id] = {unary[id].size(), unary[id].data()};
  }

  void add_edge(size_t src, size_t tgt, std::vector<pbqp_cost_t> costs) {
    check(src < tgt);
    pairwise.push_back(std::move(costs));
    edges.push_back({src, tgt, pairwise.back().data()});
  }

  void shuffle_edges(std::mt19937 &random) {
    std::shuffle(edges.begin(), edges.end(), random);
  }

  pbqp_problem view() const noexcept {
    return {nodes.size(), edges.size(), nodes.data(), edges.data()};
  }
};

struct Snapshot {
  std::vector<pbqp_node> nodes;
  std::vector<pbqp_edge> edges;
  std::vector<pbqp_cost_t> costs;

  explicit Snapshot(const pbqp_problem &p) {
    if (p.num_nodes)
      nodes.assign(p.nodes, p.nodes + p.num_nodes);
    if (p.num_edges)
      edges.assign(p.edges, p.edges + p.num_edges);
    for (const auto &node : nodes)
      costs.insert(costs.end(), node.costs, node.costs + node.num_options);
    for (const auto &edge : edges) {
      size_t count = nodes[edge.src].num_options * nodes[edge.tgt].num_options;
      costs.insert(costs.end(), edge.costs, edge.costs + count);
    }
  }

  void check_unchanged(const pbqp_problem &p) const {
    Snapshot after(p);
    check(nodes.size() == after.nodes.size() &&
          edges.size() == after.edges.size());
    for (size_t id = 0; id < nodes.size(); ++id)
      check(nodes[id].num_options == after.nodes[id].num_options &&
            nodes[id].costs == after.nodes[id].costs);
    for (size_t id = 0; id < edges.size(); ++id)
      check(edges[id].src == after.edges[id].src &&
            edges[id].tgt == after.edges[id].tgt &&
            edges[id].costs == after.edges[id].costs);
    check(costs == after.costs);
  }
};

SolutionPtr solve_reduction(const pbqp_problem &p) {
  pbqp_solution *raw = nullptr;
  check(pbqp_solve_reduction(&p, nullptr, &raw, nullptr) == PBQP_OK);
  SolutionPtr solution(raw);
  check(solution->quality == PBQP_SOLUTION_FEASIBLE &&
        solution->num_nodes == p.num_nodes);
  pbqp_evaluation evaluation{};
  check(pbqp_evaluate_assignment(&p, solution->options, solution->num_nodes,
                                 &evaluation, nullptr) == PBQP_OK);
  check(evaluation.is_feasible && solution->cost == evaluation.cost);
  return solution;
}

void test_low_degree() {
  for (std::string_view family : {"tree", "forest", "cycle"}) {
    for (unsigned seed = 0; seed < 150; ++seed) {
      context = std::string(family) + " seed=" + std::to_string(seed);
      std::mt19937 random(seed);
      size_t count = seed % 9;
      auto fixture = std::make_unique<Fixture>(count);
      std::vector<size_t> domains(count), labels(count);
      std::iota(labels.begin(), labels.end(), size_t{0});
      std::shuffle(labels.begin(), labels.end(), random);
      auto cost = [&]() -> pbqp_cost_t {
        if (random() % 7 == 0)
          return PBQP_INF;
        return (static_cast<int>(random() % 65) - 32) * 0.25f;
      };
      for (size_t id = 0; id < count; ++id) {
        domains[id] = 1 + random() % 3;
        std::vector<pbqp_cost_t> costs(domains[id]);
        std::generate(costs.begin(), costs.end(), cost);
        if (seed % 2)
          costs[0] = 0;
        fixture->set_node(id, std::move(costs));
      }
      auto add = [&](size_t a, size_t b) {
        size_t src = std::min(labels[a], labels[b]);
        size_t tgt = std::max(labels[a], labels[b]);
        std::vector<pbqp_cost_t> costs(domains[src] * domains[tgt]);
        std::generate(costs.begin(), costs.end(), cost);
        if (seed % 2)
          costs[0] = 0;
        fixture->add_edge(src, tgt, std::move(costs));
      };
      for (size_t id = 1; id < count; ++id) {
        if (family == "forest" && id % 3 == 0)
          continue;
        size_t parent = family == "cycle" ? id - 1 : random() % id;
        add(parent, id);
      }
      if (family == "cycle" && count > 2)
        add(0, count - 1);
      fixture->shuffle_edges(random);
      auto p = fixture->view();
      Snapshot snapshot(p);
      pbqp_solution *exact_raw = nullptr, *reduced_raw = nullptr;
      auto exact_status =
          pbqp_solve_bruteforce(&p, nullptr, &exact_raw, nullptr);
      auto reduced_status =
          pbqp_solve_reduction(&p, nullptr, &reduced_raw, nullptr);
      SolutionPtr exact(exact_raw), reduced(reduced_raw);
      snapshot.check_unchanged(p);
      if (exact_status == PBQP_INFEASIBLE) {
        check(!exact && !reduced && reduced_status == PBQP_NO_SOLUTION);
        continue;
      }
      check(exact_status == PBQP_OK && reduced_status == PBQP_OK && exact &&
            reduced);
      check(reduced->quality == PBQP_SOLUTION_FEASIBLE &&
            reduced->cost == exact->cost);
      pbqp_evaluation evaluation{};
      check(pbqp_evaluate_assignment(&p, reduced->options, reduced->num_nodes,
                                     &evaluation, nullptr) == PBQP_OK);
      check(evaluation.is_feasible && evaluation.cost == reduced->cost);
      std::vector<size_t> options;
      if (count)
        options.assign(reduced->options, reduced->options + count);
      fixture.reset();
      // Reading every option after input destruction also runs under ASan.
      check(reduced->num_nodes == count);
      for (size_t id = 0; id < count; ++id)
        check(reduced->options[id] == options[id]);
    }
  }
}

void test_worklist_invalidation() {
  context = "RN neighbor feasibility invalidation";
  Fixture neighbor(5);
  neighbor.set_node(0, {0});
  neighbor.set_node(1, {5, 0});
  for (size_t id = 2; id < 5; ++id)
    neighbor.set_node(id, {1, 0});
  neighbor.add_edge(0, 1, {0, PBQP_INF});
  neighbor.add_edge(1, 2, {0, 0, 0, PBQP_INF});
  neighbor.add_edge(1, 3, {0, 0, 0, 0});
  neighbor.add_edge(1, 4, {0, 0, 0, 0});
  neighbor.add_edge(2, 3, {0, 0, 0, 10});
  neighbor.add_edge(2, 4, {0, 0, 0, 0});
  neighbor.add_edge(3, 4, {0, 0, 0, 0});
  auto solution = solve_reduction(neighbor.view());
  // Leaf 0 forbids node 1's register. Node 2 must become the first safe RN
  // candidate, ahead of node 3, even though its own feasibility did not change.
  const size_t expected[] = {0, 0, 0, 1, 1};
  check(solution->cost == 6 &&
        std::equal(expected, expected + 5, solution->options));

  context = "RN spill priority invalidation";
  Fixture priority(5);
  priority.set_node(0, {0});
  for (size_t id = 1; id < 5; ++id)
    priority.set_node(id, {1, 0});
  priority.add_edge(0, 1, {9, 0});
  for (size_t src = 1; src < 5; ++src)
    for (size_t tgt = src + 1; tgt < 5; ++tgt)
      priority.add_edge(src, tgt, {0, 0, 0, PBQP_INF});
  solution = solve_reduction(priority.view());
  // R1 increases only a finite score. RN must stop treating node 1 as cheap.
  check(solution->cost == 3 && solution->options[1] == 1);

  context = "frozen edge arithmetic order";
  Fixture ordered(5);
  ordered.set_node(0, {0});
  ordered.set_node(1, {0, 0});
  for (size_t id = 2; id < 5; ++id)
    ordered.set_node(id, {0});
  ordered.add_edge(0, 1, {0, 0});
  ordered.add_edge(1, 2, {16777216, 0.5f});
  ordered.add_edge(1, 3, {1, 0});
  ordered.add_edge(1, 4, {-16777216, 0});
  ordered.add_edge(2, 3, {0});
  ordered.add_edge(2, 4, {0});
  ordered.add_edge(3, 4, {0});
  // Removing leaf 0 swaps node 1's adjacency. Freezing must restore edge-ID
  // order: (16777216 + 1) - 16777216 is 0, but regrouping can make it 1.
  solution = solve_reduction(ordered.view());
  check(solution->cost == 0 && solution->options[1] == 0);
}

void test_sparse_shapes() {
  constexpr size_t count = 4096;
  std::mt19937 random(42);
  for (size_t center : {size_t{0}, count / 2, count - 1}) {
    context = "star center=" + std::to_string(center);
    Fixture star(count);
    for (size_t id = 0; id < count; ++id)
      star.set_node(id, {1, 0});
    for (size_t id = 0; id < count; ++id)
      if (id != center)
        star.add_edge(std::min(center, id), std::max(center, id),
                      {0, 0, 0, PBQP_INF});
    star.shuffle_edges(random);
    auto solution = solve_reduction(star.view());
    check(solution->cost == 1);
  }
  context = "disjoint K4 components";
  Fixture components(count);
  for (size_t id = 0; id < count; ++id)
    components.set_node(id, {1, 0});
  for (size_t base = 0; base < count; base += 4)
    for (size_t src = base; src < base + 4; ++src)
      for (size_t tgt = src + 1; tgt < base + 4; ++tgt)
        components.add_edge(src, tgt, {0, 0, 0, PBQP_INF});
  components.shuffle_edges(random);
  auto solution = solve_reduction(components.view());
  check(solution->cost == 3 * count / 4);

  constexpr size_t arms = 2048;
  constexpr size_t size = 2 * arms + 1;
  for (size_t center : {size_t{0}, arms, size - 1}) {
    context = "subdivided star center=" + std::to_string(center);
    Fixture star(size);
    for (size_t id = 0; id < size; ++id)
      star.set_node(id, {1, 0});
    auto mapped = [&](size_t id) {
      if (id == 0)
        return center;
      return id == center ? size_t{0} : id;
    };
    auto add = [&](size_t src, size_t tgt) {
      src = mapped(src);
      tgt = mapped(tgt);
      star.add_edge(std::min(src, tgt), std::max(src, tgt),
                    {0, 0, 0, PBQP_INF});
    };
    for (size_t id = 1; id <= arms; ++id) {
      add(0, id);
      add(id, arms + id);
    }
    star.shuffle_edges(random);
    solution = solve_reduction(star.view());
    check(solution->cost == arms);
  }
}
} // namespace

int main() {
  try {
    test_low_degree();
    test_worklist_invalidation();
    test_sparse_shapes();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
