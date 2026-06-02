"""
Integration tests: ``platemaker workspace`` subcommands.

Covers:
  * workspace create  — produces a valid JSON workspace with a default output profile.
  * workspace add-profile — adds a CanvasProfile entry to the workspace.
  * workspace mod-profile — modifies an existing canvas profile.
  * workspace rm-profile  — removes a canvas profile.
  * workspace list-profiles — prints profile information, exits 0.
"""

from __future__ import annotations

import json
import pathlib
import subprocess

import pytest

from helpers import create_workspace, add_profile


# ---------------------------------------------------------------------------
# workspace create
# ---------------------------------------------------------------------------

def test_workspace_create_produces_json_file(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``workspace create`` must produce a valid JSON file containing a
    ``"version"`` field equal to 1 and at least one entry in
    ``"outputProfiles"``.
    """
    out = tmp_workspace / "test.platemaker.json"
    create_workspace(platemaker_bin, out)

    assert out.exists(), "Output workspace file was not created"

    with out.open() as fh:
        data = json.load(fh)

    assert data.get("version") == 1, f"Expected version=1, got {data.get('version')}"
    assert len(data.get("outputProfiles", [])) >= 1, \
        "Expected at least one outputProfile in a fresh workspace"


def test_workspace_create_sets_default_output_profile(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``workspace create`` with custom --target-width / --slice-height must store
    those values in the default output profile.
    """
    out = tmp_workspace / "op_test.platemaker.json"
    create_workspace(platemaker_bin, out, target_width=900, slice_height=1500)

    with out.open() as fh:
        data = json.load(fh)

    profiles = data.get("outputProfiles", [])
    assert len(profiles) >= 1

    op = profiles[0]
    assert op["targetWidth"] == 900,  f"Expected targetWidth=900,  got {op['targetWidth']}"
    assert op["sliceHeight"] == 1500, f"Expected sliceHeight=1500, got {op['sliceHeight']}"


def test_workspace_create_no_canvas_profiles_by_default(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    A freshly created workspace must have an empty ``canvasProfiles`` list.
    Canvas profiles are added explicitly via ``workspace add-profile``.
    """
    out = tmp_workspace / "empty.platemaker.json"
    create_workspace(platemaker_bin, out)

    with out.open() as fh:
        data = json.load(fh)

    assert data.get("canvasProfiles", []) == [], \
        "Fresh workspace should have no canvas profiles"


# ---------------------------------------------------------------------------
# workspace add-profile
# ---------------------------------------------------------------------------

def test_workspace_add_profile_sets_canvas(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``workspace add-profile`` must persist the canvas size and margins in
    the ``canvasProfiles`` list of the workspace JSON.
    """
    ws = tmp_workspace / "canvas_test.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(platemaker_bin, ws,
                name="TestProfile",
                canvas="1600x10240",
                margins="100,50,100,50")

    with ws.open() as fh:
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


def test_workspace_add_profile_multiple_profiles(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    Multiple calls to ``add-profile`` must accumulate all profiles.
    """
    ws = tmp_workspace / "multi.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(platemaker_bin, ws, name="Small",  canvas="800x2560",  margins="0,0,0,0")
    add_profile(platemaker_bin, ws, name="Large", canvas="1600x5120", margins="100,100,100,100")

    with ws.open() as fh:
        data = json.load(fh)

    names = {p["name"] for p in data.get("canvasProfiles", [])}
    assert "Small"  in names, "Profile 'Small' missing"
    assert "Large" in names, "Profile 'Large' missing"


def test_workspace_add_profile_rejects_duplicate_name(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    Adding a profile whose name already exists must exit with code 1.
    """
    ws = tmp_workspace / "dup.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(platemaker_bin, ws, name="Dup", canvas="800x2560", margins="0,0,0,0")

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "add-profile",
         "--workspace", str(ws),
         "--name", "Dup", "--canvas", "800x2560", "--margins", "0,0,0,0"],
        capture_output=True, text=True,
    )
    assert result.returncode == 1, \
        f"Expected exit 1 for duplicate profile name, got {result.returncode}"


