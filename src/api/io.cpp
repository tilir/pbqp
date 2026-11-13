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
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <set>
#include <utility>

module pbqp;
import :api_boundary;
import :problem;
import :failure;
import :error;
import :ownership;
import :text_reader;
import :text_writer;

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
    pbqp::TextReader r(input);
    r.expect("PBQP");
    r.expect("1");
    r.expect("NODES");
    size_t num_nodes = r.size();
    r.expect("EDGES");
    size_t num_edges = r.size();
    pbqp::check_count<pbqp_node>(num_nodes, r.token_line());
    pbqp::check_count<pbqp_edge>(num_edges, r.token_line());
    auto p = std::make_unique<pbqp::ProblemStorage>(num_nodes, num_edges);
    for (size_t k = 0; k < p->num_nodes; ++k) {
      r.expect("NODE");
      if (r.size() != k)
        pbqp::fail(PBQP_INVALID,
                   "nodes must be numbered in increasing order from zero",
                   r.token_line());
      auto &node = p->node_records[k];
      node.num_options = r.size();
      if (!node.num_options)
        pbqp::fail(PBQP_INVALID, "node must have at least one option",
                   r.token_line());
      pbqp::check_count<pbqp_cost_t>(node.num_options, r.token_line());
      auto &costs = p->unary_costs[k];
      costs.resize(node.num_options);
      node.costs = costs.data();
      for (size_t a = 0; a < node.num_options; ++a)
        costs[a] = r.cost();
    }
    std::set<std::pair<size_t, size_t>> edges;
    for (size_t k = 0; k < p->num_edges; ++k) {
      r.expect("EDGE");
      auto &e = p->edge_records[k];
      e.src = r.size();
      e.tgt = r.size();
      if (e.src >= e.tgt || e.tgt >= p->num_nodes)
        pbqp::fail(PBQP_INVALID, "edge must satisfy 0 <= src < tgt < NODES",
                   r.token_line());
      if (!edges.emplace(e.src, e.tgt).second)
        pbqp::fail(PBQP_INVALID, "duplicate edge", r.token_line());
      size_t count = pbqp::matrix_size(*p, e, r.token_line());
      auto &costs = p->pairwise_costs[k];
      costs.resize(count);
      e.costs = costs.data();
      for (size_t a = 0; a < count; ++a)
        costs[a] = r.cost();
    }
    r.expect("END");
    if (r.next())
      pbqp::fail(PBQP_INVALID, "unexpected data after END", r.token_line());
    *out = p.release();
    return PBQP_OK;
  });
}

pbqp_status pbqp_write(const char *path, const pbqp_problem *p,
                       pbqp_error *error) {
  return pbqp::api_call(error, [&]() -> pbqp_status {
    pbqp::validate(p);
    if (!path)
      pbqp::fail(PBQP_INVALID, "missing output path");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
      pbqp::fail(PBQP_IO_ERROR, "cannot open output file");
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<pbqp_cost_t>::max_digits10);
    output << "PBQP 1\nNODES " << p->num_nodes << "\nEDGES " << p->num_edges
           << '\n';
    for (size_t k = 0; k < p->num_nodes; ++k) {
      const auto &node = p->nodes[k];
      output << "\nNODE " << k << ' ' << node.num_options << '\n';
      for (size_t a = 0; a < node.num_options; ++a) {
        pbqp::write_cost(output, node.costs[a]);
        output << (a + 1 == node.num_options ? '\n' : ' ');
      }
    }
    for (size_t k = 0; k < p->num_edges; ++k) {
      const auto &e = p->edges[k];
      size_t cols = p->nodes[e.tgt].num_options;
      size_t count = pbqp::matrix_size(*p, e);
      output << "\nEDGE " << e.src << ' ' << e.tgt << '\n';
      for (size_t a = 0; a < count; ++a) {
        pbqp::write_cost(output, e.costs[a]);
        output << ((a + 1) % cols == 0 ? '\n' : ' ');
      }
    }
    output << "\nEND\n";
    output.close();
    if (!output)
      pbqp::fail(PBQP_IO_ERROR, "output write failed");
    return PBQP_OK;
  });
}
} // extern "C"
