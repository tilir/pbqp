/*
 * Google Benchmark exact-solver and heartbeat comparisons on examples 06-10.
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

void example(benchmark::State &state, const char *path, pbqp_cost_t optimum,
             size_t level) {
  pbqp_problem *raw_problem = nullptr;
  pbqp_error error{};
  auto status = pbqp_read(path, &raw_problem, &error);
  ProblemPtr problem(raw_problem);
  if (status != PBQP_OK) {
    state.SkipWithError(error.message);
    return;
  }
  pbqp_option options_entries[] = {
      pbqp_size_option("optimization-level", level),
      pbqp_size_option("num-threads", static_cast<size_t>(state.range(0))),
      pbqp_progress_option("progress", state.range(1) ? heartbeat : nullptr),
      pbqp_pointer_option("progress-data", nullptr),
      pbqp_diagnostic_option("diagnostics", nullptr),
      pbqp_pointer_option("diagnostic-data", nullptr)};
  const pbqp_solve_options options{6, options_entries};
  pbqp_solution *raw = nullptr;
  status = pbqp_solve_branch_and_bound(problem.get(), &options, &raw, &error);
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
    status = pbqp_solve_branch_and_bound(problem.get(), &options, &raw, &error);
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
        example(s, "examples/pbqp/06_exact_beats_heuristics.pbqp", 0,
                size_t{1});
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
        example(s, "examples/pbqp/07_parallel_exact_beats_heuristics.pbqp",
                10.1112299F, size_t{1});
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
        example(s, "examples/pbqp/06_exact_beats_heuristics.pbqp", 0,
                size_t{2});
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
        example(s, "examples/pbqp/07_parallel_exact_beats_heuristics.pbqp",
                10.1112299F, size_t{2});
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
        example(s, "examples/pbqp/08_too_long_for_simple_bnb.pbqp", 161.241684F,
                size_t{2});
      })
      ->ArgNames({"threads", "heartbeat"})
      ->Args({1, 0})
      ->Args({1, 1})
      ->Args({4, 0})
      ->Args({16, 0})
      ->Args({16, 1})
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();
  benchmark::RegisterBenchmark("BranchAndBoundOpt/Example09",
                               [](benchmark::State &s) {
                                 example(s,
                                         "examples/pbqp/09_more_bnbopts.pbqp",
                                         338.854034F, size_t{2});
                               })
      ->ArgNames({"threads", "heartbeat"})
      ->Args({1, 0})
      ->Args({4, 0})
      ->Args({16, 0})
      ->Args({16, 1})
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();
  benchmark::RegisterBenchmark("BranchAndBoundOpt/Example10",
                               [](benchmark::State &s) {
                                 example(s,
                                         "examples/pbqp/10_repricing_opts.pbqp",
                                         369.419556F, size_t{2});
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
