"""Solve a PBQP file through the reusable ctypes binding.

SPDX-License-Identifier: GPL-3.0-only
See LICENSE for the full license text.
"""

import argparse

from pbqp import Library, PBQPError, Solver


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", help="path to the PBQP shared library")
    parser.add_argument("input", help="PBQP input file")
    args = parser.parse_args()
    try:
        library = Library(args.library)
        with library.read(args.input) as problem:
            solution = problem.solve(Solver.BNB, num_threads=4,
                                     options={"optimization-level": 2, "repricing": True})
            evaluation = problem.evaluate(solution.options)
            if not evaluation.is_feasible or evaluation.cost != solution.cost:
                raise RuntimeError("solution evaluation mismatch")
        # The returned assignment contains Python-owned data.
        print(f"COST {solution.cost:.9g}")
        for node, option in enumerate(solution.options):
            print(f"NODE {node} {option}")
    except (PBQPError, OSError, RuntimeError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
