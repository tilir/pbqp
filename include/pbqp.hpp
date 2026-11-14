/*
 * C++23 conveniences over C API 2.0: RAII problems, vector solutions, spans,
 * typed solver selection, callbacks, and exceptions preserving C diagnostics.
 * This header does not expose implementation modules or solver internals.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#pragma once
/** @file pbqp.hpp
 * @brief Public C++23 wrapper over C API 2.0.
 */
#include "pbqp.h"
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/** Public C++ conveniences, independent of private implementation modules. */
namespace pbqp {
/** Native floating-point cost type. */
using Cost = pbqp_cost_t;

/** Typed counterpart of pbqp_solver. */
enum class Solver {
  Naive = PBQP_SOLVER_NAIVE,        ///< Exhaustive reference search.
  BranchAndBound = PBQP_SOLVER_BNB, ///< Exact B&B.
  Greedy = PBQP_SOLVER_GREEDY,      ///< Greedy heuristic.
  Reduction = PBQP_SOLVER_R1R2RN    ///< R1/R2/RN heuristic.
};

/** Feasibility or optimality guarantee, matching pbqp_solution_quality. */
enum class Quality {
  Feasible = PBQP_SOLUTION_FEASIBLE, ///< Feasible without an optimality proof.
  Optimal = PBQP_SOLUTION_OPTIMAL    ///< Proven reference-order optimum.
};

/** C failure translated into an exception with status and source line. */
class Error : public std::runtime_error {
  pbqp_status code;
  std::size_t source_line;

public:
  /** Constructs an exception preserving the native diagnostic. */
  Error(pbqp_status status, const pbqp_error &error)
      : std::runtime_error(error.line ? "line " + std::to_string(error.line) +
                                            ": " + error.message
                                      : std::string(error.message)),
        code(status), source_line(error.line) {}

  /** Native failure status. */
  pbqp_status status() const noexcept { return code; }

