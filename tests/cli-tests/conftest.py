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

import os
import pathlib
import subprocess
import sys

import pytest


# ---------------------------------------------------------------------------
# Custom command-line option
# ---------------------------------------------------------------------------

def pytest_addoption(parser: pytest.Parser) -> None:
    """Register the --platemaker-bin option."""
    parser.addoption(
        "--platemaker-bin",
        action="store",
        default=None,
        metavar="PATH",
        help="Path to the compiled platemaker CLI binary. "
             "Falls back to the PLATEMAKER_BIN environment variable.",
    )


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


@pytest.fixture
def tmp_workspace(tmp_path: pathlib.Path) -> pathlib.Path:
    """
    Return a per-test temporary directory suitable for creating workspaces.

    The directory is automatically cleaned up after each test.

    :param tmp_path: pytest built-in temporary directory fixture.
    :returns: A ``pathlib.Path`` to an empty temporary directory.
    """
    return tmp_path
