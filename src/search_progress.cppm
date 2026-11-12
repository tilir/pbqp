/*
 * Exact search-space coverage for out-of-order subtree completion.
 *
 * Workers accumulate mixed-radix counts privately and periodically merge them.
 * A lock serializes merged counts and callbacks, rather than every DFS event.
 * All counters are allocated before search, including for enormous spaces.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <mutex>
#include <vector>

module pbqp:search_progress;
import :progress;

namespace pbqp {
class CoveredSpace {
  const pbqp_problem &problem;
  std::vector<size_t> digits;
  bool complete = false;

public:
  explicit CoveredSpace(const pbqp_problem &p)
      : problem(p), digits(p.num_nodes) {}
  void add_prefix(size_t depth) noexcept {
    if (!depth) {
      complete = true;
      return;
    }
    for (size_t id = depth; id > 0; --id) {
      if (++digits[id - 1] < problem.nodes[id - 1].num_options)
        return;
      digits[id - 1] = 0;
    }
    complete = true;
  }
  void add(const CoveredSpace &other) noexcept {
    bool carry = false;
    for (size_t id = digits.size(); id > 0; --id) {
      auto &digit = digits[id - 1];
      size_t radix = problem.nodes[id - 1].num_options;
      if (carry) {
        carry = ++digit == radix;
        if (carry)
          digit = 0;
      }
      // Subtraction avoids overflowing digit + other.digit, even when a
      // domain occupies the entire size_t range.
      size_t remaining = radix - digit;
      if (other.digits[id - 1] >= remaining) {
        digit = other.digits[id - 1] - remaining;
        carry = true;
      } else {
        digit += other.digits[id - 1];
      }
    }
    complete |= other.complete || carry;
  }
  bool exceeds(const std::vector<size_t> &boundary) const noexcept {
    if (complete)
      return true;
    return std::lexicographical_compare(boundary.begin(), boundary.end(),
                                        digits.begin(), digits.end());
  }
  void clear() noexcept {
    std::fill(digits.begin(), digits.end(), size_t{0});
    complete = false;
  }
};

class SubtreeProgress {
  const pbqp_problem &problem;
  pbqp_progress_callback callback;
  void *user_data;
  CoveredSpace covered;
  std::vector<size_t> boundary;
  std::mutex mutex;
  unsigned next_percent = 1;
  size_t immediate_depth = 0;

  void report() noexcept {
    while (next_percent <= 100 && covered.exceeds(boundary)) {
      callback(next_percent, user_data);
      ++next_percent;
      if (next_percent <= 100)
        percentage_boundary(problem, next_percent, boundary.data());
    }
  }
  void merge(const CoveredSpace &pending) noexcept {
    std::scoped_lock lock(mutex);
    covered.add(pending);
    report();
  }

public:
  class Accumulator {
    SubtreeProgress &owner;
    CoveredSpace pending;
    unsigned events = 0;

  public:
    explicit Accumulator(SubtreeProgress &p) : owner(p), pending(p.problem) {}
    void completed_prefix(size_t depth) noexcept {
      pending.add_prefix(depth);
      // Large cuts report immediately. Small cuts are merged in bounded batches
      // so progress does not serialize the DFS or tuple-enumeration hot loop.
      if (++events == 256 || depth <= owner.immediate_depth)
        flush();
    }
    void flush() noexcept {
      if (!events)
        return;
      owner.merge(pending);
      pending.clear();
      events = 0;
    }
  };

  SubtreeProgress(const pbqp_problem &p, pbqp_progress_callback cb, void *data)
      : problem(p), callback(cb), user_data(data), covered(p),
        boundary(p.num_nodes) {
    percentage_boundary(problem, next_percent, boundary.data());
    // Locate roughly one-percent-sized prefixes without multiplying domains.
    // This only selects a flushing policy; coverage itself stays exact.
    size_t prefixes = 1;
    while (immediate_depth < p.num_nodes &&
           p.nodes[immediate_depth].num_options <= 100 / prefixes) {
      prefixes *= p.nodes[immediate_depth++].num_options;
    }
  }
  void completed_prefix(size_t depth) noexcept {
    std::scoped_lock lock(mutex);
    covered.add_prefix(depth);
    report();
  }
};
} // namespace pbqp
