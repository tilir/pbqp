/*
 * PBQP solver command-line entry point.
 *
 * Selects a solver by name and loads a problem through the C API.
 * Reports diagnostics and distinguishes proven infeasibility from failures.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

import pbqp.command_line;

namespace {
void help() {
  std::cout
      << "Usage: pbqp --solve=SOLVER [--heartbeat] INPUT\n"
         "       pbqp --help\n\n"
         "  --heartbeat  Print one dot per completed percent to stderr (naive "
         "or bnb).\n\n"
         "Solvers:\n"
         "  naive   Exhaustive search; proves optimality or infeasibility.\n"
         "  bnb     Exact branch-and-bound; prunes bounded or forbidden "
         "subtrees.\n"
         "  r1r2rn  R1/R2/RN reduction and backpropagation heuristic.\n"
         "  greedy  Cost-priority heuristic; returns a feasible, possibly "
         "suboptimal solution.\n";
}
void heartbeat(unsigned percent, void *) noexcept {
  std::cerr.put('.');
  if (percent == 100)
    std::cerr.put('\n');
  std::cerr.flush();
}
struct SolverOptions {
  std::string_view solver{};
  std::string_view input{};
  bool report_progress = false;
  bool help_requested = false;
};

std::optional<SolverOptions> parse_options(int argc, char **argv) {
  try {
    constexpr std::string_view names[] = {"--solve"};
    constexpr std::string_view flags[] = {"--heartbeat"};
    const pbqp_tools::Arguments arguments(argc, argv, names, flags);
    if (arguments.help())
      return SolverOptions{.help_requested = true};
    auto solver = arguments.require("--solve");
    if (solver != "naive" && solver != "greedy" && solver != "bnb" &&
        solver != "r1r2rn")
      throw std::invalid_argument("unknown solver: " + std::string(solver));
    bool report_progress = arguments.has("--heartbeat");
    if (report_progress && solver != "naive" && solver != "bnb")
      throw std::invalid_argument(
          "--heartbeat is supported only by the naive and bnb solvers");
    if (arguments.positional().size() != 1)
      throw std::invalid_argument("expected exactly one input file");

    return SolverOptions{solver, arguments.positional().front(),
                         report_progress};
  } catch (const std::exception &error) {
    std::cerr << error.what()
              << "\nUse --help for usage and available solvers.\n";
    return std::nullopt;
  }
}
using ProblemPtr =
    std::unique_ptr<pbqp_problem, decltype([](pbqp_problem *p) noexcept {
                      pbqp_problem_free(p);
                    })>;
using SolutionPtr =
    std::unique_ptr<pbqp_solution, decltype([](pbqp_solution *s) noexcept {
                      pbqp_solution_free(s);
                    })>;

pbqp_status read_problem(std::string_view path, ProblemPtr &problem,
                         pbqp_error &error) {
  pbqp_problem *raw = nullptr;
  auto status = pbqp_read(path.data(), &raw, &error);
  problem.reset(raw);
  return status;
}

pbqp_status solve_problem(const SolverOptions &options,
                          const pbqp_problem &problem, SolutionPtr &solution,
                          pbqp_error &error) {
  pbqp_solution *raw = nullptr;
  pbqp_status status;
  if (options.solver == "greedy") {
    status = pbqp_solve_greedy(&problem, &raw, &error);
  } else if (options.solver == "r1r2rn") {
    status = pbqp_solve_reduction(&problem, &raw, &error);
  } else {
    auto solve = options.solver == "naive"
                     ? pbqp_solve_bruteforce_with_progress
                     : pbqp_solve_branch_and_bound_with_progress;
    status = solve(&problem, options.report_progress ? heartbeat : nullptr,
                   nullptr, &raw, &error);
  }
  solution.reset(raw);
  return status;
}

int report_error(pbqp_status status, const pbqp_error &error) {
  if (error.line)
    std::cerr << "line " << error.line << ": ";
  std::cerr << error.message << '\n';
  switch (status) {
  case PBQP_INFEASIBLE:
    return 2;
  case PBQP_NO_SOLUTION:
    return 3;
  default:
    return 1;
  }
}

int print_solution(const pbqp_solution &solution) {
  std::cout << std::setprecision(
      std::numeric_limits<pbqp_cost_t>::max_digits10);
  std::cout << "COST " << solution.cost << '\n';
  for (size_t k = 0; k < solution.num_nodes; ++k)
    std::cout << "NODE " << k << ' ' << solution.options[k] << '\n';
  std::cout.flush();
  if (!std::cout) {
    std::cerr << "output write failed\n";
    return 1;
  }
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  auto options = parse_options(argc, argv);
  if (!options)
    return 1;
  if (options->help_requested) {
    help();
    return 0;
  }

  pbqp_error error{};
  ProblemPtr problem;
  auto status = read_problem(options->input, problem, error);
  if (status != PBQP_OK)
    return report_error(status, error);

  SolutionPtr solution;
  status = solve_problem(*options, *problem, solution, error);
  if (status != PBQP_OK)
    return report_error(status, error);

  return print_solution(*solution);
}
