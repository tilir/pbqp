/*
 * Exception translation at the public C API boundary.
 *
 * Keeps allocation failures and container limits out of solver logic while
 * preserving status codes and bounded diagnostics for C callers.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>

module pbqp:api_boundary;
import :error;
import :failure;

namespace pbqp {
size_t thread_count(const pbqp_solve_options *options) {
  size_t count = options ? options->num_threads : 1;
  if (!count || count > static_cast<size_t>(std::numeric_limits<int>::max()))
    fail(PBQP_INVALID, "num_threads must be positive and fit in int");
  return count;
}

template <typename Operation>
pbqp_status api_call(pbqp_error *error, Operation operation) noexcept {
  clear_error(error);
  try {
    return operation();
  } catch (const Failure &failure) {
    return set_error(error, failure.status, failure.line, failure.message);
  } catch (const std::length_error &) {
    return set_error(error, PBQP_OVERFLOW, 0,
                     "requested container size exceeds limits");
  } catch (const std::bad_alloc &) {
    return set_error(error, PBQP_NO_MEMORY, 0, "allocation failed");
  } catch (const std::exception &failure) {
    return set_error(error, PBQP_INTERNAL_ERROR, 0, failure.what());
  }
}
} // namespace pbqp
