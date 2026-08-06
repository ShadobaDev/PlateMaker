"""
Integration tests: ``platemaker project`` and ``workspace list-projects``.

Covers:
  * workspace list-projects  — lists projects from workspace.
  * project create  — creates a project entry.
  * project mod     — modifies name / input directory.
  * project rm      — removes a project.
  * project status  — shows per-file status (PENDING / PROCESSED).
  * process --project NAME — processes a named workspace project.
"""

from __future__ import annotations

import pathlib
import re
import subprocess

import pytest

from helpers import create_workspace, make_solid_png


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _run(platemaker_bin: pathlib.Path, *args: str) -> subprocess.CompletedProcess:
    """Run the CLI binary with given arguments; always capture output."""
    return subprocess.run(
        [str(platemaker_bin)] + list(args),
        capture_output=True,
        text=True,
    )


# ---------------------------------------------------------------------------
# workspace list-projects
# ---------------------------------------------------------------------------

def test_workspace_list_projects_empty(tmp_path, platemaker_bin):
    """list-projects on a fresh workspace reports (none)."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    r = _run(platemaker_bin, "workspace", "list-projects",
             "--workspace", str(ws))
    assert r.returncode == 0
    assert "none" in r.stdout.lower()


def test_workspace_list_projects_missing_workspace(tmp_path, platemaker_bin):
    """list-projects without --workspace returns exit code 1."""
    r = _run(platemaker_bin, "workspace", "list-projects")
    assert r.returncode == 1


# ---------------------------------------------------------------------------
# project create
# ---------------------------------------------------------------------------

def test_project_create_minimal(tmp_path, platemaker_bin):
    """Create a project by name only (no --input / --output)."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)

    r = _run(platemaker_bin, "project", "create",
             "--workspace", str(ws),
             "--name", "Chapter 1")
    assert r.returncode == 0, r.stderr
    assert "Chapter 1" in r.stderr

    lr = _run(platemaker_bin, "workspace", "list-projects",
              "--workspace", str(ws))
    assert "Chapter 1" in lr.stdout


def test_project_create_with_input(tmp_path, platemaker_bin):
    """project create --input DIR scans images immediately."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    for i in range(3):
        make_solid_png(pages / f"page_{i:03d}.png", 800, 1200)

    r = _run(platemaker_bin, "project", "create",
             "--workspace", str(ws),
             "--name", "Ch01",
             "--input", str(pages),
             "--output", str(tmp_path / "out"))
    assert r.returncode == 0, r.stderr
    assert "3 input file(s)" in r.stderr

    lr = _run(platemaker_bin, "workspace", "list-projects",
              "--workspace", str(ws))
    assert "Ch01" in lr.stdout
    assert "input files : 3" in lr.stdout


def test_project_create_duplicate_name(tmp_path, platemaker_bin):
    """Creating a project with a duplicate name returns exit code 1."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws), "--name", "My Chapter")

    r = _run(platemaker_bin, "project", "create",
             "--workspace", str(ws), "--name", "My Chapter")
    assert r.returncode == 1
    assert "already exists" in r.stderr


def test_project_create_missing_workspace(tmp_path, platemaker_bin):
    r = _run(platemaker_bin, "project", "create", "--name", "X")
    assert r.returncode == 1


def test_project_create_missing_name(tmp_path, platemaker_bin):
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    r = _run(platemaker_bin, "project", "create", "--workspace", str(ws))
    assert r.returncode == 1


# ---------------------------------------------------------------------------
# project mod
# ---------------------------------------------------------------------------

def test_project_mod_rename(tmp_path, platemaker_bin):
    """Renaming a project with --new-name works."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws), "--name", "OldName")

    r = _run(platemaker_bin, "project", "mod",
             "--workspace", str(ws),
             "--name", "OldName",
             "--new-name", "NewName")
    assert r.returncode == 0, r.stderr

    lr = _run(platemaker_bin, "workspace", "list-projects",
              "--workspace", str(ws))
    assert "NewName" in lr.stdout
    assert "OldName" not in lr.stdout


def test_project_mod_update_input(tmp_path, platemaker_bin):
    """Updating --input on an existing project rescans the directory."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    for i in range(2):
        make_solid_png(pages / f"page_{i:03d}.png", 800, 1200)

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws), "--name", "Ch01")

    r = _run(platemaker_bin, "project", "mod",
             "--workspace", str(ws),
             "--name", "Ch01",
             "--input", str(pages))
    assert r.returncode == 0, r.stderr
    assert "2 file(s)" in r.stderr


