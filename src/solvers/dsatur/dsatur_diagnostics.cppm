/*
 * Preparation/result diagnostics for DSATUR and the shared B&B seed.
 * Reads cached model decisions and counters, never logging inside local search.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <string_view>
module pbqp:dsatur_diagnostics;
import :diagnostics;
import :dsatur;
import :interference;

namespace pbqp {
void describe_dsatur(const pbqp_problem &problem,
                     const InterferenceCheck &check, const DsaturSearch &search,
                     Diagnostics log, std::string_view prefix = "dsatur: ") {
  if (!log)
    return;
  if (check.failure != InterferenceFailure::None) {
    switch (check.failure) {
    case InterferenceFailure::Empty:
      log.line(prefix, "empty input; structural search unnecessary");
      break;
    case InterferenceFailure::Domains:
      log.line(prefix, "structural path unavailable: unequal domains at node ",
               check.node);
      break;
    case InterferenceFailure::RegisterCost:
      log.line(prefix,
               "structural path unavailable: nonzero register unary at node ",
               check.node);
      break;
    case InterferenceFailure::MatrixCost:
      log.line(
          prefix,
          "structural path unavailable: general/coalescing matrix at edge ",
          check.edge);
      break;
    default:
      log.line(prefix,
               "structural path unavailable: no verified register domain");
      break;
    }
    return;
  }
  if (search.status() == DsaturState::MemoryBudget) {
    log.line(prefix,
             "structural path unavailable: workspace exceeds memory budget ",
             DsaturSettings::memory_budget);
    return;
  }
  log.line(prefix, "structural path enabled; registers=",
           problem.nodes[0].num_options - 1,
           "; memory budget=", DsaturSettings::memory_budget,
           "; work budget=", DsaturSettings{}.work_budget,
           "; restart limit=", DsaturSettings{}.restarts);
}

void describe_dsatur_result(const DsaturSearch &search, Diagnostics log,
                            std::string_view prefix = "dsatur: ") {
  if (!log)
    return;
  log.line(prefix, "moves=", search.moves_used(),
           "; restarts=", search.restarts_used(),
           "; work units=", search.work_used());
  if (search.has_solution())
    log.line(prefix, "feasible candidate cost=", search.result().cost);
  else
    log.line(prefix, "no feasible candidate; infeasibility is not proven");
  if (search.status() == DsaturState::WorkBudget)
    log.line(prefix, "work budget reached; retained candidates remain valid");
  else if (search.status() == DsaturState::RestartBudget)
    log.line(prefix,
             "restart budget reached; retained candidates remain valid");
  else if (search.status() == DsaturState::Expired)
    log.line(prefix, "enclosing exact-search deadline reached");
  else if (search.status() == DsaturState::Colored)
    log.line(prefix, "full register coloring found");
}
} // namespace pbqp
