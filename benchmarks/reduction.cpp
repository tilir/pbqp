/*
 * Google Benchmark scalability cases for stars and disjoint K4 components.
 *
 * Times the public reduction API and returned-solution cleanup, including API
 * validation, preparation, and reconstruction. Graph construction and preflight
 * assignment checks remain outside the measured iteration loop.
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
enum class Shape { Star, K4 };
using SolutionPtr =
    std::unique_ptr<pbqp_solution,
                    decltype([](pbqp_solution *solution) noexcept {
                      pbqp_solution_free(solution);
                    })>;

bool check_result(benchmark::State &state, const pbqp_problem &problem,
                  pbqp_cost_t expected) {
  pbqp_error error{};
  pbqp_solution *raw = nullptr;
  auto status = pbqp_solve_reduction(&problem, &raw, &error);
  SolutionPtr solution(raw);
  if (status != PBQP_OK) {
    state.SkipWithError(error.message);
    return false;
  }
  pbqp_evaluation evaluation{};
  if (solution->quality != PBQP_SOLUTION_FEASIBLE ||
      solution->cost != expected ||
      pbqp_evaluate_assignment(&problem, solution->options, solution->num_nodes,
                               &evaluation, &error) != PBQP_OK ||
      !evaluation.is_feasible || evaluation.cost != solution->cost) {
    state.SkipWithError("invalid reduction result");
    return false;
  }
  return true;
}

void reduction(benchmark::State &state, Shape shape) {
  size_t count = static_cast<size_t>(state.range(0));
  const pbqp_cost_t unary[] = {1, 0};
  const pbqp_cost_t matrix[] = {0, 0, 0, PBQP_INF};
  std::vector<pbqp_node> nodes(count, pbqp_node{2, unary});
  std::vector<pbqp_edge> edges;
  if (shape == Shape::Star) {
    edges.reserve(count - 1);
    for (size_t id = 1; id < count; ++id)
      edges.push_back({0, id, matrix});
  } else {
    edges.reserve(count / 4 * 6);
    for (size_t base = 0; base < count; base += 4)
      for (size_t src = base; src < base + 4; ++src)
        for (size_t tgt = src + 1; tgt < base + 4; ++tgt)
          edges.push_back({src, tgt, matrix});
  }
  const pbqp_problem problem{nodes.size(), edges.size(), nodes.data(),
                             edges.data()};
  pbqp_cost_t expected =
      shape == Shape::Star ? 1 : static_cast<pbqp_cost_t>(3 * (count / 4));
  if (!check_result(state, problem, expected))
    return;

  for (auto iteration : state) {
    pbqp_solution *raw = nullptr;
    pbqp_error error{};
    auto status = pbqp_solve_reduction(&problem, &raw, &error);
    SolutionPtr solution(raw);
    if (status != PBQP_OK) {
      state.SkipWithError(error.message);
      break;
    }
    benchmark::DoNotOptimize(solution->cost);
  }
  state.SetComplexityN(state.range(0));
  state.SetItemsProcessed(state.iterations() * state.range(0));
  state.counters["nodes"] = static_cast<double>(nodes.size());
  state.counters["edges"] = static_cast<double>(edges.size());
}

void register_benchmarks() {
  benchmark::RegisterBenchmark("Reduction/Star", reduction, Shape::Star)
      ->ArgName("nodes")
      ->Arg(8000)
      ->Arg(16000)
      ->Arg(32000)
      ->Arg(64000)
      ->Unit(benchmark::kMillisecond)
      ->Complexity();
  benchmark::RegisterBenchmark("Reduction/K4", reduction, Shape::K4)
      ->ArgName("nodes")
      ->Arg(8000)
      ->Arg(16000)
      ->Arg(32000)
      ->Arg(64000)
      ->Unit(benchmark::kMillisecond)
      ->Complexity();
}
} // namespace

int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    benchmark::Shutdown();
    return 1;
  }
  register_benchmarks();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