def test_workspace_add_profile_requires_canvas(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """Missing --canvas must result in exit code 1."""
    ws = tmp_workspace / "bad.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "add-profile",
         "--workspace", str(ws),
         "--name", "TestProfile",
         "--margins", "0,0,0,0"],  # --canvas intentionally omitted
        capture_output=True, text=True,
    )
    assert result.returncode == 1, \
        f"Expected exit 1 when --canvas is missing, got {result.returncode}"


# ---------------------------------------------------------------------------
# workspace mod-profile
# ---------------------------------------------------------------------------

def test_workspace_mod_profile_updates_margins(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``workspace mod-profile --margins`` must update the stored margins without
    touching the canvas size.
    """
    ws = tmp_workspace / "mod.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(platemaker_bin, ws, name="Mod", canvas="1600x10240", margins="0,0,0,0")

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "mod-profile",
         "--workspace", str(ws),
         "--name", "Mod",
         "--margins", "50,50,50,50"],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"mod-profile failed:\n{result.stderr}"

    with ws.open() as fh:
        data = json.load(fh)

    profile = next((p for p in data["canvasProfiles"] if p["name"] == "Mod"), None)
    assert profile is not None
    assert profile["margins"]["top"] == 50
    # Canvas size must be unchanged.
    assert profile["canvasSize"]["width"] == 1600


def test_workspace_mod_profile_nonexistent_name_exits_1(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """Modifying a non-existent profile must exit with code 1."""
    ws = tmp_workspace / "no_profile.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "mod-profile",
         "--workspace", str(ws),
         "--name", "DoesNotExist",
         "--margins", "10,10,10,10"],
        capture_output=True, text=True,
    )
    assert result.returncode == 1


# ---------------------------------------------------------------------------
# workspace rm-profile
# ---------------------------------------------------------------------------

def test_workspace_rm_profile_removes_profile(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``workspace rm-profile`` must remove the named profile from the workspace.
    """
    ws = tmp_workspace / "rm.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(platemaker_bin, ws, name="ToRemove", canvas="800x2560", margins="0,0,0,0")
    add_profile(platemaker_bin, ws, name="KeepMe",   canvas="1600x5120", margins="0,0,0,0")

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "rm-profile",
         "--workspace", str(ws),
         "--name", "ToRemove"],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"rm-profile failed:\n{result.stderr}"

    with ws.open() as fh:
        data = json.load(fh)

    names = {p["name"] for p in data.get("canvasProfiles", [])}
    assert "ToRemove" not in names, "Profile 'ToRemove' was not removed"
    assert "KeepMe"   in names,     "Profile 'KeepMe' was accidentally removed"


def test_workspace_rm_profile_nonexistent_exits_1(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """Removing a non-existent profile must exit with code 1."""
    ws = tmp_workspace / "no_rm.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "rm-profile",
         "--workspace", str(ws),
         "--name", "Ghost"],
        capture_output=True, text=True,
    )
    assert result.returncode == 1


# ---------------------------------------------------------------------------
# workspace list-profiles
# ---------------------------------------------------------------------------

def test_workspace_list_profiles(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``workspace list-profiles`` must exit 0 and print the profile name in
    its output.
    """
    ws = tmp_workspace / "list_test.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(platemaker_bin, ws, name="Webtoon", canvas="800x2560", margins="0,0,0,0")

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "list-profiles",
         "--workspace", str(ws)],
        capture_output=True, text=True,
    )
    assert result.returncode == 0
    assert "Webtoon" in result.stdout


def test_workspace_list_profiles_empty_workspace(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``workspace list-profiles`` on a workspace with no canvas profiles must
    exit 0 and print a message indicating no profiles exist.
    """
    ws = tmp_workspace / "empty_list.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "list-profiles",
         "--workspace", str(ws)],
        capture_output=True, text=True,
    )
    assert result.returncode == 0
    assert "none" in result.stdout.lower() or "no" in result.stdout.lower()


def test_workspace_list_profiles_missing_workspace(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """Missing workspace file must exit with error code != 0."""
    result = subprocess.run(
        [str(platemaker_bin), "workspace", "list-profiles",
         "--workspace", str(tmp_workspace / "does_not_exist.json")],
        capture_output=True, text=True,
    )
    assert result.returncode != 0
