# Solve a PBQP file through the reusable Fiddle binding.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

require 'pbqp'

abort 'Usage: ruby solve.rb LIBRARY INPUT' unless ARGV.size == 2
begin
  library = PBQP::Library.new(ARGV[0])
  solution = library.read(ARGV[1]) do |problem|
    result = problem.solve(PBQP::Solver::BNB, num_threads: 4,
                           options: { 'optimization-level' => 2, 'repricing' => true })
    evaluation = problem.evaluate(result.options)
    unless evaluation.is_feasible && evaluation.cost == result.cost
      raise 'solution evaluation mismatch'
    end
    result
  end
  # The result contains Ruby-owned values, independent of the closed problem.
  puts "COST #{format('%.9g', solution.cost)}"
  solution.options.each_with_index { |option, node| puts "NODE #{node} #{option}" }
rescue PBQP::Error, Fiddle::DLError, LoadError, IOError, RuntimeError => error
  warn error.message
  exit 1
end
