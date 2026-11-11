/*
 * Non-throwing C API error reporting.
 *
 * Copies diagnostic text into the caller-provided fixed-size error buffer.
 * These helpers report solver failures without throwing exceptions.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include "pbqp.h"
#include <algorithm>
#include <string>

module pbqp:error;

inline pbqp_status pbqp_fail(pbqp_error *error, pbqp_status status, size_t line,
                             const char *message) {
  if (error) {
    error->line = line;
    auto length = std::min(std::char_traits<char>::length(message),
                           sizeof(error->message) - 1);
    std::copy_n(message, length, error->message);
    error->message[length] = '\0';
  }
  return status;
}

inline void pbqp_clear_error(pbqp_error *error) {
  if (error) {
    error->line = 0;
    error->message[0] = '\0';
  }
}
