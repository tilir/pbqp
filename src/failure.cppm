/*
 * Internal failure transport for parsing and validation.
 *
 * Carries status, line, and diagnostic information to the C API boundary.
 * This exception-based helper is excluded from the solver implementation.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include "pbqp.h"

module pbqp:failure;

struct Failure {
  pbqp_status status;
  size_t line;
  const char *message;
};

[[noreturn]] inline void fail(pbqp_status status, const char *message,
                              size_t line = 0) {
  throw Failure{status, line, message};
}
