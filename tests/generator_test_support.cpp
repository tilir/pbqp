/*
 * Golden sampling protocol checks inside the generator's owning module.
 * Expected vectors were calculated independently from the MT19937-64 reference
 * recurrence, integer rejection rule, and binary32 rounding formula.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <bit>
#include <cstdint>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>
#include <string>
module pbqp.generator;
import :sampling;

namespace {
void check(bool condition,
           std::source_location line = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error("generator sampling check failed at line " +
                             std::to_string(line.line()));
}
} // namespace

namespace pbqp_tools {
extern "C++" void run_generator_sampling_tests() {
  struct IntegerCase {
    std::uint64_t maximum, result, next;
  };

  constexpr IntegerCase integers[] = {
      {0, 0, 18301848765998365067ULL},
      {1, 0, 18301848765998365067ULL},
      {2, 0, 18301848765998365067ULL},
      {9, 4, 18301848765998365067ULL},
      {80, 69, 18301848765998365067ULL},
      {2147483647, 1262607422, 18301848765998365067ULL},
      // This range rejects the first draw and consumes the second one.
      {9223372036854775808ULL, 9078476729143589258ULL, 729919693006235833ULL},
      {18446744073709551615ULL, 2947667278772165694ULL,
       18301848765998365067ULL}};
  for (const auto &item : integers) {
    if (item.maximum > std::numeric_limits<size_t>::max())
      continue;
    std::mt19937_64 random(0);
    check(uniform_index(random, static_cast<size_t>(item.maximum)) ==
          item.result);
    check(random() == item.next);
  }

  struct FloatCase {
    pbqp_cost_t minimum, maximum;
    std::uint32_t bits;
  };

  const auto tiny = std::numeric_limits<pbqp_cost_t>::denorm_min();
  const auto large = std::numeric_limits<pbqp_cost_t>::max();
  const FloatCase floats[] = {{10, 100, 0x41c30d1b},
                              {1, 1, 0x3f800000},
                              {tiny, tiny * 2, 1},
                              {large / 2, large, 0x7f14741b}};
  for (const auto &item : floats) {
    std::mt19937_64 random(0);
    auto value = uniform_cost(random, item.minimum, item.maximum);
    check(std::bit_cast<std::uint32_t>(value) == item.bits);
    check(random() == 18301848765998365067ULL);
  }
}
} // namespace pbqp_tools
