/*
 * DSATUR/tabu C API regression, fallback, ownership, and exact-seed checks.
 * Generated coloring witnesses replace a dependency on the user's local 11.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>
import pbqp.generator;

namespace pbqp {
void run_dsatur_internal_tests();
}

namespace {
void check(bool value,
           std::source_location location = std::source_location::current()) {
  if (!value)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()));
}

using SolutionPtr =
    std::unique_ptr<pbqp_solution,
                    decltype([](pbqp_solution *s) { pbqp_solution_free(s); })>;

void log(const char *message, void *data) noexcept {
  *static_cast<std::string *>(data) += std::string(message) + '\n';
}

SolutionPtr solve(const pbqp_problem &problem, std::string &diagnostics) {
  pbqp_solution *raw = nullptr;
  const pbqp_option entry[] = {
      pbqp_diagnostic_option("diagnostics", log),
      pbqp_pointer_option("diagnostic-data", &diagnostics)};
  const pbqp_solve_options options{2, entry};
  pbqp_error error;
  check(pbqp_solve_dsatur(&problem, &options, &raw, &error) == PBQP_OK);
  SolutionPtr result(raw);
  check(result->quality == PBQP_SOLUTION_FEASIBLE && !error.line &&
        !error.message[0]);
  pbqp_evaluation evaluation{};
  check(pbqp_evaluate_assignment(&problem, result->options, result->num_nodes,
                                 &evaluation, &error) == PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == result->cost);
  return result;
}

void test_coloring_seed() {
  SolutionPtr independent;
  {
    pbqp_tools::GeneratorOptions config{180, 10000, 0, 1};
    config.family = pbqp_tools::GeneratorFamily::RegAlloc;
    config.num_registers = 32;
    const auto generated = pbqp_tools::generate_problem(config);
    const auto problem = generated.view();
    std::string messages;
    independent = solve(problem, messages);
    check(independent->cost == 0);
    check(messages.find("full register coloring found") != std::string::npos);
    for (size_t edge = 0; edge < problem.num_edges; ++edge) {
      const auto &e = problem.edges[edge];
      check(independent->options[e.src] != independent->options[e.tgt]);
    }
    // Enable only this seed on O0: it must work without the catalogue or the
    // separate register-symmetry/structural-order optimization flags.
    messages.clear();
    const pbqp_option entries[] = {
        pbqp_size_option("optimization-level", 0),
        pbqp_bool_option("dsatur-seed", 1),
        pbqp_diagnostic_option("diagnostics", log),
        pbqp_pointer_option("diagnostic-data", &messages)};
    const pbqp_solve_options options{4, entries};
    pbqp_solution *raw = nullptr;
    check(pbqp_solve(PBQP_SOLVER_BNB, &problem, &options, &raw, nullptr) ==
          PBQP_OK);
    SolutionPtr exact(raw);
    check(exact->cost == 0 && exact->quality == PBQP_SOLUTION_OPTIMAL);
    check(messages.find("bnb: dsatur seed: full register coloring found") !=
          std::string::npos);
    check(messages.find("prepared workers=0") != std::string::npos);
  }
  check(independent && independent->num_nodes == 180 && independent->cost == 0);
  for (size_t node = 0; node < independent->num_nodes; ++node)
    check(independent->options[node] > 0 && independent->options[node] <= 32);
}

void test_repairs() {
  for (auto spill : {pbqp_cost_t{1}, pbqp_cost_t{-2},
                     std::numeric_limits<pbqp_cost_t>::max()}) {
    const pbqp_cost_t costs[] = {spill, 0};
    const pbqp_node nodes[] = {{2, costs}, {2, costs}, {2, costs}};
    const pbqp_cost_t matrix[] = {0, 0, 0, PBQP_INF};
    const pbqp_edge edges[] = {{1, 2, matrix}, {0, 2, matrix}, {0, 1, matrix}};
    const pbqp_problem problem{3, 3, nodes, edges};
    std::string messages;
    auto result = solve(problem, messages);
    check(result->cost == (spill < 0 ? -6 : pbqp_cost_add(spill, spill)));
    check(messages.find("structural path enabled") != std::string::npos);
    check(costs[0] == spill && matrix[3] == PBQP_INF);
  }
  const pbqp_cost_t impossible[] = {PBQP_INF, 0};
  const pbqp_node nodes[] = {{2, impossible}, {2, impossible}};
  const pbqp_cost_t matrix[] = {0, 0, 0, PBQP_INF};
  const pbqp_edge edge{0, 1, matrix};
  const pbqp_problem problem{2, 1, nodes, &edge};
  pbqp_solution *raw = nullptr;
  pbqp_error error;
  check(pbqp_solve_dsatur(&problem, nullptr, &raw, &error) == PBQP_NO_SOLUTION);
  check(!raw && error.message[0]);
  const pbqp_cost_t two_registers[] = {PBQP_INF, 0, 0};
  const pbqp_node tree_nodes[] = {{3, two_registers},
                                  {3, two_registers},
                                  {3, two_registers},
                                  {3, two_registers}};
  const pbqp_cost_t tree_matrix[] = {0, 0, 0, 0, PBQP_INF, 0, 0, 0, PBQP_INF};
  const pbqp_edge tree_edges[] = {
      {0, 2, tree_matrix}, {2, 3, tree_matrix}, {1, 3, tree_matrix}};
  const pbqp_problem tree{4, 3, tree_nodes, tree_edges};
  check(pbqp_solve_greedy(&tree, nullptr, &raw, &error) == PBQP_NO_SOLUTION &&
        !raw);
  std::string messages;
  auto rescued = solve(tree, messages);
  check(rescued->cost == 0);
  check(pbqp_solve_dsatur(nullptr, nullptr, &raw, &error) == PBQP_INVALID &&
        !raw);
  check(pbqp_solve_dsatur(&problem, nullptr, nullptr, &error) == PBQP_INVALID);
}

void test_fallbacks() {
  const pbqp_cost_t a[] = {1, 0}, b[] = {0, -1, 3};
  const pbqp_cost_t matrix[] = {0, 0, 0, 0, 0, 0};
  const pbqp_node nodes[] = {{2, a}, {3, b}};
  const pbqp_edge edge{0, 1, matrix};
  const pbqp_problem problem{2, 1, nodes, &edge};
  std::string messages;
  auto result = solve(problem, messages);
  check(messages.find("unequal domains") != std::string::npos &&
        result->cost == -1);
  const pbqp_node pair[] = {{2, a}, {2, a}};
  const pbqp_cost_t coalescing[] = {0, 1, 1, 0};
  const pbqp_edge cost_edge{0, 1, coalescing};
  const pbqp_problem general{2, 1, pair, &cost_edge};
  messages.clear();
  result = solve(general, messages);
  check(messages.find("general/coalescing matrix") != std::string::npos);

  // The model checker accepts this graph, but dense color/tabu scratch does
  // not fit. Falling back must not allocate that forbidden scratch first.
  std::vector<pbqp_cost_t> large(1'100'001, 0);
  large[0] = 1;
  const pbqp_node large_nodes[] = {{large.size(), large.data()},
                                   {large.size(), large.data()}};
  const pbqp_problem big{2, 0, large_nodes, nullptr};
  messages.clear();
  result = solve(big, messages);
  check(result->cost == 0 &&
        messages.find("memory budget") != std::string::npos);
}

void test_small_generated() {
  for (size_t seed = 0; seed < 30; ++seed) {
    pbqp_tools::GeneratorOptions config{6, seed % 16, 3, seed};
    config.family = pbqp_tools::GeneratorFamily::RegAlloc;
    config.num_registers = 1 + seed % 4;
    auto generated = pbqp_tools::generate_problem(config);
    const auto problem = generated.view();
    std::string messages;
    auto result = solve(problem, messages);
    pbqp_solution *raw = nullptr;
    check(pbqp_solve_bruteforce(&problem, nullptr, &raw, nullptr) == PBQP_OK);
    SolutionPtr optimum(raw);
    check(result->cost >= optimum->cost);
    check(pbqp_solve_greedy(&problem, nullptr, &raw, nullptr) == PBQP_OK);
    SolutionPtr greedy(raw);
    check(result->cost <= greedy->cost);
  }
  std::vector<pbqp_cost_t> costs(71, 0), matrix(71 * 71, 0);
  costs[0] = 1;
  for (size_t color = 1; color <= 70; ++color)
    matrix[color * 71 + color] = PBQP_INF;
  const std::vector<pbqp_node> nodes(70, pbqp_node{costs.size(), costs.data()});
  std::vector<pbqp_edge> edges;
  for (size_t src = 0; src < nodes.size(); ++src)
    for (size_t tgt = src + 1; tgt < nodes.size(); ++tgt)
      edges.push_back({src, tgt, matrix.data()});
  const pbqp_problem problem{nodes.size(), edges.size(), nodes.data(),
                             edges.data()};
  std::string messages;
  auto coloring = solve(problem, messages);
  check(coloring->cost == 0 &&
        *std::max_element(coloring->options,
                          coloring->options + coloring->num_nodes) == 70);
}
} // namespace

int main() {
  try {
    test_coloring_seed();
    test_repairs();
    test_fallbacks();
    test_small_generated();
    pbqp::run_dsatur_internal_tests();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
