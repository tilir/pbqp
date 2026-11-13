/*
 * Parallel exact-solver ownership, progress, and prefix regressions.
 *
 * Uses small independent exhaustive searches for cost checks, repeated worker
 * schedules, and huge spaces that close by pruning. Callbacks verify serialized
 * monotone reporting without relying on a particular worker thread or winner.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <atomic>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

import pbqp.generator;

namespace {
std::string context;
void check(bool condition,
           std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string(location.file_name()) + ":" +
                             std::to_string(location.line()) + " " + context);
}
using SolutionPtr =
    std::unique_ptr<pbqp_solution, decltype([](pbqp_solution *s) noexcept {
                      pbqp_solution_free(s);
                    })>;
struct Progress {
  std::atomic<bool> active{false};
  std::atomic<unsigned> count{0};
  std::atomic<bool> invalid{false};
};
void report(unsigned percent, void *data) noexcept {
  auto &log = *static_cast<Progress *>(data);
  if (log.active.exchange(true))
    log.invalid = true;
  if (percent != log.count.fetch_add(1) + 1 || percent > 100)
    log.invalid = true;
  log.active = false;
}

void verify(const pbqp_problem &problem, size_t threads, bool naive = false,
            bool optimized = false) {
  pbqp_solution *raw = nullptr;
  auto expected = pbqp_solve_bruteforce(&problem, &raw, nullptr);
  SolutionPtr exact(raw);
  Progress progress;
  const pbqp_solve_options options{threads, report, &progress};
  pbqp_error error{42, "old error"};
  auto solve =
      naive ? pbqp_solve_bruteforce_with_options
            : (optimized ? pbqp_solve_branch_and_bound_optimized_with_options
                         : pbqp_solve_branch_and_bound_with_options);
  auto status = solve(&problem, &options, &raw, &error);
  SolutionPtr solution(raw);
  check(status == expected && !progress.invalid && progress.count == 100);
  if (status != PBQP_OK) {
    check(!solution && status == PBQP_INFEASIBLE && error.message[0]);
    return;
  }
  check(!error.line && !error.message[0]);
  check(solution->cost == exact->cost &&
        solution->quality == PBQP_SOLUTION_OPTIMAL);
  if (naive)
    for (size_t id = 0; id < problem.num_nodes; ++id)
      check(solution->options[id] == exact->options[id]);
  pbqp_evaluation evaluation{};
  check(pbqp_evaluate_assignment(&problem, solution->options,
                                 solution->num_nodes, &evaluation,
                                 nullptr) == PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == solution->cost);
}

void test_small() {
  for (unsigned seed = 0; seed < 80; ++seed) {
    context = "parallel seed=" + std::to_string(seed);
    size_t count = seed % 7;
    size_t pairs = count ? count * (count - 1) / 2 : 0;
    pbqp_tools::GeneratorOptions options{count, seed % (pairs + 1), 4, seed};
    options.family = seed % 2 ? pbqp_tools::GeneratorFamily::Planted
                              : pbqp_tools::GeneratorFamily::Random;
    auto owner = pbqp_tools::generate_problem(options);
    auto problem = owner.view();
    for (size_t threads : {size_t{1}, size_t{2}, size_t{4}, size_t{8}}) {
      verify(problem, threads);
      verify(problem, threads, false, true);
      verify(problem, threads, true);
    }
  }
  // A dense finite-cost problem leaves a substantial frontier for the pool,
  // rather than completing entirely while planning tiny test problems.
  std::mt19937 random(342);
  std::vector<std::vector<pbqp_cost_t>> dense_unary(
      10, std::vector<pbqp_cost_t>(3));
  std::vector<std::vector<pbqp_cost_t>> matrices(45,
                                                 std::vector<pbqp_cost_t>(9));
  std::vector<pbqp_node> dense_nodes;
  std::vector<pbqp_edge> dense_edges;
  for (auto &costs : dense_unary) {
    for (auto &cost : costs)
      cost = static_cast<int>(random() % 17) - 8;
    dense_nodes.push_back({3, costs.data()});
  }
  for (size_t src = 0; src < dense_unary.size(); ++src)
    for (size_t tgt = src + 1; tgt < dense_unary.size(); ++tgt) {
      auto &costs = matrices[dense_edges.size()];
      for (auto &cost : costs)
        cost = static_cast<int>(random() % 17) - 8;
      dense_edges.push_back({src, tgt, costs.data()});
    }
  pbqp_problem dense{dense_nodes.size(), dense_edges.size(), dense_nodes.data(),
                     dense_edges.data()};
  for (size_t threads : {size_t{2}, size_t{4}, size_t{8}})
    verify(dense, threads);
  // Overflowed feasible costs remain distinct from explicit prohibitions.
  const auto maximum = std::numeric_limits<pbqp_cost_t>::max();
  pbqp_cost_t unary[] = {maximum, maximum};
  pbqp_node nodes[] = {{2, unary}, {2, unary}};
  pbqp_cost_t pair[] = {PBQP_INF, PBQP_INF, PBQP_INF, 0};
  pbqp_edge edge{0, 1, pair};
  pbqp_problem overflow{2, 1, nodes, &edge};
  verify(overflow, 4);

  // A greedy trap and an independent RN trap defeat both initial heuristics,
  // while the combined problem still has a feasible assignment.
  pbqp_cost_t left[] = {0, 1}, zero[] = {0}, core[] = {0, 0}, other[] = {1, 1};
  pbqp_node unseeded_nodes[] = {{2, left},  {1, zero},  {2, core},
                                {2, other}, {2, other}, {2, other}};
  pbqp_cost_t trap[] = {PBQP_INF, 0};
  pbqp_cost_t unequal[] = {PBQP_INF, 0, 0, PBQP_INF};
  pbqp_cost_t equal[] = {0, PBQP_INF, PBQP_INF, 0};
  pbqp_cost_t any[] = {0, 0, 0, 0};
  pbqp_edge unseeded_edges[] = {{0, 1, trap},  {2, 3, unequal}, {2, 4, equal},
                                {2, 5, equal}, {3, 4, any},     {3, 5, any},
                                {4, 5, any}};
  pbqp_problem unseeded{6, 7, unseeded_nodes, unseeded_edges};
  pbqp_solution *missing = nullptr;
  check(pbqp_solve_greedy(&unseeded, &missing, nullptr) == PBQP_NO_SOLUTION &&
        !missing);
  check(pbqp_solve_reduction(&unseeded, &missing, nullptr) ==
            PBQP_NO_SOLUTION &&
        !missing);
  verify(unseeded, 4);
  pbqp_problem empty{};
  verify(empty, 8);
}

void test_large_progress() {
  pbqp_cost_t zero_costs[] = {0, 0, 0, 0};
  pbqp_node tied_nodes[] = {
      {2, zero_costs}, {3, zero_costs}, {1, zero_costs}, {4, zero_costs}};
  pbqp_problem tied{4, 0, tied_nodes, nullptr};
  for (unsigned repeat = 0; repeat < 10; ++repeat)
    verify(tied, 4, true);
  std::vector<pbqp_cost_t> values(1003);
  for (size_t id = 0; id < values.size(); ++id)
    values[id] = static_cast<pbqp_cost_t>(id);
  pbqp_cost_t zero[] = {0, 0};
  pbqp_node pair_nodes[] = {{values.size(), values.data()}, {2, zero}};
  std::vector<pbqp_cost_t> matrix(values.size() * 2);
  for (size_t row = 0; row < values.size(); ++row)
    matrix[row * 2] = matrix[row * 2 + 1] = -values[row];
  pbqp_edge edge{0, 1, matrix.data()};
  pbqp_problem large_domain{2, 1, pair_nodes, &edge};
  verify(large_domain, 4);
  verify(large_domain, 4, true);
  for (bool forbidden : {false, true}) {
    pbqp_cost_t costs[] = {forbidden ? PBQP_INF : 0, forbidden ? PBQP_INF : 0};
    std::vector<pbqp_node> nodes(65, pbqp_node{2, costs});
    pbqp_problem problem{nodes.size(), 0, nodes.data(), nullptr};
    Progress progress;
    const pbqp_solve_options options{4, report, &progress};
    pbqp_solution *raw = nullptr;
    auto status = pbqp_solve_branch_and_bound_with_options(&problem, &options,
                                                           &raw, nullptr);
    SolutionPtr solution(raw);
    check(status == (forbidden ? PBQP_INFEASIBLE : PBQP_OK));
    check(progress.count == 100 && !progress.invalid);
  }
  // Worker batches merge counts in a 2^65 space, including long carries. Eight
  // free prefix bits fan out jobs; unary constraints close huge suffixes. A
  // frustrated finite triangle keeps the root bound below the incumbent.
  pbqp_cost_t free[] = {0, 0}, restricted[] = {0, PBQP_INF};
  std::vector<pbqp_node> nodes(65, pbqp_node{2, restricted});
  for (size_t id = 0; id < nodes.size(); ++id)
    if (id < 8 || id >= 62)
      nodes[id].costs = free;
  pbqp_cost_t equal[] = {0, 1, 1, 0}, unequal[] = {1, 0, 0, 1};
  pbqp_edge edges[] = {{62, 63, equal}, {62, 64, unequal}, {63, 64, equal}};
  pbqp_problem problem{nodes.size(), 3, nodes.data(), edges};
  Progress progress;
  const pbqp_solve_options options{4, report, &progress};
  pbqp_solution *raw = nullptr;
  check(pbqp_solve_branch_and_bound_with_options(&problem, &options, &raw,
                                                 nullptr) == PBQP_OK);
  SolutionPtr solution(raw);
  check(solution->cost == 1 && solution->quality == PBQP_SOLUTION_OPTIMAL);
  check(progress.count == 100 && !progress.invalid);
}

void test_progress_batches() {
  // Every worker crosses batch boundaries inside a job. Heterogeneous radices
  // exercise carries on merging both full batches and partial job tails.
  pbqp_cost_t costs[] = {1, 0, PBQP_INF};
  std::vector<pbqp_node> nodes;
  for (size_t id = 0; id < 20; ++id)
    nodes.push_back(
        {id % 5 == 0 ? size_t{1} : (id % 5 == 1 ? size_t{3} : size_t{2}),
         costs});
  pbqp_problem problem{nodes.size(), 0, nodes.data(), nullptr};
  context = "batched mixed-radix progress";
  for (size_t threads : {size_t{2}, size_t{4}, size_t{16}})
    verify(problem, threads, true);
  pbqp_cost_t forbidden[] = {PBQP_INF};
  nodes.back() = {1, forbidden};
  verify(problem, 4, true);
}

void test_singleton_prefix() {
  context = "long singleton prefix followed by a binary suffix";
  pbqp_cost_t costs[] = {1, 0};
  std::vector<pbqp_node> nodes(1024, pbqp_node{1, costs});
  nodes.insert(nodes.end(), 8, pbqp_node{2, costs});
  pbqp_problem problem{nodes.size(), 0, nodes.data(), nullptr};
  verify(problem, 4, true);
  verify(problem, 4);
}

void test_optimized_regalloc() {
  for (unsigned seed = 0; seed < 60; ++seed) {
    size_t count = seed % 8;
    size_t pairs = count ? count * (count - 1) / 2 : 0;
    pbqp_tools::GeneratorOptions options{count, seed % (pairs + 1), 0, seed};
    options.family = pbqp_tools::GeneratorFamily::RegAlloc;
    options.num_registers = 1 + seed % 3;
    options.spill_cost_min = 0.125f;
    options.spill_cost_max = 17.375f;
    auto owner = pbqp_tools::generate_problem(options);
    auto problem = owner.view();
    context = "bnbopt regalloc seed=" + std::to_string(seed);
    for (size_t threads : {size_t{1}, size_t{4}, size_t{8}})
      verify(problem, threads, false, true);
  }
  // Independent triangles require four spills. A single clique relaxation
  // cannot account for every component, leaving actual subtree work to do.
  pbqp_cost_t unary[] = {1, 0, 0};
  pbqp_cost_t matrix[] = {0, 0, 0, 0, PBQP_INF, 0, 0, 0, PBQP_INF};
  std::vector<pbqp_node> nodes(12, pbqp_node{3, unary});
  std::vector<pbqp_edge> edges;
  for (size_t base = 0; base < 12; base += 3)
    for (size_t src = base; src < base + 3; ++src)
      for (size_t tgt = src + 1; tgt < base + 3; ++tgt)
        edges.push_back({src, tgt, matrix});
  pbqp_problem problem{nodes.size(), edges.size(), nodes.data(), edges.data()};
  verify(problem, 4, false, true);

  const auto maximum = std::numeric_limits<pbqp_cost_t>::max();
  pbqp_cost_t positive[] = {maximum, 0};
  pbqp_cost_t negative[] = {-maximum, 0};
  pbqp_cost_t forbidden[] = {PBQP_INF, 0}, ordinary[] = {1, 0};
  pbqp_node triangle[] = {{2, positive}, {2, positive}, {2, positive}};
  pbqp_cost_t pair[] = {0, 0, 0, PBQP_INF};
  pbqp_edge triangle_edges[] = {{0, 1, pair}, {0, 2, pair}, {1, 2, pair}};
  pbqp_problem numeric{3, 3, triangle, triangle_edges};
  // The clique requires two spills. +INF is a reachable optimum, rather than
  // an unreachable DP state or a proof of infeasibility.
  verify(numeric, 4, false, true);
  triangle[2].costs = negative;
  verify(numeric, 4, false, true);
  triangle[0].costs = triangle[1].costs = forbidden;
  triangle[2].costs = ordinary;
  verify(numeric, 4, false, true);

  pbqp_cost_t hard[] = {PBQP_INF, 0, 0};
  pbqp_node hard_nodes[] = {{3, hard}, {3, hard}, {3, hard}};
  pbqp_edge hard_edges[] = {{0, 1, matrix}, {0, 2, matrix}, {1, 2, matrix}};
  pbqp_problem impossible{3, 3, hard_nodes, hard_edges};
  // Every pair can use different registers, but three mutually interfering
  // nodes cannot fit in two registers when no spill is allowed.
  verify(impossible, 4, false, true);

  // Four odd cycles need at least one spill each with two registers. Cliques
  // have size two, so this keeps the root bound loose and exercises workers
  // without an expensive exhaustive oracle or a wall-clock assertion.
  std::vector<pbqp_node> cycles(20, pbqp_node{3, unary});
  std::vector<pbqp_edge> cycle_edges;
  for (size_t base = 0; base < cycles.size(); base += 5) {
    for (size_t id = base; id < base + 4; ++id)
      cycle_edges.push_back({id, id + 1, matrix});
    cycle_edges.push_back({base, base + 4, matrix});
  }
  pbqp_problem odd_cycles{cycles.size(), cycle_edges.size(), cycles.data(),
                          cycle_edges.data()};
  Progress progress;
  pbqp_solve_options configuration{4, report, &progress};
  pbqp_solution *raw = nullptr;
  check(pbqp_solve_branch_and_bound_optimized_with_options(
            &odd_cycles, &configuration, &raw, nullptr) == PBQP_OK);
  SolutionPtr solution(raw);
  check(solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == 4);
  pbqp_evaluation evaluation{};
  check(pbqp_evaluate_assignment(&odd_cycles, solution->options,
                                 solution->num_nodes, &evaluation,
                                 nullptr) == PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == 4);
  check(progress.count == 100 && !progress.invalid);
}

void test_contract() {
  pbqp_problem empty{};
  pbqp_solution *raw = nullptr;
  const pbqp_solve_options options{4, nullptr, nullptr};
  for (auto solve : {pbqp_solve_branch_and_bound_with_options,
                     pbqp_solve_branch_and_bound_optimized_with_options,
                     pbqp_solve_bruteforce_with_options}) {
    check(solve(&empty, nullptr, &raw, nullptr) == PBQP_OK);
    SolutionPtr solution(raw);
    for (size_t threads :
         {size_t{0},
          static_cast<size_t>(std::numeric_limits<int>::max()) + 1}) {
      Progress progress;
      const pbqp_solve_options invalid{threads, report, &progress};
      raw = solution.get();
      pbqp_error error;
      check(solve(&empty, &invalid, &raw, &error) == PBQP_INVALID);
      check(!raw && error.message[0] && !error.line && progress.count == 0);
    }
    check(solve(nullptr, &options, &raw, nullptr) == PBQP_INVALID && !raw);
    check(solve(&empty, &options, nullptr, nullptr) == PBQP_INVALID);
  }

  pbqp_solution *owned = nullptr;
  {
    pbqp_tools::GeneratorOptions settings{6, 15, 3, 42};
    settings.family = pbqp_tools::GeneratorFamily::RegAlloc;
    settings.num_registers = 2;
    auto owner = pbqp_tools::generate_problem(settings);
    auto view = owner.view();
    check(pbqp_solve_branch_and_bound_with_options(&view, &options, &owned,
                                                   nullptr) == PBQP_OK);
  }
  SolutionPtr independent(owned);
  check(independent->num_nodes == 6 &&
        independent->quality == PBQP_SOLUTION_OPTIMAL);
  for (size_t id = 0; id < independent->num_nodes; ++id)
    check(independent->options[id] < 3);
}
} // namespace

int main() {
  try {
    test_small();
    test_large_progress();
    test_progress_batches();
    test_singleton_prefix();
    test_contract();
    test_optimized_regalloc();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
