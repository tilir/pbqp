/*
 * Human-readable reports of optimized-bound eligibility and preparation limits.
 * Reads cached decisions from the algorithms; never reconstructs their state
 * or changes optimization choices. All reporting stays outside DFS.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <cstddef>
module pbqp:bnbopt_diagnostics;
import :diagnostics;
import :bitset;
import :bnb_bounds;
import :bnbopt_structure;
import :independent_sets;
import :bnbopt_pricing;
namespace pbqp {
void describe_bnbopt_input(const pbqp_problem &p,
                           const InterferenceCheck &check,
                           const RegallocStructure &graph, Diagnostics log) {
  if (!log)
    return;
  log.line("bnbopt: dynamic bitsets; word bits=", BitSet<>::word_bits,
           "; no fixed 64-node or 64-register limit");
  log.line(
      "bnbopt: graph limits: storage bytes=", RegallocStructure::memory_budget,
      "; ranking words=", RegallocStructure::word_budget,
      "; greedy starts=", RegallocStructure::greedy_starts,
      "; clique extensions=", RegallocStructure::clique_budget,
      "; retained cliques=", RegallocStructure::max_cliques);
  log.line("bnbopt: independent-set limits: storage bytes=",
           IndependentSets::memory_budget,
           "; sets=", IndependentSets::pattern_limit,
           "; word work=", IndependentSets::word_budget);
  log.line("bnbopt: pricing limits: iterations=",
           RegallocPricing::maximum_iterations,
           "; pattern-vertex visits=", RegallocPricing::visit_budget,
           "; total auxiliary weight=", RegallocPricing::state_budget,
           "; DP state slots=", RegallocPricing::dp_work_budget);
  switch (check.failure) {
  case InterferenceFailure::None:
    log.line("bnbopt: input shape=pure interference; registers=",
             p.nodes[0].num_options - 1);
    if (graph.eligible())
      log.line("bnbopt: structural path available");
    else
      log.line("bnbopt: structural path unavailable: graph storage budget "
               "exceeded; using generic PBQP bounds");
    break;
  case InterferenceFailure::Empty:
    log.line("bnbopt: empty problem; structural optimizations unnecessary");
    break;
  case InterferenceFailure::NoRegisters:
    log.line("bnbopt: structural path unavailable: domain has no register "
             "after spill option 0; using generic PBQP bounds");
    break;
  case InterferenceFailure::Domains:
    log.line("bnbopt: structural path unavailable: node ", check.node, " has ",
             p.nodes[check.node].num_options, " options, expected ",
             p.nodes[0].num_options, "; using generic PBQP bounds");
    break;
  case InterferenceFailure::RegisterCost:
    log.line("bnbopt: structural path unavailable: node ", check.node,
             " option ", check.option, " has register unary cost ",
             check.actual, ", expected 0; using generic PBQP bounds");
    break;
  case InterferenceFailure::MatrixCost: {
    const auto &edge = p.edges[check.edge];
    log.line(
        "bnbopt: structural path unavailable: edge (", edge.src, ", ", edge.tgt,
        ") matrix entry [", check.row, ",", check.col, "]=", check.actual,
        ", expected ", check.expected == PBQP_INF ? "INF" : "0",
        "; coalescing or general pairwise costs require generic PBQP bounds");
    break;
  }
  }
}

void describe_bnbopt_structure(const RegallocStructure &graph,
                               Diagnostics log) {
  if (!log)
    return;
  if (!graph.ready()) {
    log.line(
        "bnbopt: structural preparation skipped; generic PBQP bounds retained");
    return;
  }
  log.line("bnbopt: structural path enabled: forward checking, clique bounds, "
           "clique-first order");
  log.line("bnbopt: clique preparation: greedy starts=",
           graph.greedy_starts_used(), "; extensions=", graph.clique_steps(),
           "; retained=", graph.cliques().size(),
           "; omitted by witness limit=", graph.omitted_cliques());
  if (graph.clique_budget_hit())
    log.line("bnbopt: clique extension budget reached; valid witnesses "
             "retained, maximum clique not required");
  if (graph.word_budget_hit())
    log.line(
        "bnbopt: clique ranking word budget reached; valid witnesses retained");
  log.line("bnbopt: tail ordering=",
           graph.uses_adaptive_tail()
               ? "adaptive prefix connectivity"
               : "static degree order (comparison budget)");
}

void describe_bnbopt_capacity(const pbqp_problem &problem,
                              const IndependentSets &sets,
                              const RegallocPricing &pricing, Diagnostics log) {
  if (!log)
    return;
  switch (sets.status()) {
  case IndependentSetStatus::Complete:
    log.line("bnbopt: independent sets complete: count=", sets.size(),
             "; maximum size=", sets.maximum_size(),
             "; global cardinality capacity enabled");
    break;
  case IndependentSetStatus::MemoryBudget:
    log.line("bnbopt: independent sets disabled: storage budget reached; "
             "partial results discarded");
    break;
  case IndependentSetStatus::SetBudget:
    log.line(
        "bnbopt: independent sets disabled: set count budget reached after ",
        sets.num_examined(), " sets; partial results discarded");
    break;
  case IndependentSetStatus::WordBudget:
    log.line(
        "bnbopt: independent sets disabled: word-work budget reached after ",
        sets.num_examined(), " sets; partial results discarded");
    break;
  case IndependentSetStatus::NotPrepared:
    log.line("bnbopt: independent-set preparation not attempted");
    break;
  }
  if (pricing.iterations_used())
    log.line("bnbopt: pricing iterations=", pricing.iterations_used(), "/",
             RegallocPricing::maximum_iterations);
  switch (pricing.status()) {
  case PricingStatus::Enabled:
    log.line("bnbopt: weighted capacity enabled: total auxiliary weight=",
             pricing.total_weight(),
             "; effective weight limit=", pricing.effective_weight_limit(),
             "; quantization scale=", pricing.scale());
    if (pricing.scale() < 4)
      log.line("bnbopt: auxiliary weights scaled down to fit state/DP budgets");
    break;
  case PricingStatus::IncompleteSets:
    log.line("bnbopt: weighted capacity disabled: independent-set enumeration "
             "is incomplete; clique bounds retained");
    break;
  case PricingStatus::VisitBudget:
    log.line("bnbopt: weighted pricing skipped: pattern-vertex visit budget "
             "exceeded; entries=",
             sets.num_entries(), "; passes=", RegallocPricing::vertex_passes,
             "; unweighted bounds retained");
    break;
  case PricingStatus::UnsupportedCost: {
    auto node = pricing.unsupported_cost_node();
    log.line(
        "bnbopt: weighted pricing skipped: node ", node,
        " spill cost=", problem.nodes[node].costs[0],
        "; requires finite nonnegative spills; unweighted bounds retained");
    break;
  }
  case PricingStatus::NonPositive:
    log.line("bnbopt: weighted pricing skipped: nonpositive heuristic score; "
             "unweighted bounds retained, no infeasibility claim");
    break;
  case PricingStatus::NonFinite:
    log.line("bnbopt: weighted pricing skipped: nonfinite heuristic "
             "arithmetic; unweighted bounds retained");
    break;
  case PricingStatus::StateBudget:
    log.line("bnbopt: weighted pricing skipped: auxiliary state/conversion "
             "budget; unweighted bounds retained");
    break;
  case PricingStatus::ZeroWeights:
    log.line("bnbopt: weighted pricing skipped: auxiliary weights are zero; "
             "unweighted bounds retained");
    break;
  case PricingStatus::Empty:
    log.line("bnbopt: weighted pricing unnecessary: empty domain of patterns");
    break;
  case PricingStatus::NotPrepared:
    log.line("bnbopt: weighted pricing not attempted");
    break;
  }
}
} // namespace pbqp
