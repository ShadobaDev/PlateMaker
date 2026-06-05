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
    assert "up-to-date  : no" in r.stdout


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
    assert "up-to-date  : yes" in r.stdout


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

    r = _run(platemaker_bin, "process",
             "--workspace", str(ws),
             "--project", "Ch01")
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
