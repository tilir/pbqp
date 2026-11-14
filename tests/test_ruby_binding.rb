# Exercise Fiddle layouts, all solver selections, errors, and native ownership.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

require 'pbqp'
require 'tmpdir'

abort 'expected LIBRARY INPUT' unless ARGV.size == 2

def check(condition)
  raise "check failed at #{caller(1, 1).first}" unless condition
end

def expect_error(type)
  begin
    yield
  rescue type => error
    return error
  end
  raise "expected #{type}"
end

library = PBQP::Library.new(ARGV[0])
check(library.version == [1, 0, 0])
closed = nil
solutions = library.read(ARGV[1]) do |problem|
  closed = problem
  PBQP::Solver::ALL.map do |solver|
    result = problem.solve(solver, num_threads: 4)
    evaluation = problem.evaluate(result.options)
    check(evaluation.is_feasible && evaluation.cost == result.cost)
    check(result.cost.zero?)
    expected = [PBQP::Solver::GREEDY, PBQP::Solver::R1R2RN].include?(solver) ?
      PBQP::Quality::FEASIBLE : PBQP::Quality::OPTIMAL
    check(result.quality == expected)
    result
  end
end
closed.close
expect_error(IOError) { closed.solve }
check(solutions.all? { |s| !s.options.empty? && s.frozen? && s.options.frozen? })

Dir.mktmpdir('pbqp-ruby') do |directory|
  path = File.join(directory, 'copy.pbqp')
  original = library.read(ARGV[1]) do |problem|
    problem.write(path)
    [0, -1, 2**31, 2**64, 1.5].each do |threads|
      expect_error(ArgumentError) { problem.solve(num_threads: threads) }
    end
    expect_error(ArgumentError) { problem.solve(99) }
    error = expect_error(PBQP::Error) { problem.evaluate([]) }
    check(error.status == PBQP::Status::INVALID)
    expect_error(ArgumentError) { problem.evaluate([-1]) }
    problem.solve(PBQP::Solver::NAIVE)
  end
  library.read(path) { |problem| check(problem.solve(PBQP::Solver::NAIVE) == original) }
  error = expect_error(PBQP::Error) { library.read(File.join(directory, 'missing.pbqp')) }
  check(error.status == PBQP::Status::IO_ERROR)
  File.write(path, "PBQP 1\nNODES 1\nEDGES 0\nNODE 0 1\nINF\nEND\n")
  library.read(path) do |problem|
    check(!problem.evaluate([0]).is_feasible)
    error = expect_error(PBQP::Error) { problem.solve(PBQP::Solver::NAIVE) }
    check(error.status == PBQP::Status::INFEASIBLE)
    error = expect_error(PBQP::Error) { problem.solve(PBQP::Solver::GREEDY) }
    check(error.status == PBQP::Status::NO_SOLUTION)
  end
  File.write(path, "PBQP 1\nNODES 0\nEDGES 0\nEND\n")
  library.read(path) do |problem|
    PBQP::Solver::ALL.each do |solver|
      result = problem.solve(solver)
      check(result.options.empty? && result.cost.zero?)
      check(problem.evaluate([]).is_feasible)
    end
  end
  File.write(path, "PBQP 1\nBROKEN\n")
  error = expect_error(PBQP::Error) { library.read(path) }
  check(error.status == PBQP::Status::INVALID && error.line == 2)
end
puts 'Ruby binding checks passed'
