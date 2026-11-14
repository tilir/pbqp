"""Check ctypes signatures, failures, ownership, and all solver selections.

SPDX-License-Identifier: GPL-3.0-only
See LICENSE for the full license text.
"""

from pathlib import Path
import sys
import tempfile
import unittest
import warnings

from pbqp import Library, PBQPError, Quality, Solver, Status

LIBRARY = sys.argv[1]
EXAMPLE = sys.argv[2]


class BindingTests(unittest.TestCase):
    def setUp(self):
        self.library = Library(LIBRARY)

    def test_all_solvers_and_lifetime(self):
        self.assertEqual(self.library.version, (2, 0, 0))
        with self.library.read(EXAMPLE) as problem:
            solutions = []
            for solver in Solver:
                solution = problem.solve(solver, num_threads=4)
                evaluation = problem.evaluate(solution.options)
                self.assertTrue(evaluation.is_feasible)
                self.assertEqual(evaluation.cost, solution.cost)
                self.assertEqual(solution.cost, 0)
                self.assertEqual(solution.quality,
                                 Quality.FEASIBLE if solver in (Solver.GREEDY,
                                                               Solver.R1R2RN)
                                 else Quality.OPTIMAL)
                solutions.append(solution)
        problem.close()
        self.assertTrue(all(s.options for s in solutions))
        with self.assertRaises(RuntimeError):
            problem.solve()

    def test_open_option_map(self):
        with self.library.read(EXAMPLE) as problem:
            for level in (0, 1, 2):
                with self.assertWarnsRegex(UserWarning, "application.setting"):
                    result = problem.solve(Solver.BNB, options={
                        "optimization-level": level,
                        "weighted-bounds": True,
                        "repricing": False,
                        "application.setting": "ignored",
                    })
                self.assertEqual(result.cost, 0)
                self.assertEqual(result.quality, Quality.OPTIMAL)
            with self.assertRaises(PBQPError) as caught:
                problem.solve(Solver.BNB, options={"optimization-level": 3})
            self.assertEqual(caught.exception.status, Status.INVALID)
            with self.assertRaises(PBQPError):
                problem.solve(Solver.BNB, options={"weighted-bounds": 1})
            with self.assertRaises(PBQPError) as caught:
                problem.solve(options={"unknown": True, "warnings-as-errors": True})
            self.assertEqual(caught.exception.status, Status.INVALID)
            with warnings.catch_warnings():
                warnings.simplefilter("error")
                with self.assertRaises(UserWarning):
                    problem.solve(options={"unknown": True})
            # Algorithm-specific keys warn on unrelated solvers.

            with warnings.catch_warnings(record=True) as recorded:
                warnings.simplefilter("always")
                self.assertEqual(problem.solve(Solver.NAIVE, options={
                    "optimization-level": "ignored", "weighted-bounds": "ignored"
                }).cost, 0)
            self.assertEqual(len(recorded), 2)
            for options in ({"": True}, {"a\0b": True}, {"key": "a\0b"},
                            {"key": -1}, {"key": 2**100}):
                with self.assertRaises(ValueError):
                    problem.solve(options=options)
            with self.assertRaises(TypeError):
                problem.solve(options={"key": 1.5})
            # A map key follows the convenience argument and therefore wins.
            with self.assertRaises(PBQPError):
                problem.solve(options={"num-threads": 0})

    def test_roundtrip_and_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "copy.pbqp"
            with self.library.read(EXAMPLE) as problem:
                problem.write(path)
                original = problem.solve(Solver.NAIVE)
                for threads in (0, -1, 2**31, 2**64, 1.5):
                    with self.assertRaises(ValueError):
                        problem.solve(num_threads=threads)
                with self.assertRaises(ValueError):
                    problem.solve(99)
                with self.assertRaises(PBQPError) as caught:
                    problem.evaluate(())
                self.assertEqual(caught.exception.status, Status.INVALID)
                with self.assertRaises(ValueError):
                    problem.evaluate([-1])
            with self.library.read(path) as copied:
                self.assertEqual(copied.solve(Solver.NAIVE), original)
            with self.assertRaises(PBQPError) as caught:
                self.library.read(Path(directory) / "missing.pbqp")
            self.assertEqual(caught.exception.status, Status.IO_ERROR)
            path.write_text("PBQP 1\nNODES 1\nEDGES 0\nNODE 0 1\nINF\nEND\n")
            with self.library.read(path) as problem:
                self.assertFalse(problem.evaluate([0]).is_feasible)
                with self.assertRaises(PBQPError) as caught:
                    problem.solve(Solver.NAIVE)
                self.assertEqual(caught.exception.status, Status.INFEASIBLE)
                with self.assertRaises(PBQPError) as caught:
                    problem.solve(Solver.GREEDY)
                self.assertEqual(caught.exception.status, Status.NO_SOLUTION)

    def test_empty_and_parse_line(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty.pbqp"
            path.write_text("PBQP 1\nNODES 0\nEDGES 0\nEND\n")
            with self.library.read(path) as problem:
                for solver in Solver:
                    solution = problem.solve(solver)
                    self.assertEqual(solution.options, ())
                    self.assertEqual(solution.cost, 0)
                    self.assertTrue(problem.evaluate(()).is_feasible)
            path.write_text("PBQP 1\nBROKEN\n")
            with self.assertRaises(PBQPError) as caught:
                self.library.read(path)
            self.assertEqual(caught.exception.status, Status.INVALID)
            self.assertEqual(caught.exception.line, 2)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