def test_project_mod_not_found(tmp_path, platemaker_bin):
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    r = _run(platemaker_bin, "project", "mod",
             "--workspace", str(ws), "--name", "NoSuch")
    assert r.returncode == 1


# ---------------------------------------------------------------------------
# project duplicate
# ---------------------------------------------------------------------------

def test_project_duplicate_basic(tmp_path, platemaker_bin):
    """Duplicate seeds a second, distinct project from the source's inputs + profile."""
    import json

    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    for i in range(3):
        make_solid_png(pages / f"page_{i:03d}.png", 800, 1200)

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws), "--name", "Ch01",
         "--input", str(pages), "--output", str(tmp_path / "out"))

    r = _run(platemaker_bin, "project", "duplicate",
             "--workspace", str(ws),
             "--name", "Ch01",
             "--new-name", "Ch01 PubX")
    assert r.returncode == 0, r.stderr
    assert "3 input file(s)" in r.stderr

    data = json.loads(ws.read_text())
    projects = {p["name"]: p for p in data["projectItems"]}
    assert set(projects) == {"Ch01", "Ch01 PubX"}
    src, dup = projects["Ch01"], projects["Ch01 PubX"]

    # Fresh, distinct project uid.
    assert dup["uid"] != src["uid"]
    assert dup["uid"].startswith("proj-")

    # Same input files (paths + order) but fresh input uids and Pending status.
    assert [i["filePath"] for i in dup["inputFiles"]] == \
           [i["filePath"] for i in src["inputFiles"]]
    assert {i["uid"] for i in dup["inputFiles"]}.isdisjoint(
           {i["uid"] for i in src["inputFiles"]})
    assert all(i["status"] == "Pending" for i in dup["inputFiles"])

    # The copy carries no render products and no output directory (must not clobber the source).
    assert dup["outputDirectory"] == ""
    assert dup["outputFiles"] == []


def test_project_duplicate_drops_rendered_state(tmp_path, platemaker_bin):
    """Even after the source is processed, the duplicate starts fresh (Pending, no outputs)."""
    import json

    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    make_solid_png(pages / "page_001.png", 800, 1200)

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws), "--name", "Ch01",
         "--input", str(pages), "--output", str(tmp_path / "out"))
    r_proc = _run(platemaker_bin, "process",
                  "--workspace", str(ws), "--project", "Ch01")
    assert r_proc.returncode == 0, r_proc.stderr

    _run(platemaker_bin, "project", "duplicate",
         "--workspace", str(ws), "--name", "Ch01", "--new-name", "Ch01 Copy")

    data = json.loads(ws.read_text())
    projects = {p["name"]: p for p in data["projectItems"]}
    src, dup = projects["Ch01"], projects["Ch01 Copy"]

    # Source is fully rendered...
    assert src["outputDirectory"] != ""
    assert len(src["outputFiles"]) > 0
    assert all(i["sha256"] != "" for i in src["inputFiles"])
    # ...the duplicate has none of that.
    assert dup["outputDirectory"] == ""
    assert dup["outputFiles"] == []
    assert all(i["status"] == "Pending" for i in dup["inputFiles"])
    assert all(i["sha256"] == "" for i in dup["inputFiles"])


def test_project_duplicate_with_output(tmp_path, platemaker_bin):
    """--output points the copy at its own output directory immediately."""
    import json

    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws), "--name", "Ch01")

    out = tmp_path / "pubx-out"
    r = _run(platemaker_bin, "project", "duplicate",
             "--workspace", str(ws), "--name", "Ch01",
             "--new-name", "Ch01 PubX", "--output", str(out))
    assert r.returncode == 0, r.stderr

    data = json.loads(ws.read_text())
    dup = next(p for p in data["projectItems"] if p["name"] == "Ch01 PubX")
    assert dup["outputDirectory"] == str(out)


def test_project_duplicate_source_not_found(tmp_path, platemaker_bin):
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    r = _run(platemaker_bin, "project", "duplicate",
             "--workspace", str(ws), "--name", "NoSuch", "--new-name", "X")
    assert r.returncode == 1
    assert "not found" in r.stderr.lower()


def test_project_duplicate_existing_new_name(tmp_path, platemaker_bin):
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    _run(platemaker_bin, "project", "create", "--workspace", str(ws), "--name", "Ch01")
    _run(platemaker_bin, "project", "create", "--workspace", str(ws), "--name", "Ch02")

    r = _run(platemaker_bin, "project", "duplicate",
             "--workspace", str(ws), "--name", "Ch01", "--new-name", "Ch02")
    assert r.returncode == 1
    assert "already exists" in r.stderr


