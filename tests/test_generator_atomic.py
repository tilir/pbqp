"""Inject output failures and check generator publication and staging cleanup.

SPDX-License-Identifier: GPL-3.0-only
See LICENSE for the full license text.
"""

from pathlib import Path
import os
import signal
import subprocess
import sys
import tempfile

if os.name != "posix":
    sys.exit(0)

import resource


def limit_output():
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (100, 100))


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    target = root / "output.pbqp"
    original = b"previous complete file\n"
    target.write_bytes(original)
    arguments = [sys.argv[1], "--nodes=0", "--edges=0", "--max-options=1",
                 "--seed=0", f"--output={target}"]
    # The empty task fits in 100 bytes; the provenance does not. This exercises
    # the second write, rather than rejecting parameters or the destination.
    failed = subprocess.run(arguments, preexec_fn=limit_output,
                            capture_output=True, timeout=10)
    assert failed.returncode == 1, failed
    assert b"metadata write failed" in failed.stderr, failed.stderr
    assert target.read_bytes() == original
    assert list(root.iterdir()) == [target], list(root.iterdir())

    # The same failure must not publish a new, apparently complete task either.
    target.unlink()
    failed = subprocess.run(arguments, preexec_fn=limit_output,
                            capture_output=True, timeout=10)
    assert failed.returncode == 1, failed
    assert not target.exists()
    assert not list(root.iterdir())

    complete = subprocess.run(arguments, capture_output=True, timeout=10)
    assert complete.returncode == 0, complete.stderr
    contents = target.read_bytes()
    assert b"END\n# generator: " in contents and b"# command:" in contents
    assert list(root.iterdir()) == [target]
