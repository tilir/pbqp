/*
 * Internal file I/O orchestration over the text parser and serializer.
 * Stream opening, completion checks, and ownership transfer may throw; the
 * public C entry points translate failures through the shared boundary.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <fstream>
#include <memory>
module pbqp:io_api;
import :problem;
import :failure;
import :error;
import :ownership;
import :problem_reader;
import :problem_writer;

namespace pbqp {
pbqp_status read_problem(const char *path, pbqp_problem **out,
                         pbqp_error *error) {
  if (!path || !out)
    return set_error(error, PBQP_INVALID, 0, "missing path or output pointer");
  std::ifstream input(path, std::ios::binary);
  if (!input)
    fail(PBQP_IO_ERROR, "cannot open input file");
  *out = ProblemReader(input).read().release();
  return PBQP_OK;
}

pbqp_status write_problem(const char *path, const pbqp_problem *p) {
  validate(p);
  if (!path)
    fail(PBQP_INVALID, "missing output path");
  const ProblemWriter writer(*p);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    fail(PBQP_IO_ERROR, "cannot open output file");
  writer.write(output);
  output.close();
  if (!output)
    fail(PBQP_IO_ERROR, "output write failed");
  return PBQP_OK;
}
} // namespace pbqp
