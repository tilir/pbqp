/*
 * Percentage boundaries for mixed-radix search-space coverage.
 *
 * Computes exact tuple indices without forming the product of domain sizes.
 * A caller-provided boundary buffer keeps progress allocation out of search.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp:progress;

namespace pbqp {
void percentage_boundary(const pbqp_problem &problem, unsigned percent,
                         size_t *boundary) noexcept {
  if (percent == 100) {
    for (size_t k = 0; k < problem.num_nodes; ++k)
      boundary[k] = problem.nodes[k].num_options - 1;
    return;
  }
  // Expand percent / 100 into domain-radix digits of floor(total * p / 100).
  // Splitting each radix by 100 avoids overflowing remainder * radix.
  size_t remainder = percent;
  for (size_t k = 0; k < problem.num_nodes; ++k) {
    size_t radix = problem.nodes[k].num_options;
    size_t small = remainder * (radix % 100);
    boundary[k] = remainder * (radix / 100) + small / 100;
    remainder = small % 100;
  }
  // The zero-based tuple index is ceil(total * p / 100) - 1.
  // An exact division needs one mixed-radix decrement.
  if (remainder == 0) {
    for (size_t k = problem.num_nodes; k > 0; --k) {
      if (boundary[k - 1] != 0) {
        --boundary[k - 1];
        break;
      }
      boundary[k - 1] = problem.nodes[k - 1].num_options - 1;
    }
  }
}

class PercentProgress {
  const pbqp_problem &problem;
  pbqp_progress_callback callback;
  void *user_data;
  size_t *boundary;
  unsigned next_percent = 1;

  void prepare_boundary() noexcept {
    percentage_boundary(problem, next_percent, boundary);
  }

  bool reached(const size_t *tuple, size_t depth) const noexcept {
    for (size_t k = 0; k < depth; ++k)
      if (tuple[k] != boundary[k])
        return boundary[k] < tuple[k];
    // A closed prefix covers every suffix, including its final tuple.
    return true;
  }

public:
  PercentProgress(const pbqp_problem &p, pbqp_progress_callback cb, void *data,
                  size_t *scratch) noexcept
      : problem(p), callback(cb), user_data(data), boundary(scratch) {
    prepare_boundary();
  }

  void completed(const size_t *tuple) noexcept {
    completed_prefix(tuple, problem.num_nodes);
  }

  void completed_prefix(const size_t *tuple, size_t depth) noexcept {
    while (next_percent <= 100 && reached(tuple, depth)) {
      callback(next_percent, user_data);
      ++next_percent;
      if (next_percent <= 100)
        prepare_boundary();
    }
  }
};
} // namespace pbqp