  /** Input source line, or zero when not known. */
  std::size_t line() const noexcept { return source_line; }
};

/** Scalar values accepted by the open solver parameter map. */
using OptionValue = std::variant<bool, std::size_t, std::string>;

/** C++ callbacks are captured and safely bridged by solve(). */
struct SolveOptions {
  std::size_t num_threads = 1; ///< Positive concurrency limit.
  std::function<void(unsigned)>
      progress; ///< Optional progress (approximate for B&B).
  std::function<void(std::string_view)>
      diagnostics; ///< Borrowed preparation text.
  std::function<void(std::string_view)>
      warnings; ///< Optional warnings; falls back to diagnostics when empty.
  std::map<std::string, OptionValue>
      parameters; ///< Additional key-value options.
};

/** Independent vector-backed assignment returned by solve(). */
struct Solution {
  Cost cost = 0;                       ///< Reference-order float objective.
  Quality quality = Quality::Feasible; ///< Assignment guarantee.
  std::vector<std::size_t> options;    ///< Owned choices indexed by node ID.
};

/** Reference-order score and explicit-INF feasibility. */
struct Evaluation {
  Cost cost = 0;            ///< Reference-order float objective.
  bool is_feasible = false; ///< False for an explicit forbidden term.
};

/** Validates a borrowed view, throwing Error on failure. */
inline void validate(const pbqp_problem &problem) {
  pbqp_error error;
  if (auto status = pbqp_validate(&problem, &error); status != PBQP_OK)
    throw Error(status, error);
}

/** Validates and serializes a borrowed view, throwing Error on failure. */
inline void write(const std::filesystem::path &path,
                  const pbqp_problem &problem) {
  pbqp_error error;
  if (auto status = pbqp_write(path.string().c_str(), &problem, &error);
      status != PBQP_OK)
    throw Error(status, error);
}

/** Evaluates a borrowed assignment span; forbidden tuples are not errors. */
inline Evaluation evaluate(const pbqp_problem &problem,
                           std::span<const std::size_t> options) {
  pbqp_evaluation result{};
  pbqp_error error;
  if (auto status = pbqp_evaluate_assignment(&problem, options.data(),
                                             options.size(), &result, &error);
      status != PBQP_OK)
    throw Error(status, error);
  return {result.cost, result.is_feasible != 0};
}

/** Borrows a C problem view. Copies native results into a vector before freeing
 * the C solution. Callback exceptions are captured at the C boundary, suppress
 * further callbacks, and are rethrown after the synchronous solver returns;
 * they do not cancel search. Diagnostic string_views are borrowed for a call.
 * One call's callbacks are serialized by the underlying C API. */
inline Solution solve(const pbqp_problem &problem,
                      Solver solver = Solver::BranchAndBound,
                      const SolveOptions &options = {}) {
  struct Callbacks {
    const SolveOptions &options;
    std::exception_ptr failure;

    static void progress(unsigned percent, void *data) noexcept {
      auto &self = *static_cast<Callbacks *>(data);
      if (self.failure)
        return;
      try {
        self.options.progress(percent);
      } catch (...) {
        self.failure = std::current_exception();
      }
    }

    static void diagnostics(const char *message, void *data) noexcept {
      auto &self = *static_cast<Callbacks *>(data);
      if (self.failure)
        return;
      try {
        self.options.diagnostics(message);
      } catch (...) {
        self.failure = std::current_exception();
      }
    }

    static void warnings(const char *message, void *data) noexcept {
      auto &self = *static_cast<Callbacks *>(data);
      if (self.failure)
        return;
      try {
        self.options.warnings(message);
      } catch (...) {
        self.failure = std::current_exception();
      }
    }
  } callbacks{options, {}};

  std::vector<pbqp_option> entries{
      pbqp_size_option("num-threads", options.num_threads),
      pbqp_progress_option("progress",
                           options.progress ? Callbacks::progress : nullptr),
      pbqp_pointer_option("progress-data", &callbacks),
      pbqp_diagnostic_option("diagnostics", options.diagnostics
                                                ? Callbacks::diagnostics
                                                : nullptr),
      pbqp_pointer_option("diagnostic-data", &callbacks),
      pbqp_diagnostic_option("warnings",
                             options.warnings ? Callbacks::warnings : nullptr),
      pbqp_pointer_option("warning-data", &callbacks)};
  for (const auto &[key, value] : options.parameters) {
    if (auto item = std::get_if<bool>(&value))
      entries.push_back(pbqp_bool_option(key.c_str(), *item));
    else if (auto item = std::get_if<std::size_t>(&value))
      entries.push_back(pbqp_size_option(key.c_str(), *item));
    else
      entries.push_back(pbqp_string_option(
          key.c_str(), std::get<std::string>(value).c_str()));
  }
  const pbqp_solve_options config{entries.size(), entries.data()};
  pbqp_solution *raw = nullptr;
  pbqp_error error;
  const auto status = pbqp_solve(static_cast<pbqp_solver>(solver), &problem,
                                 &config, &raw, &error);
  const std::unique_ptr<pbqp_solution, decltype([](pbqp_solution *s) noexcept {
                          pbqp_solution_free(s);
                        })>
      result(raw);
  if (callbacks.failure)
    std::rethrow_exception(callbacks.failure);
  if (status != PBQP_OK)
    throw Error(status, error);
  Solution solution{result->cost, static_cast<Quality>(result->quality), {}};
  if (result->num_nodes)
    solution.options.assign(result->options,
                            result->options + result->num_nodes);
  return solution;
}

/** Move-only owner of a parsed problem. view() borrows the original read-only
 * arrays. A moved-from owner may be destroyed or assigned, but accessing its
 * view throws std::logic_error. Solutions remain valid after destruction. */
class Problem {
  std::unique_ptr<pbqp_problem, decltype([](pbqp_problem *p) noexcept {
                    pbqp_problem_free(p);
                  })>
      storage;

  explicit Problem(pbqp_problem *p) noexcept : storage(p) {}

public:
  /** Ownership cannot be copied. */
  Problem(const Problem &) = delete;
  /** Ownership cannot be copied. */
  Problem &operator=(const Problem &) = delete;
  /** Transfers ownership; the source becomes moved from. */
  Problem(Problem &&) noexcept = default;
  /** Releases current storage and transfers ownership from the source. */
  Problem &operator=(Problem &&) noexcept = default;

  /** Parses an owned problem, throwing Error on failure. */
  static Problem read(const std::filesystem::path &path) {
    pbqp_problem *raw = nullptr;
    pbqp_error error;
    if (auto status = pbqp_read(path.string().c_str(), &raw, &error);
        status != PBQP_OK)
      throw Error(status, error);
    return Problem(raw);
  }

  /** Borrows the read-only native view; throws for a moved-from object. */
  const pbqp_problem &view() const {
    if (!storage)
      throw std::logic_error("PBQP problem has been moved from");
    return *storage;
  }

  /** Serializes this owned problem. */
  void write(const std::filesystem::path &path) const {
    pbqp::write(path, view());
  }

  /** Evaluates a complete assignment span against this problem. */
  Evaluation evaluate(std::span<const std::size_t> options) const {
    return pbqp::evaluate(view(), options);
  }

  /** Solves synchronously and returns an independent vector assignment. */
  Solution solve(Solver solver = Solver::BranchAndBound,
                 const SolveOptions &options = {}) const {
    return pbqp::solve(view(), solver, options);
  }
};
} // namespace pbqp
