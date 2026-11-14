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
#include <fstream>
#include <memory>

module pbqp;
import :api_boundary;
import :problem;
import :failure;
import :error;
import :ownership;
import :problem_reader;
import :problem_writer;

extern "C" {
pbqp_status pbqp_read(const char *path, pbqp_problem **out, pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, [&]() -> pbqp_status {
    if (!path || !out)
      return pbqp::set_error(error, PBQP_INVALID, 0,
                             "missing path or output pointer");
    std::ifstream input(path, std::ios::binary);
    if (!input)
      pbqp::fail(PBQP_IO_ERROR, "cannot open input file");
    *out = pbqp::ProblemReader(input).read().release();
    return PBQP_OK;
  });
}

pbqp_status pbqp_write(const char *path, const pbqp_problem *p,
                       pbqp_error *error) {
  return pbqp::api_call(error, [&]() -> pbqp_status {
    pbqp::validate(p);
    if (!path)
      pbqp::fail(PBQP_INVALID, "missing output path");
    const pbqp::ProblemWriter writer(*p);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
      pbqp::fail(PBQP_IO_ERROR, "cannot open output file");
    writer.write(output);
    output.close();
    if (!output)
      pbqp::fail(PBQP_IO_ERROR, "output write failed");
    return PBQP_OK;
  });
}
} // extern "C"
