/*
 * Public C++ wrapper contracts: borrowed views, RAII, callbacks, and errors.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#include "pbqp.hpp"
#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void check(bool value,
           std::source_location location = std::source_location::current()) {
  if (!value)
    throw std::runtime_error("C++ API check failed at line " +
                             std::to_string(location.line()));
}

template <typename Operation> void invalid(Operation operation) {
  bool caught = false;
  try {
    operation();
  } catch (const pbqp::Error &error) {
    caught = true;
    check(error.status() == PBQP_INVALID && error.line() == 0);
    check(!std::string_view(error.what()).empty());
  }
  check(caught);
}

void test_borrowed_and_callbacks() {
  const std::array<pbqp_cost_t, 2> costs{1, 0};
  const pbqp_node node{costs.size(), costs.data()};
  const pbqp_problem problem{1, 0, &node, nullptr};
  pbqp::validate(problem);
  for (auto solver : {pbqp::Solver::Naive, pbqp::Solver::BranchAndBound,
                      pbqp::Solver::Greedy, pbqp::Solver::Reduction}) {
    const bool exact =
        solver != pbqp::Solver::Greedy && solver != pbqp::Solver::Reduction;
    unsigned percent = 0, messages = 0;
    pbqp::SolveOptions options;
    options.num_threads = 4;
    if (exact)
      options.progress = [&](unsigned value) { check(value == ++percent); };
    options.diagnostics = [&](std::string_view message) {
      check(!message.empty());
      ++messages;
    };
    const auto solution = pbqp::solve(problem, solver, options);
    const auto evaluation = pbqp::evaluate(problem, solution.options);
    check(solution.cost == 0 && evaluation.cost == 0 && evaluation.is_feasible);
    check(solution.options == std::vector<std::size_t>{1});
    check(solution.quality ==
          (exact ? pbqp::Quality::Optimal : pbqp::Quality::Feasible));
    check(messages > 0 && percent == (exact ? 100u : 0u));
  }
  for (std::size_t level : {0u, 1u, 2u}) {
    pbqp::SolveOptions tuned;
    tuned.parameters["optimization-level"] = level;
    tuned.parameters["weighted-bounds"] = true;
    tuned.parameters["application.setting"] = std::string("ignored");
    const auto result =
        pbqp::solve(problem, pbqp::Solver::BranchAndBound, tuned);
    check(result.cost == 0 && result.quality == pbqp::Quality::Optimal);
  }
  pbqp::SolveOptions invalid_profile;
  invalid_profile.parameters["optimization-level"] = std::size_t{3};
  invalid([&] {
    pbqp::solve(problem, pbqp::Solver::BranchAndBound, invalid_profile);
  });
  pbqp::SolveOptions warned;
  unsigned warning_count = 0;
  warned.parameters["unrecognized"] = true;
  warned.warnings = [&](std::string_view text) {
    warning_count +=
        text.starts_with("warning: unrecognized solver option: unrecognized");
  };
  check(pbqp::solve(problem, pbqp::Solver::BranchAndBound, warned).cost == 0);
  check(warning_count == 1);
  warned.parameters["warnings-as-errors"] = true;
  warning_count = 0;
  invalid([&] { pbqp::solve(problem, pbqp::Solver::BranchAndBound, warned); });
  check(warning_count == 0);
  const auto empty = pbqp::solve(pbqp_problem{});
  check(empty.cost == 0 && empty.options.empty());
  invalid([&] { pbqp::solve(problem, static_cast<pbqp::Solver>(7)); });
  invalid([&] { pbqp::evaluate(problem, std::span<const std::size_t>{}); });
  const std::array<std::size_t, 1> bad_option{2};
  invalid([&] { pbqp::evaluate(problem, bad_option); });
  pbqp::SolveOptions bad;
  bad.num_threads = 0;
  invalid([&] { pbqp::solve(problem, pbqp::Solver::Naive, bad); });
  bad.num_threads = 1;
  bad.progress = [](unsigned) {};
  bad.parameters["warnings-as-errors"] = true;
  invalid([&] { pbqp::solve(problem, pbqp::Solver::Greedy, bad); });

  enum class CallbackKind { Progress, Diagnostic, Warning };
  for (auto kind : {CallbackKind::Progress, CallbackKind::Diagnostic,
                    CallbackKind::Warning}) {
    unsigned calls = 0;
    pbqp::SolveOptions options;
    options.num_threads = 4;
    auto fail = [&] {
      ++calls;
      throw std::runtime_error("callback failure");
    };
    if (kind == CallbackKind::Diagnostic)
      options.diagnostics = [&](std::string_view) { fail(); };
    else if (kind == CallbackKind::Warning) {
      options.parameters["unrecognized"] = true;
      options.warnings = [&](std::string_view) { fail(); };
    } else
      options.progress = [&](unsigned) { fail(); };
    bool caught = false;
    try {
      pbqp::solve(problem, pbqp::Solver::BranchAndBound, options);
    } catch (const std::runtime_error &error) {
      caught = std::string_view(error.what()) == "callback failure";
    }
    check(caught && calls == 1);
  }
}

void test_parsed(const std::filesystem::path &input,
                 const std::filesystem::path &output) {
  pbqp::Solution solution;
  {
    auto original = pbqp::Problem::read(input);
    auto problem = std::move(original);
    bool caught = false;
    try {
      original.view();
    } catch (const std::logic_error &) {
      caught = true;
    }
    check(caught);
    problem.write(output);
    const auto copy = pbqp::Problem::read(output);
    solution = copy.solve(pbqp::Solver::Naive);
    check(problem.evaluate(solution.options).is_feasible);
    check(problem.view().num_nodes == solution.options.size());
  }
  check(solution.cost == 0 && !solution.options.empty());
  {
    std::ofstream invalid_file(output);
    invalid_file << "PBQP 1\nBROKEN\n";
  }
  bool caught = false;
  try {
    pbqp::Problem::read(output);
  } catch (const pbqp::Error &error) {
    caught = error.status() == PBQP_INVALID && error.line() == 2;
  }
  check(caught);
  std::filesystem::remove(output);
}
} // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 3);
    test_borrowed_and_callbacks();
    test_parsed(argv[1], argv[2]);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
