"""Small ctypes binding for PBQP C API 1.0: file I/O, solving, and evaluation.

SPDX-License-Identifier: GPL-3.0-only
See LICENSE for the full license text.
"""

import ctypes as ct
from dataclasses import dataclass
from enum import IntEnum
import os
from pathlib import Path
import weakref


class Solver(IntEnum):
    NAIVE = 0
    BNB = 1
    BNBOPT = 2
    GREEDY = 3
    R1R2RN = 4


class Quality(IntEnum):
    FEASIBLE = 0
    OPTIMAL = 1


class Status(IntEnum):
    OK = 0
    INVALID = 1
    IO_ERROR = 2
    NO_MEMORY = 3
    OVERFLOW = 4
    INFEASIBLE = 5
    NO_SOLUTION = 6
    INTERNAL_ERROR = 7


class PBQPError(RuntimeError):
    def __init__(self, status, error):
        self.status = Status(status)
        self.line = error.line
        message = error.message.decode("utf-8", errors="replace")
        super().__init__(f"line {self.line}: {message}" if self.line else message)


class _Version(ct.Structure):
    _fields_ = [("major", ct.c_uint), ("minor", ct.c_uint), ("patch", ct.c_uint)]


class _Error(ct.Structure):
    _fields_ = [("line", ct.c_size_t), ("message", ct.c_char * 192)]


class _Problem(ct.Structure):
    pass  # Parsed problems are opaque to this binding.


class _Solution(ct.Structure):
    _fields_ = [
        ("cost", ct.c_float),
        ("quality", ct.c_int),
        ("num_nodes", ct.c_size_t),
        ("options", ct.POINTER(ct.c_size_t)),
    ]


_Progress = ct.CFUNCTYPE(None, ct.c_uint, ct.c_void_p)
_Diagnostic = ct.CFUNCTYPE(None, ct.c_char_p, ct.c_void_p)


class _Options(ct.Structure):
    _fields_ = [
        ("num_threads", ct.c_size_t),
        ("progress", _Progress),
        ("user_data", ct.c_void_p),
        ("diagnostics", _Diagnostic),
        ("diagnostic_data", ct.c_void_p),
    ]


class _Evaluation(ct.Structure):
    _fields_ = [("cost", ct.c_float), ("is_feasible", ct.c_int)]


@dataclass(frozen=True)
class Solution:
    cost: float
    quality: Quality
    options: tuple[int, ...]


@dataclass(frozen=True)
class Evaluation:
    cost: float
    is_feasible: bool


def _check(status, error):
    if status != Status.OK:
        raise PBQPError(status, error)


class Library:
    """Load an explicit shared-library path and verify C API compatibility."""

    def __init__(self, path):
        self._dll = ct.CDLL(str(Path(path).resolve()))
        dll = self._dll
        dll.pbqp_get_api_version.argtypes = [ct.POINTER(_Version)]
        dll.pbqp_get_api_version.restype = None
        version = _Version()
        dll.pbqp_get_api_version(ct.byref(version))
        self.version = (version.major, version.minor, version.patch)
        if version.major != 1:
            raise RuntimeError(f"unsupported PBQP C API version: {self.version}")
        problem = ct.POINTER(_Problem)
        solution = ct.POINTER(_Solution)
        error = ct.POINTER(_Error)
        dll.pbqp_read.argtypes = [ct.c_char_p, ct.POINTER(problem), error]
        dll.pbqp_read.restype = ct.c_int
        dll.pbqp_write.argtypes = [ct.c_char_p, problem, error]
        dll.pbqp_write.restype = ct.c_int
        dll.pbqp_problem_free.argtypes = [problem]
        dll.pbqp_problem_free.restype = None
        dll.pbqp_solution_free.argtypes = [solution]
        dll.pbqp_solution_free.restype = None
        dll.pbqp_solve.argtypes = [
            ct.c_int, problem, ct.POINTER(_Options), ct.POINTER(solution), error
        ]
        dll.pbqp_solve.restype = ct.c_int
        dll.pbqp_evaluate_assignment.argtypes = [
            problem, ct.POINTER(ct.c_size_t), ct.c_size_t,
            ct.POINTER(_Evaluation), error
        ]
        dll.pbqp_evaluate_assignment.restype = ct.c_int

    def read(self, path):
        pointer = ct.POINTER(_Problem)()
        error = _Error()
        _check(self._dll.pbqp_read(os.fsencode(path), ct.byref(pointer),
                                  ct.byref(error)), error)
        try:
            return Problem(self, pointer)
        except Exception:
            self._dll.pbqp_problem_free(pointer)
            raise


class Problem:
    """Own a parsed problem. Use a context manager or call close explicitly.

    A finalizer also releases abandoned handles. Do not close a problem while
    another thread is using it. Returned Solution objects contain Python-owned
    copies and remain valid after close.
    """

    def __init__(self, library, pointer):
        self._library = library
        self._pointer = pointer
        self._cleanup = weakref.finalize(
            self, library._dll.pbqp_problem_free, pointer
        )

    def _require_open(self):
        if not self._cleanup.alive:
            raise RuntimeError("PBQP problem is closed")
        return self._pointer

    def close(self):
        self._cleanup()

    def __enter__(self):
        self._require_open()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def write(self, path):
        error = _Error()
        _check(self._library._dll.pbqp_write(
            os.fsencode(path), self._require_open(), ct.byref(error)
        ), error)

    def solve(self, solver=Solver.BNBOPT, *, num_threads=1):
        if not isinstance(num_threads, int) or not 0 < num_threads <= 2**31 - 1:
            raise ValueError("num_threads must be positive and fit in int")
        config = _Options(num_threads, _Progress(), None, _Diagnostic(), None)
        pointer = ct.POINTER(_Solution)()
        error = _Error()
        _check(self._library._dll.pbqp_solve(
            int(Solver(solver)), self._require_open(), ct.byref(config),
            ct.byref(pointer), ct.byref(error)
        ), error)
        try:
            value = pointer.contents
            return Solution(value.cost, Quality(value.quality),
                            tuple(value.options[i] for i in range(value.num_nodes)))
        finally:
            self._library._dll.pbqp_solution_free(pointer)

    def evaluate(self, options):
        values = tuple(options)
        max_size = ct.c_size_t(-1).value
        if any(not isinstance(value, int) or not 0 <= value <= max_size
               for value in values):
            raise ValueError("options must contain size_t-representable indices")
        assignment = (ct.c_size_t * len(values))(*values)
        result = _Evaluation()
        error = _Error()
        _check(self._library._dll.pbqp_evaluate_assignment(
            self._require_open(), assignment, len(values), ct.byref(result),
            ct.byref(error)
        ), error)
        return Evaluation(result.cost, bool(result.is_feasible))
