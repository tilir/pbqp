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
#include <vector>
module pbqp:bnb_diagnostics;
import :diagnostics;
import :bnb_term_bounds;
import :bnb_options;
import :bnb_structure;
import :independent_sets;
import :bnb_pricing;
import :bnb_prefix_pricing;

namespace pbqp {
void describe_bnb_input(const pbqp_problem &p, const InterferenceCheck &check,
                        const RegallocStructure &graph, Diagnostics log) {
  if (!log)
    return;
  log.line(
      "bnb: graph limits: storage bytes=", RegallocStructure::memory_budget,
      "; ranking words=", RegallocStructure::word_budget,
      "; greedy starts=", RegallocStructure::greedy_starts,
      "; clique extensions=", RegallocStructure::clique_budget,
      "; retained cliques=", RegallocStructure::max_cliques);
  log.line("bnb: independent-set limits: storage bytes=",
           IndependentSets::memory_budget,
           "; sets=", IndependentSets::pattern_limit,
           "; word work=", IndependentSets::word_budget);
  log.line(
      "bnb: pricing limits: iterations=", RegallocPricing::maximum_iterations,
      "; pattern-vertex visits=", RegallocPricing::visit_budget,
      "; total auxiliary weight=", RegallocPricing::state_budget,
      "; DP state slots=", RegallocPricing::dp_work_budget);
  switch (check.failure) {
  case InterferenceFailure::None:
    log.line("bnb: input shape=pure interference; registers=",
             p.nodes[0].num_options - 1);
    if (!graph.eligible())
      log.line("bnb: structural path unavailable: graph storage budget "
               "exceeded; using generic PBQP bounds");
    break;
  case InterferenceFailure::Disabled:
    log.line("bnb: structural verification disabled by optimization settings");
    break;
  case InterferenceFailure::Empty:
    log.line("bnb: empty problem; structural optimizations unnecessary");
    break;
  case InterferenceFailure::NoRegisters:
    log.line("bnb: structural path unavailable: domain has no register "
             "after spill option 0; using generic PBQP bounds");
    break;
  case InterferenceFailure::Domains:
    log.line("bnb: structural path unavailable: node ", check.node, " has ",
             p.nodes[check.node].num_options, " options, expected ",
             p.nodes[0].num_options, "; using generic PBQP bounds");
    break;
  case InterferenceFailure::RegisterCost:
    log.line("bnb: structural path unavailable: node ", check.node, " option ",
             check.option, " has register unary cost ", check.actual,
             ", expected 0; using generic PBQP bounds");
    break;
  case InterferenceFailure::MatrixCost: {
    const auto &edge = p.edges[check.edge];
    log.line(
        "bnb: structural path unavailable: edge (", edge.src, ", ", edge.tgt,
        ") matrix entry [", check.row, ",", check.col, "]=", check.actual,
        ", expected ", check.expected == PBQP_INF ? "INF" : "0",
        "; coalescing or general pairwise costs require generic PBQP bounds");
    break;
  }
  }
}

void describe_bnb_structure(const RegallocStructure &graph, BnbOptions options,
                            Diagnostics log) {
  if (!log || !graph.ready())
    return;
  log.line("bnb: structural path enabled");
  log.line("bnb: structural forward checking=",
           options.enabled(BnbOptimization::ForwardChecking) ? "enabled"
                                                             : "disabled",
           "; clique bounds=",
           options.enabled(BnbOptimization::CliqueBounds) ? "enabled"
                                                          : "disabled",
           "; order=",
           options.enabled(BnbOptimization::StructuralOrder) ? "clique-first"
                                                             : "node ID");
  if (!graph.cliques().empty())
    log.line("bnb: clique preparation: greedy starts=",
             graph.greedy_starts_used(), "; extensions=", graph.clique_steps(),
             "; retained=", graph.cliques().size(),
             "; omitted by witness limit=", graph.omitted_cliques());
  if (graph.clique_budget_hit())
    log.line("bnb: clique extension budget reached; valid witnesses "
             "retained, maximum clique not required");
  if (graph.word_budget_hit())
    log.line(
        "bnb: clique ranking word budget reached; valid witnesses retained");
  if (options.enabled(BnbOptimization::StructuralOrder))
    log.line("bnb: tail ordering=",
             graph.uses_adaptive_tail()
                 ? "adaptive prefix connectivity"
                 : "static degree order (comparison budget)");
}

void describe_bnb_prefix_pricing(const PrefixPricingPlan &plan,
                                 Diagnostics log) {
  if (!log)
    return;
  switch (plan.status) {
  case PrefixPricingStatus::Enabled:
    log.line("bnb: prefix pricing enabled: iterations per temperature=",
             PrefixPriceOptimizer::iterations,
             "; iteration work units per refresh=",
             PrefixPriceOptimizer::work_budget,
             "; candidate slots=", plan.candidates, "/",
             PrefixPriceOptimizer::candidate_budget,
             "; auxiliary state limit=", plan.state_limit,
             "; depth refresh interval=", PrefixPriceOptimizer::refresh_depth,
             "; finite incumbent required");
    break;
  case PrefixPricingStatus::CandidateBudget:
    log.line("bnb: prefix pricing disabled: register-pattern candidate "
             "budget exceeded; root weights retained");
    break;
  case PrefixPricingStatus::RootUnavailable:
    log.line(
        "bnb: prefix pricing disabled: root auxiliary weights unavailable");
    break;
  }
}

void describe_bnb_capacity(const pbqp_problem &problem,
                           const IndependentSets &sets,
                           const RegallocPricing &pricing, BnbOptions options,
                           Diagnostics log) {
  if (!log)
    return;
  switch (sets.status()) {
  case IndependentSetStatus::Complete:
    log.line("bnb: independent sets complete: count=", sets.size(),
             "; maximum size=", sets.maximum_size(),
             "; word work=", sets.work_used(), "; global cardinality capacity ",
             options.enabled(BnbOptimization::CardinalityBounds) ? "enabled"
                                                                 : "disabled");
    break;
  case IndependentSetStatus::MemoryBudget:
    log.line("bnb: independent sets disabled: storage budget reached; "
             "partial results discarded");
    break;
  case IndependentSetStatus::SetBudget:
    log.line("bnb: independent sets disabled: set count budget reached after ",
             sets.num_examined(), " sets; partial results discarded");
    break;
  case IndependentSetStatus::WordBudget:
    log.line("bnb: independent sets disabled: word-work budget reached after ",
             sets.num_examined(), " sets; partial results discarded");
    break;
  case IndependentSetStatus::NotPrepared:
    log.line("bnb: independent-set preparation not attempted");
    break;
  }
  if (!options.enabled(BnbOptimization::WeightedBounds)) {
    log.line("bnb: weighted capacity disabled by optimization settings");
    return;
  }
  if (pricing.iterations_used())
    log.line("bnb: pricing iterations=", pricing.iterations_used(), "/",
             RegallocPricing::maximum_iterations);
  switch (pricing.status()) {
  case PricingStatus::Enabled:
    log.line("bnb: weighted capacity enabled: total auxiliary weight=",
             pricing.total_weight(),
             "; effective weight limit=", pricing.effective_weight_limit(),
             "; quantization scale=", pricing.scale());
    if (pricing.scale() < 4)
      log.line("bnb: auxiliary weights scaled down to fit state/DP budgets");
    break;
  case PricingStatus::IncompleteSets:
    log.line("bnb: weighted capacity disabled: independent-set enumeration "
             "is incomplete; other enabled bounds retained");
    break;
  case PricingStatus::VisitBudget:
    log.line("bnb: weighted pricing skipped: pattern-vertex visit budget "
             "exceeded; entries=",
             sets.num_entries(), "; passes=", RegallocPricing::vertex_passes,
             "; other enabled bounds retained");
    break;
  case PricingStatus::UnsupportedCost: {
    auto node = pricing.unsupported_cost_node();
    log.line(
        "bnb: weighted pricing skipped: node ", node,
        " spill cost=", problem.nodes[node].costs[0],
        "; requires finite nonnegative spills; other enabled bounds retained");
    break;
  }
  case PricingStatus::NonPositive:
    log.line("bnb: weighted pricing skipped: nonpositive heuristic score; "
             "other enabled bounds retained, no infeasibility claim");
    break;
  case PricingStatus::NonFinite:
    log.line("bnb: weighted pricing skipped: nonfinite heuristic "
             "arithmetic; other enabled bounds retained");
    break;
  case PricingStatus::StateBudget:
    log.line("bnb: weighted pricing skipped: auxiliary state/conversion "
             "budget; other enabled bounds retained");
    break;
  case PricingStatus::ZeroWeights:
    log.line("bnb: weighted pricing skipped: auxiliary weights are zero; "
             "other enabled bounds retained");
    break;
  case PricingStatus::Empty:
    log.line("bnb: weighted pricing unnecessary: empty domain of patterns");
    break;
  case PricingStatus::NotPrepared:
    log.line("bnb: weighted pricing not attempted");
    break;
  }
}
} // namespace pbqp
