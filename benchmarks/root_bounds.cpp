/*
 * Google Benchmark root proofs that must avoid expensive reduction seeding.
 *
 * Zero-cost paths cover both cubic R2 work and quadratic fill amplification
 * from a one-option center. Input construction stays outside measured loops.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <benchmark/benchmark.h>
#include <memory>
#include <vector>

namespace {
using SolutionPtr =
    std::unique_ptr<pbqp_solution, decltype([](pbqp_solution *s) noexcept {
                      pbqp_solution_free(s);
                    })>;

void root_solved(benchmark::State &state) {
  size_t domain = static_cast<size_t>(state.range(0));
  size_t center = state.range(1) ? 1 : domain;
  std::vector<pbqp_cost_t> unary(domain);
  std::vector<pbqp_cost_t> matrix(center * domain);
  pbqp_node nodes[] = {
      {center, unary.data()}, {domain, unary.data()}, {domain, unary.data()}};
  pbqp_edge edges[] = {{0, 1, matrix.data()}, {0, 2, matrix.data()}};
  pbqp_problem problem{3, 2, nodes, edges};
  pbqp_error error{};
  pbqp_solution *raw = nullptr;
  auto status = pbqp_solve_branch_and_bound(&problem, nullptr, &raw, &error);
  SolutionPtr checked(raw);
  if (status != PBQP_OK) {
    state.SkipWithError(error.message);
    return;
  }
  pbqp_evaluation evaluation{};
  if (checked->quality != PBQP_SOLUTION_OPTIMAL || checked->cost != 0 ||
      pbqp_evaluate_assignment(&problem, checked->options, checked->num_nodes,
                               &evaluation, &error) != PBQP_OK ||
      !evaluation.is_feasible || evaluation.cost != 0) {
    state.SkipWithError("invalid root-solved optimum");
    return;
  }
  checked.reset();
  for (auto iteration : state) {
    status = pbqp_solve_branch_and_bound(&problem, nullptr, &raw, &error);
    SolutionPtr solution(raw);
    if (status != PBQP_OK) {
      state.SkipWithError(error.message);
      break;
    }
    benchmark::DoNotOptimize(solution->cost);
  }
  state.counters["input_costs"] =
      static_cast<double>(center + 2 * domain + 2 * center * domain);
  state.SetItemsProcessed(state.iterations());
}
} // namespace

int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    benchmark::Shutdown();
    return 1;
  }
  auto *cases =
      benchmark::RegisterBenchmark("BranchAndBound/RootSolved", root_solved);
  cases->ArgNames({"domain", "singleton_center"});
  for (int domain : {64, 128, 256})
    for (int singleton : {0, 1})
      cases->Args({domain, singleton});
  cases->Unit(benchmark::kMillisecond)->UseRealTime();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
