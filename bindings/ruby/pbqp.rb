# Standard-library Fiddle binding for PBQP C API 1.0.
# SPDX-License-Identifier: GPL-3.0-only
# See LICENSE for the full license text.

require 'fiddle/import'

module PBQP
  module Solver
    NAIVE = 0
    BNB = 1
    BNBOPT = 2
    GREEDY = 3
    R1R2RN = 4
    ALL = [NAIVE, BNB, BNBOPT, GREEDY, R1R2RN].freeze
  end

  module Quality
    FEASIBLE = 0
    OPTIMAL = 1
  end

  module Status
    OK = 0
    INVALID = 1
    IO_ERROR = 2
    NO_MEMORY = 3
    OVERFLOW = 4
    INFEASIBLE = 5
    NO_SOLUTION = 6
    INTERNAL_ERROR = 7
  end

  module Native
    extend Fiddle::Importer
    Version = struct ['unsigned int major', 'unsigned int minor', 'unsigned int patch']
    Error = struct ['size_t line', 'char message[192]']
    Options = struct ['size_t num_threads', 'void* progress', 'void* user_data',
                      'void* diagnostics', 'void* diagnostic_data']
    Solution = struct ['float cost', 'int quality', 'size_t num_nodes', 'void* options']
    Evaluation = struct ['float cost', 'int is_feasible']
    # Ruby pack's Q/L directives use native byte order and fixed 64/32-bit widths.
    raise LoadError, 'unsupported size_t width' unless [4, 8].include?(Fiddle::SIZEOF_SIZE_T)
    SIZE_T_FORMAT = Fiddle::SIZEOF_SIZE_T == 8 ? 'Q' : 'L'

    # Public Problem wrappers can be copied or frozen. Only this shared mutable
    # handle owns disposal state and a finalizer, so aliases never own a second
    # native destructor and freezing a wrapper cannot prevent closing it.
    class ProblemHandle
      def self.finalizer(api, pointer)
        proc { api.pbqp_problem_free(pointer) }
      end

      def initialize(api, pointer)
        @api = api
        @pointer = pointer
        ObjectSpace.define_finalizer(self, self.class.finalizer(api, pointer))
      end

      def pointer
        raise IOError, 'PBQP problem is closed' unless @pointer

        @pointer
      end

      def close
        pointer = @pointer
        return unless pointer

        @pointer = nil
        ObjectSpace.undefine_finalizer(self)
        @api.pbqp_problem_free(pointer)
      end
    end
  end

  class Error < StandardError
    attr_reader :status, :line

    def initialize(status, error)
      @status = status
      @line = error.line
      message = error.message.pack('c*').split("\0", 2).first.to_s
      super(@line.zero? ? message : "line #{@line}: #{message}")
    end
  end

  Solution = Struct.new(:cost, :quality, :options, keyword_init: true)
  Evaluation = Struct.new(:cost, :is_feasible, keyword_init: true)

  class Library
    attr_reader :version

    def initialize(path)
      @api = Module.new
      @api.extend(Fiddle::Importer)
      @api.dlload(File.expand_path(path))
      @api.extern('void pbqp_get_api_version(void*)')
      version = Native::Version.malloc(Fiddle::RUBY_FREE)
      @api.pbqp_get_api_version(version)
      @version = [version.major, version.minor, version.patch].freeze
      raise LoadError, "unsupported PBQP C API version: #{@version}" unless version.major == 1

      @api.extern('int pbqp_read(const char*, void*, void*)')
      @api.extern('int pbqp_write(const char*, void*, void*)')
      @api.extern('void pbqp_problem_free(void*)')
      @api.extern('void pbqp_solution_free(void*)')
      @api.extern('int pbqp_solve(int, void*, void*, void*, void*)')
      @api.extern('int pbqp_evaluate_assignment(void*, void*, size_t, void*, void*)')
    end

    # With a block, close even if the block raises. Without one, the caller owns
    # the handle and should call close; a finalizer handles abandoned objects.
    def read(path)
      out = Fiddle::Pointer.malloc(Fiddle::SIZEOF_VOIDP, Fiddle::RUBY_FREE)
      error = Native::Error.malloc(Fiddle::RUBY_FREE)
      status = @api.pbqp_read(path.to_s, out, error)
      raise Error.new(status, error) unless status == Status::OK
      pointer = Fiddle::Pointer.new(out[0, Fiddle::SIZEOF_VOIDP].unpack1('J'))
      begin
        problem = Problem.new(@api, pointer)
      rescue StandardError
        @api.pbqp_problem_free(pointer)
        raise
      end
      return problem unless block_given?

      begin
        yield problem
      ensure
        problem.close
      end
    end
  end

  class Problem
    def initialize(api, pointer)
      @api = api
      @handle = Native::ProblemHandle.new(api, pointer)
    end

    # Copies share one handle: closing any alias closes all of them. A frozen
    # wrapper may also close safely. Do not close during another operation.
    def close
      @handle.close
    end

    def write(path)
      error = Native::Error.malloc(Fiddle::RUBY_FREE)
      status = @api.pbqp_write(path.to_s, require_open, error)
      raise Error.new(status, error) unless status == Status::OK
    end

    def solve(solver = Solver::BNBOPT, num_threads: 1)
      unless num_threads.is_a?(Integer) && num_threads.between?(1, 2**31 - 1)
        raise ArgumentError, 'num_threads must be positive and fit in int'
      end
      raise ArgumentError, 'unknown solver' unless Solver::ALL.include?(solver)

      config = Native::Options.malloc(Fiddle::RUBY_FREE)
      config.to_ptr[0, Native::Options.size] = "\0" * Native::Options.size
      config.num_threads = num_threads
      out = Fiddle::Pointer.malloc(Fiddle::SIZEOF_VOIDP, Fiddle::RUBY_FREE)
      error = Native::Error.malloc(Fiddle::RUBY_FREE)
      status = @api.pbqp_solve(solver, require_open, config, out, error)
      raise Error.new(status, error) unless status == Status::OK

      pointer = Fiddle::Pointer.new(out[0, Fiddle::SIZEOF_VOIDP].unpack1('J'))
      begin
        result = Native::Solution.new(pointer)
        options = result.num_nodes.zero? ? [] : result.options[
          0, result.num_nodes * Fiddle::SIZEOF_SIZE_T
        ].unpack("#{Native::SIZE_T_FORMAT}*")
        Solution.new(cost: result.cost, quality: result.quality,
                     options: options.freeze).freeze
      ensure
        @api.pbqp_solution_free(pointer)
      end
    end

    def evaluate(options)
      values = options.to_a
      maximum = 2**(8 * Fiddle::SIZEOF_SIZE_T) - 1
      unless values.all? { |v| v.is_a?(Integer) && v.between?(0, maximum) }
        raise ArgumentError, 'options must contain size_t-representable indices'
      end
      assignment = values.pack("#{Native::SIZE_T_FORMAT}*")
      result = Native::Evaluation.malloc(Fiddle::RUBY_FREE)
      error = Native::Error.malloc(Fiddle::RUBY_FREE)
      status = @api.pbqp_evaluate_assignment(require_open, assignment, values.size,
                                            result, error)
      raise Error.new(status, error) unless status == Status::OK

      Evaluation.new(cost: result.cost, is_feasible: !result.is_feasible.zero?).freeze
    end

    private

    def require_open
      @handle.pointer
    end
  end
end
