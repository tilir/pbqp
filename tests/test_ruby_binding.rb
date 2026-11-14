# Exercise Fiddle layouts, all solver selections, errors, and native ownership.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

require 'pbqp'
require 'tmpdir'
require 'weakref'
require 'stringio'

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

# Fake pointers make ownership failures safe: the mock records deallocations
# without passing sentinel addresses to the actual native allocator.
class MockApi
  def initialize(frees)
    @frees = frees
  end

  def pbqp_problem_free(pointer)
    @frees << pointer.to_i
  end
end

def mock_problem(address, frees)
  PBQP::Problem.new(MockApi.new(frees), Fiddle::Pointer.new(address))
end

def abandoned_aliases(address, frees, frozen)
  # MRI conservatively scans native stacks: a returned method can leave stale
  # temporary pointers which keep wrappers alive despite clearing Ruby locals.
  # Create wrappers on a separate fiber so the caller's native stack never
  # contains those pointers when it requests collection.
  Fiber.new do
    original = mock_problem(address, frees)
    original.freeze if frozen
    copy = original.dup
    cloned = original.clone
    references = [WeakRef.new(original), WeakRef.new(copy), WeakRef.new(cloned)]
    original = copy = cloned = nil
    references
  end.resume
end

[false, true].each_with_index do |frozen, index|
  frees = []
  address = index + 1
  original = mock_problem(address, frees)
  original.freeze if frozen
  copies = [original.dup, original.clone]
  copies.first.freeze
  check(original.frozen? == frozen && copies.last.frozen? == frozen)
  copies.last.close
  [original, *copies].each do |problem|
    problem.close
    expect_error(IOError) { problem.solve }
    expect_error(IOError) { problem.write('unused.pbqp') }
  end
  check(frees == [address])
  copies = original = nil
  3.times { GC.start }
  check(frees == [address])

  frees = []
  references = abandoned_aliases(address, frees, frozen)
  3.times { GC.start }
  check(references.none?(&:weakref_alive?))
  check(frees == [address])
end

# Collecting one alias must not finalize the handle retained by another.
frees = []
original = mock_problem(3, frees)
copy = original.dup
original = nil
3.times { GC.start }
check(frees.empty?)
copy.close
check(frees == [3])
copy = nil
3.times { GC.start }
check(frees == [3])

library = PBQP::Library.new(ARGV[0])
check(library.version == [2, 0, 0])
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

original_stderr = $stderr
warning_output = StringIO.new
begin
  $stderr = warning_output
  library.read(ARGV[1]) do |problem|
    [0, 1, 2].each do |level|
      result = problem.solve(PBQP::Solver::BNB, options: {
        'optimization-level' => level, 'weighted-bounds' => true,
        'repricing' => false, 'application.setting' => 'ignored'
      })
      check(result.cost.zero? && result.quality == PBQP::Quality::OPTIMAL)
    end
    strict = expect_error(PBQP::Error) do
      problem.solve(options: { 'unknown' => true, 'warnings-as-errors' => true })
    end
    check(strict.status == PBQP::Status::INVALID)
    error = expect_error(PBQP::Error) do
      problem.solve(PBQP::Solver::BNB, options: { 'optimization-level' => 3 })
    end
    check(error.status == PBQP::Status::INVALID)
    expect_error(PBQP::Error) do
      problem.solve(PBQP::Solver::BNB, options: { 'weighted-bounds' => 1 })
    end
    result = problem.solve(PBQP::Solver::NAIVE, options: {
      'optimization-level' => 'ignored', 'weighted-bounds' => 'ignored'
    })
    check(result.cost.zero?)
    [{ '' => true }, { "a\0b" => true }, { 'key' => "a\0b" },
     { 'key' => -1 }, { 'key' => 2**100 }, { 'key' => 1.5 }].each do |options|
      expect_error(ArgumentError) { problem.solve(options: options) }
    end
    expect_error(PBQP::Error) { problem.solve(options: { 'num-threads' => 0 }) }
  end

ensure
  $stderr = original_stderr
end
check(warning_output.string.scan('unrecognized solver option: application.setting').size == 3)
check(warning_output.string.include?('unrecognized solver option: weighted-bounds'))

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
