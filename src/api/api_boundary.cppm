/*
 * Exception translation at the public C API boundary.
 *
 * Keeps allocation failures and container limits out of solver logic while
 * preserving status codes and bounded diagnostics for C callers. Named
 * functions receive forwarded arguments directly; void return means success
 * unless the operation throws. No captured function-sized closure is needed.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <exception>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

module pbqp:api_boundary;
import :error;
import :failure;

namespace pbqp {
template <typename Result, typename... Parameters, typename... Arguments>
pbqp_status api_call(pbqp_error *error, Result (*operation)(Parameters...),
                     Arguments &&...arguments) noexcept {
  clear_error(error);
  try {
    if constexpr (std::is_void_v<Result>) {
      operation(std::forward<Arguments>(arguments)...);
      return PBQP_OK;
    } else {
      return operation(std::forward<Arguments>(arguments)...);
    }
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
