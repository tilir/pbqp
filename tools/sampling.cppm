/*
 * Versioned unbiased integer and reproducible binary32 sampling over the
 * standardized mt19937_64 engine. No implementation-defined distributions.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
module pbqp.generator:sampling;

namespace pbqp_tools {
// This protocol consumes the standardized mt19937_64 stream directly.
// Rejection removes modulo bias; the full-width inclusive range needs no
// modulo.
size_t uniform_index(std::mt19937_64 &random, size_t maximum) {
  static_assert(std::numeric_limits<size_t>::digits <= 64);
  const std::uint64_t span = static_cast<std::uint64_t>(maximum) + 1;
  if (!span)
    return static_cast<size_t>(random());
  const std::uint64_t rejected = (std::uint64_t{0} - span) % span;
  std::uint64_t value;
  do {
    value = random();
  } while (value < rejected);
  return static_cast<size_t>(value % span);
}

pbqp_cost_t uniform_cost(std::mt19937_64 &random, pbqp_cost_t minimum,
                         pbqp_cost_t maximum) {
  static_assert(std::numeric_limits<pbqp_cost_t>::is_iec559 &&
                std::numeric_limits<pbqp_cost_t>::digits == 24 &&
                std::numeric_limits<pbqp_cost_t>::max_exponent == 128);
  static_assert(std::numeric_limits<double>::is_iec559 &&
                std::numeric_limits<double>::digits == 53 &&
                std::numeric_limits<double>::max_exponent == 1024);
  constexpr unsigned fraction_bits = 24;
  const double fraction =
      static_cast<double>(random() >> (64 - fraction_bits)) * 0x1p-24;
  const double width =
      static_cast<double>(maximum) - static_cast<double>(minimum);
  // Explicit correctly rounded fma prevents contraction choices from changing
  // costs between compilers. The final binary32 conversion is intentional.
  return static_cast<pbqp_cost_t>(
      std::fma(width, fraction, static_cast<double>(minimum)));
}

} // namespace pbqp_tools
