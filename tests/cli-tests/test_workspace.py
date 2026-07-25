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
    ``workspace create`` must produce a valid JSON file with ``"version"`` == 2.

    A plain create stores **no** output profiles: presets are baked into the library and never
    written to a workspace, so a fresh file lists only what the user defined (nothing yet) — the
    Webtoon Standard preset is offered from the catalogue instead.
    """
    out = tmp_workspace / "test.platemaker.json"
    create_workspace(platemaker_bin, out)

    assert out.exists(), "Output workspace file was not created"

    with out.open() as fh:
        data = json.load(fh)

    assert data.get("version") == 2, f"Expected version=2, got {data.get('version')}"
    assert data.get("outputProfiles", []) == [], \
        "A fresh workspace must persist no output profiles (the preset lives in the catalogue)"


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


# ---------------------------------------------------------------------------
# workspace add-profile / mod-profile — --canvas-safe-area
# ---------------------------------------------------------------------------

def test_canvas_safe_area_stores_computed_canvas_size(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``--canvas-safe-area WxH --margins T,R,B,L`` stores the computed absolute
    canvas size (safe-area + margins) in the workspace JSON.
    """
    ws = tmp_workspace / "sa.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(
        platemaker_bin, ws,
        name="SA-Test",
        canvas_safe_area="1400x10040",
        margins="100,100,100,100",  # adds 200px in each axis
    )

    data = json.loads(ws.read_text())
    cp = next(p for p in data["canvasProfiles"] if p["name"] == "SA-Test")
    # Canvas = safe-area + margins: 1400+200=1600 × 10040+200=10240
    assert cp["canvasSize"]["width"]  == 1600,  f"width:  {cp['canvasSize']}"
    assert cp["canvasSize"]["height"] == 10240, f"height: {cp['canvasSize']}"
    # Margins are also persisted.
    assert cp["margins"]["top"]    == 100
    assert cp["margins"]["right"]  == 100
    assert cp["margins"]["bottom"] == 100
    assert cp["margins"]["left"]   == 100


