"""
Integration tests: ``platemaker workspace`` subcommands.

These tests are placeholders for Stage 2.  Each test is marked
``pytest.mark.skip`` until the corresponding CLI command is implemented.

Add real assertions as each CLI command is implemented in Stage 2.
"""

from __future__ import annotations

import json
import pathlib
import subprocess

import pytest


# ---------------------------------------------------------------------------
# platemaker workspace create
# ---------------------------------------------------------------------------

@pytest.mark.skip(reason="Stage 2 — not yet implemented")
def test_workspace_create_produces_json_file(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """
    ``platemaker workspace create`` should write a valid JSON workspace file
    to the path specified by ``--output``.
    """
    out = tmp_workspace / "test.platemaker.json"
    result = subprocess.run(
        [
            str(platemaker_bin),
            "workspace", "create",
            "--canvas", "1600x10240",
            "--margins", "100,100,100,100",
            "--output", str(out),
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert out.exists(), "Output file was not created"

    with out.open() as f:
        data = json.load(f)

    assert "version" in data
    assert data["version"] == 1


@pytest.mark.skip(reason="Stage 2 — not yet implemented")
def test_workspace_create_sets_canvas_profile(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """Canvas size and margins in the JSON must match the CLI arguments."""
    out = tmp_workspace / "canvas_test.platemaker.json"
    subprocess.run(
        [
            str(platemaker_bin),
            "workspace", "create",
            "--canvas", "800x10240",
            "--margins", "50,50,50,50",
            "--output", str(out),
        ],
        capture_output=True,
        check=True,
    )

    with out.open() as f:
        data = json.load(f)

    profiles = data.get("canvasProfiles", [])
    assert len(profiles) >= 1
    assert profiles[0]["canvasSize"]["width"]  == 800
    assert profiles[0]["canvasSize"]["height"] == 10240
    assert profiles[0]["margins"]["top"]       == 50


# ---------------------------------------------------------------------------
# platemaker workspace list-profiles
# ---------------------------------------------------------------------------

@pytest.mark.skip(reason="Stage 2 — not yet implemented")
def test_workspace_list_profiles(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """``platemaker workspace list-profiles`` should print the profile names."""
    ws = tmp_workspace / "list_test.platemaker.json"
    subprocess.run(
        [str(platemaker_bin), "workspace", "create",
         "--canvas", "1600x10240", "--margins", "100,100,100,100",
         "--output", str(ws)],
        capture_output=True, check=True,
    )

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "list-profiles",
         "--workspace", str(ws)],
        capture_output=True, text=True,
    )
    assert result.returncode == 0
    assert len(result.stdout.strip()) > 0
