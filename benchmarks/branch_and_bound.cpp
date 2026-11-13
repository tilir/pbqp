/*
 * Google Benchmark exact-solver and heartbeat comparisons on examples 06-08.
 *
 * Run from the source checkout root. Input parsing and preflight verification
 * are outside timing; each iteration includes exact solve and solution cleanup.
 * Google Benchmark itself uses one caller, and oneTBB manages solver workers.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <benchmark/benchmark.h>
#include <memory>

namespace {
using ProblemPtr =
    std::unique_ptr<pbqp_problem, decltype([](pbqp_problem *p) noexcept {
                      pbqp_problem_free(p);
                    })>;
using SolutionPtr =
    std::unique_ptr<pbqp_solution, decltype([](pbqp_solution *s) noexcept {
                      pbqp_solution_free(s);
                    })>;

void heartbeat(unsigned, void *) noexcept {}

using ExactSolver = decltype(&pbqp_solve_branch_and_bound_with_options);

void example(benchmark::State &state, const char *path, pbqp_cost_t optimum,
             ExactSolver solve) {
  pbqp_problem *raw_problem = nullptr;
  pbqp_error error{};
  auto status = pbqp_read(path, &raw_problem, &error);
  ProblemPtr problem(raw_problem);
  if (status != PBQP_OK) {
    state.SkipWithError(error.message);
    return;
  }
  const pbqp_solve_options options{static_cast<size_t>(state.range(0)),
                                   state.range(1) ? heartbeat : nullptr,
                                   nullptr};
  pbqp_solution *raw = nullptr;
  status = solve(problem.get(), &options, &raw, &error);
  SolutionPtr checked(raw);
  if (status != PBQP_OK) {
    state.SkipWithError(error.message);
    return;
  }
  pbqp_evaluation evaluation{};
  if (checked->quality != PBQP_SOLUTION_OPTIMAL || checked->cost != optimum ||
      pbqp_evaluate_assignment(problem.get(), checked->options,
                               checked->num_nodes, &evaluation,
                               &error) != PBQP_OK ||
      !evaluation.is_feasible || evaluation.cost != optimum) {
    state.SkipWithError("invalid example optimum");
    return;
  }
  checked.reset();
  for (auto iteration : state) {
    status = solve(problem.get(), &options, &raw, &error);
    SolutionPtr solution(raw);
    if (status != PBQP_OK) {
      state.SkipWithError(error.message);
      break;
    }
    benchmark::DoNotOptimize(solution->cost);
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["nodes"] = static_cast<double>(problem->num_nodes);
  state.counters["edges"] = static_cast<double>(problem->num_edges);
}
} // namespace

int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    benchmark::Shutdown();
    return 1;
  }
  benchmark::RegisterBenchmark(
      "BranchAndBound/Example06",
      [](benchmark::State &s) {
        example(s, "examples/06_exact_beats_heuristics.pbqp", 0,
                pbqp_solve_branch_and_bound_with_options);
      })
      ->ArgNames({"threads", "heartbeat"})
      ->Args({1, 0})
      ->Args({2, 0})
      ->Args({4, 0})
      ->Args({8, 0})
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();
  benchmark::RegisterBenchmark(
      "BranchAndBound/Example07",
      [](benchmark::State &s) {
        example(s, "examples/07_parallel_exact_beats_heuristics.pbqp",
                10.1112299F, pbqp_solve_branch_and_bound_with_options);
      })
      ->ArgNames({"threads", "heartbeat"})
      ->Args({1, 0})
      ->Args({1, 1})
      ->Args({4, 0})
      ->Args({4, 1})
      ->Args({16, 0})
      ->Args({16, 1})
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();
  benchmark::RegisterBenchmark(
      "BranchAndBoundOpt/Example06",
      [](benchmark::State &s) {
        example(s, "examples/06_exact_beats_heuristics.pbqp", 0,
                pbqp_solve_branch_and_bound_optimized_with_options);
      })
      ->ArgNames({"threads", "heartbeat"})
      ->Args({1, 0})
      ->Args({4, 0})
      ->Args({16, 0})
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();
  benchmark::RegisterBenchmark(
      "BranchAndBoundOpt/Example07",
      [](benchmark::State &s) {
        example(s, "examples/07_parallel_exact_beats_heuristics.pbqp",
                10.1112299F,
                pbqp_solve_branch_and_bound_optimized_with_options);
      })
      ->ArgNames({"threads", "heartbeat"})
      ->Args({1, 0})
      ->Args({1, 1})
      ->Args({4, 0})
      ->Args({16, 0})
      ->Args({16, 1})
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();
  benchmark::RegisterBenchmark(
      "BranchAndBoundOpt/Example08",
      [](benchmark::State &s) {
        example(s, "examples/08_too_long_for_simple_bnb.pbqp", 161.241684F,
                pbqp_solve_branch_and_bound_optimized_with_options);
      })
      ->ArgNames({"threads", "heartbeat"})
      ->Args({1, 0})
      ->Args({1, 1})
      ->Args({4, 0})
      ->Args({16, 0})
      ->Args({16, 1})
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