def test_canvas_safe_area_matches_canvas_with_margins(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    A profile created with ``--canvas-safe-area`` must produce an identical
    canvas size to one created with ``--canvas`` equal to safe-area + margins.
    """
    ws = tmp_workspace / "sa_match.platemaker.json"
    create_workspace(platemaker_bin, ws)

    # Profile A: explicit absolute canvas.
    add_profile(
        platemaker_bin, ws,
        name="A",
        canvas="1600x10240",
        margins="100,100,100,100",
    )
    # Profile B: computed from safe-area.
    add_profile(
        platemaker_bin, ws,
        name="B",
        canvas_safe_area="1400x10040",  # + 200px each axis = 1600×10240
        margins="100,100,100,100",
    )

    data = json.loads(ws.read_text())
    by_name = {p["name"]: p for p in data["canvasProfiles"]}
    assert by_name["A"]["canvasSize"] == by_name["B"]["canvasSize"], (
        f"A={by_name['A']['canvasSize']}  B={by_name['B']['canvasSize']}"
    )


def test_canvas_safe_area_and_canvas_are_mutually_exclusive_add(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    Providing both ``--canvas`` and ``--canvas-safe-area`` to add-profile
    must exit with code 1 and an informative error message.
    """
    ws = tmp_workspace / "sa_mutex.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [
            str(platemaker_bin), "workspace", "add-profile",
            "--workspace",        str(ws),
            "--name",             "Conflict",
            "--canvas",           "1600x10240",
            "--canvas-safe-area", "1400x10040",
            "--margins",          "100,100,100,100",
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 1
    assert "mutually exclusive" in result.stderr.lower(), (
        f"Expected 'mutually exclusive' in stderr.\nstderr: {result.stderr}"
    )


def test_canvas_safe_area_and_canvas_are_mutually_exclusive_mod(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    Providing both ``--canvas`` and ``--canvas-safe-area`` to mod-profile
    must exit with code 1.
    """
    ws = tmp_workspace / "sa_mutex_mod.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(platemaker_bin, ws, name="P", canvas="800x2560", margins="0,0,0,0")

    result = subprocess.run(
        [
            str(platemaker_bin), "workspace", "mod-profile",
            "--workspace",        str(ws),
            "--name",             "P",
            "--canvas",           "1600x10240",
            "--canvas-safe-area", "1400x10040",
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 1
    assert "mutually exclusive" in result.stderr.lower()


def test_canvas_safe_area_requires_either_canvas_or_safe_area(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``workspace add-profile`` without ``--canvas`` or ``--canvas-safe-area``
    must exit with code 1.
    """
    ws = tmp_workspace / "sa_required.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [
            str(platemaker_bin), "workspace", "add-profile",
            "--workspace", str(ws),
            "--name",      "NoCanvas",
            "--margins",   "100,100,100,100",
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 1


def test_mod_profile_canvas_safe_area_uses_existing_margins(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``mod-profile --canvas-safe-area`` without ``--margins`` uses the
    profile's existing margins to compute the new absolute canvas size.
    """
    ws = tmp_workspace / "sa_mod_existing.platemaker.json"
    create_workspace(platemaker_bin, ws)
    # Initial profile: margins 50,50,50,50.
    add_profile(
        platemaker_bin, ws,
        name="UseExisting",
        canvas="900x2660",   # 800 + 100 margins × 2 = 900 × 2560 + 100×2 = 2760... 
        # Let's be precise: safe-area 800×2560, margins 50 each side
        # canvas = 800 + 50 + 50 = 900, height = 2560 + 50 + 50 = 2660
        margins="50,50,50,50",
    )

    # Now resize safe-area to 1000×3000 keeping margins=50.
    subprocess.run(
        [
            str(platemaker_bin), "workspace", "mod-profile",
            "--workspace",        str(ws),
            "--name",             "UseExisting",
            "--canvas-safe-area", "1000x3000",
            # No --margins → reuse existing 50,50,50,50
        ],
        capture_output=True,
        check=True,
    )

    data = json.loads(ws.read_text())
    cp = next(p for p in data["canvasProfiles"] if p["name"] == "UseExisting")
    # canvas = 1000 + 50 + 50 = 1100, 3000 + 50 + 50 = 3100
    assert cp["canvasSize"]["width"]  == 1100, f"width:  {cp['canvasSize']}"
    assert cp["canvasSize"]["height"] == 3100, f"height: {cp['canvasSize']}"
    # Margins should remain unchanged.
    assert cp["margins"]["top"]    == 50
    assert cp["margins"]["right"]  == 50
    assert cp["margins"]["bottom"] == 50
    assert cp["margins"]["left"]   == 50


def test_mod_profile_canvas_safe_area_with_new_margins(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    ``mod-profile --canvas-safe-area --margins`` updates both the canvas size
    and the margins in a single command.
    """
    ws = tmp_workspace / "sa_mod_new.platemaker.json"
    create_workspace(platemaker_bin, ws)
    add_profile(
        platemaker_bin, ws,
        name="UpdateBoth",
        canvas="800x2560",
        margins="0,0,0,0",
    )

    subprocess.run(
        [
            str(platemaker_bin), "workspace", "mod-profile",
            "--workspace",        str(ws),
            "--name",             "UpdateBoth",
            "--canvas-safe-area", "700x2360",
            "--margins",          "100,50,100,50",  # top,right,bottom,left
        ],
        capture_output=True,
        check=True,
    )

    data = json.loads(ws.read_text())
    cp = next(p for p in data["canvasProfiles"] if p["name"] == "UpdateBoth")
    # canvas = 700 + 50 + 50 = 800, 2360 + 100 + 100 = 2560
    assert cp["canvasSize"]["width"]  == 800,  f"width:  {cp['canvasSize']}"
    assert cp["canvasSize"]["height"] == 2560, f"height: {cp['canvasSize']}"
    assert cp["margins"]["top"]    == 100
    assert cp["margins"]["right"]  == 50
    assert cp["margins"]["bottom"] == 100
    assert cp["margins"]["left"]   == 50


# ---------------------------------------------------------------------------
# workspace output-profile family (id-selected; presets are read-only)
# ---------------------------------------------------------------------------

_PRESET_ID = "op-preset-webtoon-standard"


def test_list_presets_shows_the_webtoon_preset(platemaker_bin: pathlib.Path) -> None:
    result = subprocess.run(
        [str(platemaker_bin), "workspace", "list-presets"],
        capture_output=True, text=True,
    )
    assert result.returncode == 0
    assert _PRESET_ID in result.stdout, result.stdout


def test_add_output_profile_from_preset_persists_a_user_copy(
    platemaker_bin: pathlib.Path, tmp_workspace: pathlib.Path,
) -> None:
    """A copy made from a preset is stored as the user's own profile (fresh id, not a preset id)."""
    ws = tmp_workspace / "op.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "add-output-profile",
         "--workspace", str(ws), "--name", "My Webtoon", "--from-preset", _PRESET_ID],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stderr

    profiles = json.loads(ws.read_text()).get("outputProfiles", [])
    assert len(profiles) == 1, "the copy must be persisted (not swallowed by the migration)"
    assert profiles[0]["name"] == "My Webtoon"
    assert not profiles[0]["id"].startswith("op-preset-"), "a copy must not carry a preset id"


def test_add_output_profile_from_scratch_stores_settings(
    platemaker_bin: pathlib.Path, tmp_workspace: pathlib.Path,
) -> None:
    ws = tmp_workspace / "op2.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "add-output-profile",
         "--workspace", str(ws), "--name", "Wide",
         "--target-width", "1080", "--slice-height", "1920", "--format", "jpg"],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stderr

    op = json.loads(ws.read_text())["outputProfiles"][0]
    assert op["targetWidth"] == 1080 and op["sliceHeight"] == 1920


def test_mod_output_profile_on_a_preset_id_is_rejected(
    platemaker_bin: pathlib.Path, tmp_workspace: pathlib.Path,
) -> None:
    ws = tmp_workspace / "op3.platemaker.json"
    create_workspace(platemaker_bin, ws)

    result = subprocess.run(
        [str(platemaker_bin), "workspace", "mod-output-profile",
         "--workspace", str(ws), "--output-profile", _PRESET_ID, "--name", "Nope"],
        capture_output=True, text=True,
    )
    assert result.returncode != 0, "modifying a preset must be refused by the CLI"
    assert "preset" in result.stderr.lower()
