/*
 * PBQP file input and output.
 *
 * Implements the public C path-based API using C++ file streams.
 * Owns partial read results and translates internal failures to C status codes.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp;
import :api_boundary;
import :io_api;

extern "C" {
pbqp_status pbqp_read(const char *path, pbqp_problem **out, pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, pbqp::read_problem, path, out, error);
}

pbqp_status pbqp_write(const char *path, const pbqp_problem *p,
                       pbqp_error *error) {
  return pbqp::api_call(error, pbqp::write_problem, path, p);
}
} // extern "C"