def test_project_duplicate_missing_new_name(tmp_path, platemaker_bin):
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    _run(platemaker_bin, "project", "create", "--workspace", str(ws), "--name", "Ch01")

    r = _run(platemaker_bin, "project", "duplicate",
             "--workspace", str(ws), "--name", "Ch01")
    assert r.returncode == 1


# ---------------------------------------------------------------------------
# project rm
# ---------------------------------------------------------------------------

def test_project_rm(tmp_path, platemaker_bin):
    """Removing an existing project removes it from list-projects."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws), "--name", "ToRemove")

    r = _run(platemaker_bin, "project", "rm",
             "--workspace", str(ws), "--name", "ToRemove")
    assert r.returncode == 0

    lr = _run(platemaker_bin, "workspace", "list-projects",
              "--workspace", str(ws))
    assert "ToRemove" not in lr.stdout


def test_project_rm_not_found(tmp_path, platemaker_bin):
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    r = _run(platemaker_bin, "project", "rm",
             "--workspace", str(ws), "--name", "NoSuch")
    assert r.returncode == 1


# ---------------------------------------------------------------------------
# project status
# ---------------------------------------------------------------------------

def test_project_status_pending(tmp_path, platemaker_bin):
    """A freshly created project with files shows PENDING status."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    make_solid_png(pages / "page_001.png", 800, 1200)

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws),
         "--name", "Ch01",
         "--input", str(pages))

    r = _run(platemaker_bin, "project", "status",
             "--workspace", str(ws), "--name", "Ch01")
    assert r.returncode == 0
    assert "PENDING" in r.stdout
    assert re.search(r"up-to-date\s*:\s*no", r.stdout)


def test_project_status_processed(tmp_path, platemaker_bin):
    """After processing, project status shows PROCESSED and up-to-date."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    make_solid_png(pages / "page_001.png", 800, 1200)
    out_dir = tmp_path / "out"

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws),
         "--name", "Ch01",
         "--input", str(pages),
         "--output", str(out_dir))

    r_proc = _run(platemaker_bin, "process",
                  "--workspace", str(ws),
                  "--project", "Ch01")
    assert r_proc.returncode == 0, r_proc.stderr

    r = _run(platemaker_bin, "project", "status",
             "--workspace", str(ws), "--name", "Ch01")
    assert r.returncode == 0
    assert "PROCESSED" in r.stdout
    assert re.search(r"up-to-date\s*:\s*yes", r.stdout)


def test_project_status_not_found(tmp_path, platemaker_bin):
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    r = _run(platemaker_bin, "project", "status",
             "--workspace", str(ws), "--name", "NoSuch")
    assert r.returncode == 1


# ---------------------------------------------------------------------------
# process --project NAME
# ---------------------------------------------------------------------------

def test_process_with_project_flag(tmp_path, platemaker_bin):
    """process --project NAME processes an existing workspace project."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    for i in range(2):
        make_solid_png(pages / f"page_{i:03d}.png", 800, 1200)
    out_dir = tmp_path / "out"

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws),
         "--name", "Ch01",
         "--input", str(pages),
         "--output", str(out_dir))

    # Pin PNG: the ad-hoc profile defaults to JPEG (Webtoon Standard preset), but
    # this test asserts on output_*.png and only cares that slices were produced.
    r = _run(platemaker_bin, "process",
             "--workspace", str(ws),
             "--project", "Ch01",
             "--format", "png")
    assert r.returncode == 0, r.stderr
    output_files = list(out_dir.glob("output_*.png"))
    assert len(output_files) > 0


def test_process_project_not_found(tmp_path, platemaker_bin):
    """process --project NoSuch returns exit code 1."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    r = _run(platemaker_bin, "process",
             "--workspace", str(ws),
             "--project", "NoSuch")
    assert r.returncode == 1
    assert "not found" in r.stderr.lower()


def test_process_project_incremental_skips(tmp_path, platemaker_bin):
    """process --project is skipped on second run when files are unchanged."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    make_solid_png(pages / "page_001.png", 800, 1200)
    out_dir = tmp_path / "out"

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws),
         "--name", "Ch01",
         "--input", str(pages),
         "--output", str(out_dir))

    r1 = _run(platemaker_bin, "process",
              "--workspace", str(ws), "--project", "Ch01")
    assert r1.returncode == 0

    r2 = _run(platemaker_bin, "process",
              "--workspace", str(ws), "--project", "Ch01")
    assert r2.returncode == 0
    assert "nothing to do" in r2.stderr.lower()


