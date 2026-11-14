/*
 * Use the public C++ API with RAII, typed solver options, and callbacks.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#include "pbqp.hpp"
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: pbqp_example_cpp INPUT\n";
    return 1;
  }
  try {
    const auto solution = [&] {
      const auto problem = pbqp::Problem::read(argv[1]);
      pbqp::SolveOptions options;
      options.num_threads = 4;
      options.parameters["optimization-level"] = std::size_t{2};
      options.parameters["repricing"] = true;
      options.diagnostics = [](std::string_view message) {
        std::cerr << message << '\n';
      };
      return problem.solve(pbqp::Solver::BranchAndBound, options);
    }();
    // The solution's vector remains valid after the problem is destroyed.
    std::cout.precision(std::numeric_limits<pbqp::Cost>::max_digits10);
    std::cout << "COST " << solution.cost << '\n';
    for (std::size_t node = 0; node < solution.options.size(); ++node)
      std::cout << "NODE " << node << ' ' << solution.options[node] << '\n';
  } catch (const pbqp::Error &error) {
    std::cerr << "PBQP status " << error.status() << ": " << error.what()
              << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
