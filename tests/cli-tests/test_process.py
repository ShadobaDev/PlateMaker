"""
Integration tests: ``platemaker process`` subcommand.

These tests are placeholders for Stage 2.  They exercise the full
pipeline: scale → strip → slice → save.  Each test is skipped until
the corresponding CLI command is implemented.
"""

from __future__ import annotations

import json
import pathlib
import subprocess

import pytest


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _create_workspace(
    platemaker_bin: pathlib.Path,
    workspace_path: pathlib.Path,
) -> None:
    """Create a minimal Webtoon workspace at *workspace_path*."""
    subprocess.run(
        [
            str(platemaker_bin),
            "workspace", "create",
            "--canvas",  "800x10240",
            "--margins", "0,0,0,0",
            "--output",  str(workspace_path),
        ],
        capture_output=True,
        check=True,
    )


# ---------------------------------------------------------------------------
# platemaker process — standard pipeline
# ---------------------------------------------------------------------------

@pytest.mark.skip(reason="Stage 2 — not yet implemented")
def test_process_produces_output_slices(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """
    ``platemaker process`` should create numbered PNG slices in the output
    directory.  This test uses synthetic input files generated on the fly.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    # TODO Stage 2: generate synthetic input PNGs using PIL or vips-python.
    # For now, this is a documentation-only placeholder.

    ws_path = tmp_workspace / "project.platemaker.json"
    _create_workspace(platemaker_bin, ws_path)

    result = subprocess.run(
        [
            str(platemaker_bin), "process",
            "--workspace", str(ws_path),
            "--input",     str(input_dir),
            "--output",    str(output_dir),
            "--format",    "png",
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"

    slices = sorted(output_dir.glob("output_*.png"))
    assert len(slices) > 0, "No output slices were produced"


@pytest.mark.skip(reason="Stage 2 — not yet implemented")
def test_process_json_flag_outputs_valid_json(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """
    ``platemaker process --json`` should write a machine-readable JSON
    summary to stdout.
    """
    ws_path    = tmp_workspace / "project.platemaker.json"
    output_dir = tmp_workspace / "output"
    output_dir.mkdir()
    _create_workspace(platemaker_bin, ws_path)

    result = subprocess.run(
        [
            str(platemaker_bin), "process",
            "--workspace", str(ws_path),
            "--output",    str(output_dir),
            "--json",
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"

    summary = json.loads(result.stdout)
    assert "outputFiles" in summary or "sliceCount" in summary


@pytest.mark.skip(reason="Stage 2 — not yet implemented")
def test_process_start_index_flag(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """
    ``--start-index N`` should name the first output file ``output_N.png``.
    """
    ws_path    = tmp_workspace / "project.platemaker.json"
    output_dir = tmp_workspace / "output"
    output_dir.mkdir()
    _create_workspace(platemaker_bin, ws_path)

    subprocess.run(
        [
            str(platemaker_bin), "process",
            "--workspace",   str(ws_path),
            "--output",      str(output_dir),
            "--start-index", "5",
        ],
        capture_output=True,
        check=True,
    )

    first = output_dir / "output_005.png"
    assert first.exists(), f"Expected output_005.png but it does not exist"