def test_process_multi_input_per_output_provenance(tmp_path, platemaker_bin):
    """
    Verify that when a slice spans two input files, both files appear in
    the output's sourceMap and the crossing file lists both outputs in
    its contributesTo field.

    Setup:
      page_001.png  — 800 × 1200 px
      page_002.png  — 800 × 1200 px
      sliceHeight = 1280 px

    Virtual strip: 2400 px total
      slice_0 (output_001): pixels 0–1279 → 1200 from page_001 + 80 from page_002
      slice_1 (output_002): pixels 1280–2399 → 1120 from page_002 (plus tail kept)

    Expectations:
      • output_001 sourceMap has 2 segments (page_001 AND page_002)
      • page_002.contributesTo contains BOTH output_001 and output_002
      • page_001.contributesTo contains only output_001
    """
    import json

    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, slice_height=1280)
    pages = tmp_path / "pages"
    pages.mkdir()
    make_solid_png(pages / "page_001.png", 800, 1200)
    make_solid_png(pages / "page_002.png", 800, 1200)
    out_dir = tmp_path / "out"

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws),
         "--name", "Ch01",
         "--input", str(pages),
         "--output", str(out_dir))

    # Pin PNG (default profile is the JPEG Webtoon Standard preset); this test
    # asserts on output_001.png and verifies source provenance, not the format.
    r = _run(platemaker_bin, "process",
             "--workspace", str(ws), "--project", "Ch01",
             "--format", "png")
    assert r.returncode == 0, r.stderr

    # --- Parse saved workspace JSON and inspect provenance data ---
    data = json.loads(ws.read_text())
    proj = data["projectItems"][0]

    # Build lookup: filePath → contributesTo  (from saved JSON)
    contrib_map = {
        inf["filePath"]: inf["contributesTo"]
        for inf in proj["inputFiles"]
    }
    # Build lookup: fileName → sourceMap  (from saved JSON)
    source_map = {
        outf["fileName"]: outf["sourceMap"]
        for outf in proj["outputFiles"]
    }

    # Identify which path is page_001 / page_002 (sorted by filename)
    sorted_paths = sorted(contrib_map.keys())
    path_001, path_002 = sorted_paths[0], sorted_paths[1]

    # output_001 must draw from BOTH page_001 and page_002
    assert "output_001.png" in source_map, "output_001.png not found in saved outputFiles"
    sm_001 = source_map["output_001.png"]
    source_paths_001 = {seg["sourceFilePath"] for seg in sm_001}
    assert path_001 in source_paths_001, (
        f"page_001 not in output_001 sourceMap: {source_paths_001}")
    assert path_002 in source_paths_001, (
        f"page_002 not in output_001 sourceMap (expected boundary-crossing): {source_paths_001}")

    # page_001 contributes only to output_001 (it fits inside one slice)
    assert contrib_map[path_001] == ["output_001.png"], (
        f"page_001.contributesTo expected [output_001.png], got {contrib_map[path_001]}")

    # page_002 contributes to BOTH output_001 (boundary) and output_002 (remainder)
    assert "output_001.png" in contrib_map[path_002], (
        f"page_002 should contribute to output_001 (boundary crossing)")
    assert "output_002.png" in contrib_map[path_002], (
        f"page_002 should contribute to output_002 (remainder)")


def test_process_project_reprocesses_after_mod(tmp_path, platemaker_bin):
    """After project mod --input (new scan), next process re-runs."""
    ws = tmp_path / "project.platemaker.json"
    create_workspace(platemaker_bin, ws)
    pages = tmp_path / "pages"
    pages.mkdir()
    make_solid_png(pages / "page_001.png", 800, 1200)
    out_dir = tmp_path / "out"

    _run(platemaker_bin, "project", "create",
         "--workspace", str(ws),
         "--name", "Ch01",
         "--input", str(pages),
         "--output", str(out_dir))
    _run(platemaker_bin, "process",
         "--workspace", str(ws), "--project", "Ch01")

    # Add another image and re-scan via project mod.
    make_solid_png(pages / "page_002.png", 800, 1200)
    _run(platemaker_bin, "project", "mod",
         "--workspace", str(ws),
         "--name", "Ch01",
         "--input", str(pages))

    r = _run(platemaker_bin, "process",
             "--workspace", str(ws), "--project", "Ch01")
    assert r.returncode == 0
    assert "nothing to do" not in r.stderr.lower()
