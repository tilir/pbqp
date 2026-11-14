/*
 * Shared optimization profiles for both branch-and-bound solver names.
 * Profiles choose defaults; individual overrides are resolved before preparing
 * any bounds or workers. O0 keeps only the reference-term B&B relaxation.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include <array>
#include <cstddef>
module pbqp:bnb_options;

namespace pbqp {
enum class BnbOptimization : unsigned {
  OptionOrder = 1u << 0,
  ForwardChecking = 1u << 1,
  RegisterSymmetry = 1u << 2,
  GreedySeed = 1u << 3,
  ReductionSeed = 1u << 4,
  PrefixPlanning = 1u << 5,
  WorkDonation = 1u << 6,
  StructuralOrder = 1u << 7,
  CliqueBounds = 1u << 8,
  CardinalityBounds = 1u << 9,
  WeightedBounds = 1u << 10,
  Repricing = 1u << 11,
  CoverSeed = 1u << 12
};

enum class BnbProfile { O0, O1, O2 };

struct BnbOptimizationInfo {
  const char *key;
  BnbOptimization optimization;
  BnbProfile since = BnbProfile::O2;
};

constexpr std::array bnb_optimizations{
    BnbOptimizationInfo{"option-order", BnbOptimization::OptionOrder,
                        BnbProfile::O1},
    BnbOptimizationInfo{"forward-checking", BnbOptimization::ForwardChecking,
                        BnbProfile::O1},
    BnbOptimizationInfo{"register-symmetry", BnbOptimization::RegisterSymmetry,
                        BnbProfile::O1},
    BnbOptimizationInfo{"greedy-seed", BnbOptimization::GreedySeed,
                        BnbProfile::O1},
    BnbOptimizationInfo{"reduction-seed", BnbOptimization::ReductionSeed,
                        BnbProfile::O1},
    BnbOptimizationInfo{"prefix-planning", BnbOptimization::PrefixPlanning,
                        BnbProfile::O1},
    BnbOptimizationInfo{"work-donation", BnbOptimization::WorkDonation,
                        BnbProfile::O1},
    BnbOptimizationInfo{"structural-order", BnbOptimization::StructuralOrder},
    BnbOptimizationInfo{"clique-bounds", BnbOptimization::CliqueBounds},
    BnbOptimizationInfo{"cardinality-bounds",
                        BnbOptimization::CardinalityBounds},
    BnbOptimizationInfo{"weighted-bounds", BnbOptimization::WeightedBounds},
    BnbOptimizationInfo{"repricing", BnbOptimization::Repricing},
    BnbOptimizationInfo{"cover-seed", BnbOptimization::CoverSeed}};

constexpr unsigned bnb_profile_mask(BnbProfile profile) noexcept {
  unsigned mask = 0;
  for (const auto &entry : bnb_optimizations)
    if (profile >= entry.since)
      mask |= static_cast<unsigned>(entry.optimization);
  return mask;
}

class BnbOptions {
  unsigned bits;

public:
  static constexpr unsigned o1_mask = bnb_profile_mask(BnbProfile::O1);
  static constexpr unsigned all_mask = bnb_profile_mask(BnbProfile::O2);

  explicit BnbOptions(BnbProfile profile = BnbProfile::O1, unsigned enable = 0,
                      unsigned disable = 0) noexcept
      : bits((bnb_profile_mask(profile) | enable) & ~disable) {}

  bool requested(BnbOptimization option) const noexcept {
    return bits & static_cast<unsigned>(option);
  }

  bool enabled(BnbOptimization option) const noexcept {
    return requested(option) && (option != BnbOptimization::Repricing ||
                                 enabled(BnbOptimization::WeightedBounds));
  }

  bool structural() const noexcept {
    return bits & ((all_mask ^ o1_mask) ^
                   static_cast<unsigned>(BnbOptimization::Repricing));
  }

  bool catalogue() const noexcept {
    return enabled(BnbOptimization::CardinalityBounds) ||
           enabled(BnbOptimization::WeightedBounds) ||
           enabled(BnbOptimization::CoverSeed);
  }
};
} // namespace pbqp
