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
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import pbqp.command_line;

namespace {
void help() {
  std::cout
      << "Usage: pbqp --solve=SOLVER [--heartbeat] INPUT\n"
         "       pbqp --help\n\n"
         "  --heartbeat  Print one dot per completed percent to stderr (naive "
         "only).\n\n"
         "Solvers:\n"
         "  naive   Exhaustive search; proves optimality or infeasibility.\n"
         "  greedy  Cost-priority heuristic; returns a feasible, possibly "
         "suboptimal solution.\n";
}
void heartbeat(unsigned percent, void *) noexcept {
  std::cerr.put('.');
  if (percent == 100)
    std::cerr.put('\n');
  std::cerr.flush();
}
} // namespace

int main(int argc, char **argv) {
  try {
    constexpr std::string_view names[] = {"--solve"};
    constexpr std::string_view flags[] = {"--heartbeat"};
    const pbqp_tools::Arguments arguments(argc, argv, names, flags);
    if (arguments.help()) {
      help();
      return 0;
    }
    auto solver = arguments.require("--solve");
    if (solver != "naive" && solver != "greedy")
      throw std::invalid_argument("unknown solver: " + std::string(solver));
    bool report_progress = arguments.has("--heartbeat");
    if (report_progress && solver != "naive")
      throw std::invalid_argument(
          "--heartbeat is supported only by the naive solver");
    if (arguments.positional().size() != 1)
      throw std::invalid_argument("expected exactly one input file");

    pbqp_problem *raw = nullptr;
    pbqp_error error{};
    auto status =
        pbqp_read(arguments.positional().front().data(), &raw, &error);
    const auto free_problem = [](pbqp_problem *p) noexcept {
      pbqp_problem_free(p);
    };
    std::unique_ptr<pbqp_problem, decltype(free_problem)> problem(raw,
                                                                  free_problem);
    if (status == PBQP_OK) {
      pbqp_solution *result = nullptr;
      auto solve =
          solver == "naive" ? pbqp_solve_bruteforce : pbqp_solve_greedy;
      status = report_progress
                   ? pbqp_solve_bruteforce_with_progress(
                         problem.get(), heartbeat, nullptr, &result, &error)
                   : solve(problem.get(), &result, &error);
      const auto free_solution = [](pbqp_solution *s) noexcept {
        pbqp_solution_free(s);
      };
      std::unique_ptr<pbqp_solution, decltype(free_solution)> solution(
          result, free_solution);
      if (status == PBQP_OK) {
        std::cout << std::setprecision(
            std::numeric_limits<pbqp_cost_t>::max_digits10);
        std::cout << "COST " << solution->cost << '\n';
        for (size_t k = 0; k < solution->num_nodes; ++k)
          std::cout << "NODE " << k << ' ' << solution->options[k] << '\n';
        std::cout.flush();
        if (!std::cout) {
          std::cerr << "output write failed\n";
          return 1;
        }
      }
    }
    if (status != PBQP_OK) {
      if (error.line)
        std::cerr << "line " << error.line << ": ";
      std::cerr << error.message << '\n';
      return status == PBQP_INFEASIBLE ? 2 : status == PBQP_NO_SOLUTION ? 3 : 1;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what()
              << "\nUse --help for usage and available solvers.\n";
    return 1;
  }
  return 0;
}
