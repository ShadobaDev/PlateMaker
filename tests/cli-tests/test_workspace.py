"""
Integration tests: ``platemaker workspace`` subcommands.

These tests verify that:
  * ``workspace create`` writes a valid JSON workspace file.
  * The JSON contains the correct canvas size, margins, and version fields.
  * ``workspace list-profiles`` prints profile information and exits 0.
"""

from __future__ import annotations

import json
import pathlib
import subprocess

import pytest

from helpers import create_workspace


# ---------------------------------------------------------------------------
# platemaker workspace create
# ---------------------------------------------------------------------------

def test_workspace_create_produces_json_file(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """
    ``workspace create`` must produce a valid JSON file at the given path.
    The file must contain a ``"version"`` field equal to 1.
    """
    out = tmp_workspace / "test.platemaker.json"
    create_workspace(platemaker_bin, out, canvas="800x2560", margins="0,0,0,0")

    assert out.exists(), "Output workspace file was not created"

    with out.open() as fh:
        data = json.load(fh)

    assert "version" in data, "'version' key missing from workspace JSON"
    assert data["version"] == 1, f"Expected version=1, got {data['version']}"


def test_workspace_create_sets_canvas_profile(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """
    The canvas size and margins stored in the workspace JSON must match the
    values passed on the command line.
    """
    out = tmp_workspace / "canvas_test.platemaker.json"
    create_workspace(
        platemaker_bin, out,
        canvas="1600x10240",
        margins="100,50,100,50",
        name="TestProfile",
    )

    with out.open() as fh:
        data = json.load(fh)

    profiles = data.get("canvasProfiles", [])
    assert len(profiles) >= 1, "No canvasProfiles in workspace JSON"

    profile = next((p for p in profiles if p["name"] == "TestProfile"), None)
    assert profile is not None, "Profile 'TestProfile' not found"
    assert profile["canvasSize"]["width"]  == 1600
    assert profile["canvasSize"]["height"] == 10240
    assert profile["margins"]["top"]    == 100
    assert profile["margins"]["right"]  == 50
    assert profile["margins"]["bottom"] == 100
    assert profile["margins"]["left"]   == 50


def test_workspace_create_sets_output_profile(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """
    The OutputProfile stored in the workspace must reflect --target-width and
    --slice-height.
    """
    out = tmp_workspace / "op_test.platemaker.json"
    create_workspace(
        platemaker_bin, out,
        canvas="800x2560",
        margins="0,0,0,0",
        target_width=900,
        slice_height=1500,
    )

    with out.open() as fh:
        data = json.load(fh)

    profiles = data.get("outputProfiles", [])
    assert len(profiles) >= 1

    op = profiles[0]
    assert op["targetWidth"]  == 900
    assert op["sliceHeight"] == 1500


def test_workspace_create_requires_canvas(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """Missing --canvas must result in exit code 1."""
    out = tmp_workspace / "bad.platemaker.json"
    result = subprocess.run(
        [str(platemaker_bin), "workspace", "create",
         "--margins", "0,0,0,0", "--output", str(out)],
        capture_output=True, text=True,
    )
    assert result.returncode == 1


# ---------------------------------------------------------------------------
# platemaker workspace list-profiles
# ---------------------------------------------------------------------------

def test_workspace_list_profiles(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """
    ``workspace list-profiles`` must exit 0 and print non-empty output
    containing the profile name.
    """
    ws = tmp_workspace / "list_test.platemaker.json"
    create_workspace(platemaker_bin, ws, name="Webtoon")

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "list-profiles",
         "--workspace", str(ws)],
        capture_output=True, text=True,
    )
    assert result.returncode == 0
    assert "Webtoon" in result.stdout


def test_workspace_list_profiles_missing_workspace(
    platemaker_bin: pathlib.Path,
    tmp_workspace: pathlib.Path,
) -> None:
    """Missing workspace file must exit with error code != 0."""
    result = subprocess.run(
        [str(platemaker_bin), "workspace", "list-profiles",
         "--workspace", str(tmp_workspace / "does_not_exist.json")],
        capture_output=True, text=True,
    )
    assert result.returncode != 0
