"""
pytest configuration for Platemaker CLI integration tests.

The ``platemaker_bin`` session fixture returns a ``pathlib.Path`` to the
compiled ``platemaker`` binary.  CTest passes the path via the
``--platemaker-bin`` command-line option.  When running pytest directly
(outside CTest) you can either:

  * pass  --platemaker-bin /path/to/platemaker  on the command line, or
  * set   the PLATEMAKER_BIN environment variable.
"""

from __future__ import annotations

import datetime
import os
import pathlib
import subprocess
import sys
import time

import pytest


# ---------------------------------------------------------------------------
# Diagnostic tracing
# ---------------------------------------------------------------------------
#
# The suite intermittently hangs at a random test (see docs/TODO.md). Per-test
# timestamps tell us *which* test stalled — the last one to print START without a
# matching END — and whether the run crept slower over time or one test froze.
#
# Flip this one constant to turn the trace off again when the investigation is
# done; the hooks below stay in place. --pm-trace / PM_TRACE=0 override it per run.
TRACE_DEFAULT = True


def _trace_enabled(config: pytest.Config) -> bool:
    if config.getoption("--pm-trace"):
        return True
    env = os.environ.get("PM_TRACE")
    if env is not None:
        return env not in ("", "0", "false", "False")
    return TRACE_DEFAULT


# ---------------------------------------------------------------------------
# Custom command-line options
# ---------------------------------------------------------------------------

def pytest_addoption(parser: pytest.Parser) -> None:
    """Register the --platemaker-bin and --pm-trace options."""
    parser.addoption(
        "--platemaker-bin",
        action="store",
        default=None,
        metavar="PATH",
        help="Path to the compiled platemaker CLI binary. "
             "Falls back to the PLATEMAKER_BIN environment variable.",
    )
    parser.addoption(
        "--pm-trace",
        action="store_true",
        default=False,
        help="Emit a timestamped START/END line per test to diagnose hangs. "
             "Also enabled by PM_TRACE=1; default set by TRACE_DEFAULT in conftest.",
    )


# ---------------------------------------------------------------------------
# Trace hooks — timestamped test boundaries
# ---------------------------------------------------------------------------

# Monotonic clock for deltas (immune to wall-clock adjustments); wall clock only
# for the human-readable stamp.
_last_event = [0.0]


def _emit(tag: str, nodeid: str) -> None:
    now = time.monotonic()
    delta = now - _last_event[0] if _last_event[0] else 0.0
    _last_event[0] = now
    stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    # Bypass pytest's output capture so the line survives even if we later kill a
    # hung process: the real stderr is flushed immediately.
    sys.__stderr__.write(f"[{stamp}  +{delta:6.2f}s] {tag:5} {nodeid}\n")
    sys.__stderr__.flush()


# logstart/logfinish don't receive `config`, so resolve the flag once at configure
# time and keep it module-level.
_trace_on = [False]


def pytest_configure(config: pytest.Config) -> None:
    _trace_on[0] = _trace_enabled(config)


def pytest_runtest_logstart(nodeid: str, location: object) -> None:
    if _trace_on[0]:
        _emit("START", nodeid)


def pytest_runtest_logfinish(nodeid: str, location: object) -> None:
    if _trace_on[0]:
        _emit("END", nodeid)


# ---------------------------------------------------------------------------
# Session-scoped fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def platemaker_bin(request: pytest.FixtureRequest) -> pathlib.Path:
    """
    Return the absolute path to the platemaker CLI binary.

    Resolution order:
    1. ``--platemaker-bin`` CLI option
    2. ``PLATEMAKER_BIN`` environment variable
    3. ``pytest.skip`` if neither is available

    :returns: A ``pathlib.Path`` pointing to the binary.
    :raises pytest.skip.Exception: When the binary cannot be located.
    """
    raw: str | None = request.config.getoption("--platemaker-bin")
    if raw is None:
        raw = os.environ.get("PLATEMAKER_BIN")
    if raw is None:
        pytest.skip(
            "platemaker binary not specified. "
            "Pass --platemaker-bin=<path> or set PLATEMAKER_BIN."
        )

    path = pathlib.Path(raw).resolve()
    if not path.exists():
        pytest.skip(f"platemaker binary not found at: {path}")

    return path


@pytest.fixture(scope="session")
def platemaker_version(platemaker_bin: pathlib.Path) -> str:
    """
    Return the version string reported by ``platemaker --version``.

    Useful as a quick sanity check that the binary is functional.

    :param platemaker_bin: Path to the CLI binary (injected by fixture).
    :returns: Version string, e.g. ``"0.1.0"``.
    """
    result = subprocess.run(
        [str(platemaker_bin), "--version"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    return result.stdout.strip()


# ---------------------------------------------------------------------------
# Per-invocation CLI timeout — safety net against the intermittent hang
# ---------------------------------------------------------------------------
#
# The CLI occasionally wedges and never exits (see docs/TODO.md). Without a
# per-call timeout, subprocess.run() waits forever and the whole suite freezes
# until CTest's 120 s limit fires as a bare "Timeout" with no clue which test.
#
# This injects a default timeout into every subprocess.run() that doesn't set one
# (helpers and tests all call `subprocess.run(...)`, so patching the module works).
# subprocess.run kills the child on TimeoutExpired, so a wedge becomes one clearly
# failed test and the suite keeps going.
#
# A normal call is ~0.16 s; a wedged one never recovers, so waiting long is pure
# waste. 15 s is ~90x headroom over normal yet stays well under CTest's 120 s suite
# limit, so the per-test failure always fires first with a clear culprit.
_CLI_CALL_TIMEOUT = 15.0


@pytest.fixture(autouse=True)
def _cli_call_timeout(monkeypatch: pytest.MonkeyPatch) -> None:
    real_run = subprocess.run

    def run_with_timeout(*args: object, **kwargs: object):
        kwargs.setdefault("timeout", _CLI_CALL_TIMEOUT)
        return real_run(*args, **kwargs)

    monkeypatch.setattr(subprocess, "run", run_with_timeout)


@pytest.fixture
def tmp_workspace(tmp_path: pathlib.Path) -> pathlib.Path:
    """
    Return a per-test temporary directory suitable for creating workspaces.

    The directory is automatically cleaned up after each test.

    :param tmp_path: pytest built-in temporary directory fixture.
    :returns: A ``pathlib.Path`` to an empty temporary directory.
    """
    return tmp_path
