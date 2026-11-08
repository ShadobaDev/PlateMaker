"""
Integration test: every CLI command that reads or writes a location accepts a non-ASCII one.

On Windows a narrow ``std::filesystem::path`` is read in the ANSI code page, so a UTF-8 path either
names a folder that does not exist or comes back out as ANSI bytes in the workspace. The folder names
below hold a character that the usual code pages have (``ó``) and one that none of them has (``漫``).
"""

from __future__ import annotations

import json
import pathlib
import subprocess

from helpers import add_profile, create_workspace, make_solid_png

ROOT_NAME   = "Mój 漫画"
FIRST_PAGES = "strony ó"
PAGES       = "rozdział 漫"
PAGE_COUNT  = 2


def _run(platemaker_bin: pathlib.Path, *args: str) -> subprocess.CompletedProcess:
    result = subprocess.run([str(platemaker_bin), *args], capture_output=True, text=True)
    assert result.returncode == 0, f"{args[:2]} failed (rc={result.returncode}):\n{result.stderr}"
    return result


def _pages(folder: pathlib.Path) -> pathlib.Path:
    folder.mkdir()
    for i in range(PAGE_COUNT):
        make_solid_png(folder / f"page_{i:03d}.png", 800, 1200)
    return folder


def test_every_location_may_be_non_ascii(tmp_path, platemaker_bin):
    root = tmp_path / ROOT_NAME
    root.mkdir()
    ws = root / "projekt.platemaker.json"
    source = root / "źródło.platemaker.json"
    out = root / "wynik"
    template = root / "szablon.png"

    create_workspace(platemaker_bin, ws)
    create_workspace(platemaker_bin, source)
    add_profile(platemaker_bin, source, name="Profil", canvas="1600x10240", margins="100,100,100,100")
    _run(platemaker_bin, "workspace", "import-profiles", "--workspace", str(ws), "--from", str(source))
    _run(platemaker_bin, "template", "--workspace", str(ws), "--profile", "Profil",
         "--output", str(template))
    assert template.is_file()

    _run(platemaker_bin, "project", "create", "--workspace", str(ws), "--name", "Ch01",
         "--input", str(_pages(root / FIRST_PAGES)))
    pages = _pages(root / PAGES)
    _run(platemaker_bin, "project", "mod", "--workspace", str(ws), "--name", "Ch01",
         "--input", str(pages))
    # process --input finds the project by its stored folder: a second project means the stored
    # folder no longer equals the one given.
    _run(platemaker_bin, "process", "--workspace", str(ws), "--input", str(pages),
         "--output", str(out), "--no-profile", "--format", "png")
    assert list(out.glob("output_*.png"))

    projects = json.loads(ws.read_bytes().decode("utf-8"))["projectItems"]
    assert len(projects) == 1
    assert pathlib.Path(projects[0]["inputDirectory"]).name == PAGES
    inputs = projects[0]["inputFiles"]
    assert len(inputs) == PAGE_COUNT
    assert all(PAGES in i["filePath"] for i in inputs)
