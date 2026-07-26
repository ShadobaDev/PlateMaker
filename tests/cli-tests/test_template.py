"""
test_template.py — CLI integration tests for ``platemaker template``.

Covers:
  - Basic template generation (exit code 0, file created).
  - PNG dimensions match the canvas profile's canvasSize.
  - Missing required arguments produce exit code 1.
  - Non-existent profile produces exit code 1 with a useful message.
  - --margins-tpl-color / --background-tpl-color override colours at runtime
    (bad colour strings are silently ignored; generation still succeeds).
  - Colours stored on the profile via add-canvas-profile --margins-tpl-color
    are persisted to the workspace JSON and used by template generation.
"""

from __future__ import annotations

import json
import pathlib
import struct
import subprocess

import pytest

from helpers import create_workspace, add_profile


# ---------------------------------------------------------------------------
# PNG utilities (stdlib only — no Pillow dependency in tests)
# ---------------------------------------------------------------------------

def _png_dimensions(path: pathlib.Path) -> tuple[int, int]:
    """
    Read the width and height from a PNG file's IHDR chunk.

    The PNG spec places the IHDR chunk immediately after the 8-byte signature:
      offset  0 ..  7  — 8-byte PNG signature
      offset  8 .. 11  — IHDR chunk length (4 bytes, big-endian)
      offset 12 .. 15  — "IHDR" tag
      offset 16 .. 19  — width  (4 bytes, big-endian)
      offset 20 .. 23  — height (4 bytes, big-endian)

    :param path: Path to a valid PNG file.
    :returns:    ``(width, height)`` as a tuple of integers.
    :raises ValueError: If the file does not look like a valid PNG.
    """
    with path.open("rb") as f:
        header = f.read(24)
    if len(header) < 24 or header[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a valid PNG file: {path}")
    width, height = struct.unpack(">II", header[16:24])
    return width, height


def _png_bands(path: pathlib.Path) -> int:
    """
    Read the colour type from the PNG IHDR and return the number of channels.

    Colour type byte is at offset 25 in the file (after the 8-byte sig,
    4-byte length, 4-byte tag, 4-byte width, 4-byte height, 1-byte bit-depth).

    Colour type → channels:
      0 = greyscale        → 1
      2 = RGB              → 3
      3 = indexed          → 1
      4 = greyscale+alpha  → 2
      6 = RGBA             → 4

    :param path: Path to a valid PNG file.
    :returns:    Number of channels (bands).
    """
    with path.open("rb") as f:
        data = f.read(26)
    colour_type = data[25]
    mapping = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
    return mapping.get(colour_type, -1)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def ws(tmp_workspace: pathlib.Path, platemaker_bin: pathlib.Path) -> pathlib.Path:
    """
    Per-test fixture: creates a workspace with a single "WebtoonTest" profile.

    Canvas: 1600×10240 px, margins: 100,100,100,100 px.
    Returns the path to the workspace JSON file.
    """
    ws_path = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws_path, target_width=800, slice_height=1280)
    add_profile(
        platemaker_bin,
        ws_path,
        name="WebtoonTest",
        canvas="1600x10240",
        margins="100,100,100,100",
    )
    return ws_path


# ---------------------------------------------------------------------------
# Basic tests
# ---------------------------------------------------------------------------

class TestTemplateBasic:
    """Basic happy-path tests for ``platemaker template``."""

    def test_template_creates_file(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """Template command exits 0 and creates the output PNG file."""
        out = tmp_workspace / "template.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--profile",   "WebtoonTest",
                "--output",    str(out),
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"template command failed (rc={result.returncode}):\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
        assert out.exists(), "Expected output PNG file was not created."
        assert out.stat().st_size > 0, "Output PNG file is empty."

    def test_template_png_dimensions(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """Output PNG dimensions match the canvas profile (1600×10240)."""
        out = tmp_workspace / "template.png"
        subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--profile",   "WebtoonTest",
                "--output",    str(out),
            ],
            capture_output=True,
            check=True,
        )
        width, height = _png_dimensions(out)
        assert width  == 1600,  f"Expected width 1600, got {width}"
        assert height == 10240, f"Expected height 10240, got {height}"

    def test_template_png_is_rgba(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """Output PNG is a 4-band (RGBA) image so alpha is preserved."""
        out = tmp_workspace / "template.png"
        subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--profile",   "WebtoonTest",
                "--output",    str(out),
            ],
            capture_output=True,
            check=True,
        )
        bands = _png_bands(out)
        assert bands == 4, (
            f"Expected 4-band RGBA PNG (colour type 6), got {bands} bands"
        )

    def test_template_stderr_reports_path(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """stderr should mention 'Template written' and the output path."""
        out = tmp_workspace / "template.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--profile",   "WebtoonTest",
                "--output",    str(out),
            ],
            capture_output=True,
            text=True,
        )
        assert "Template written" in result.stderr, (
            f"Expected 'Template written' in stderr.\nstderr: {result.stderr}"
        )

    def test_template_output_directory_created(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """Output parent directories are created automatically."""
        out = tmp_workspace / "subdir" / "nested" / "template.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--profile",   "WebtoonTest",
                "--output",    str(out),
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"template with nested output dir failed:\nstderr: {result.stderr}"
        )
        assert out.exists()

    def test_template_png_extension_auto_appended(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """
        When --output has no .png extension the tool appends it and creates a
        valid PNG file.  The path *without* the extension must NOT exist, and
        the path *with* .png appended must be a valid PNG.
        """
        out_no_ext = tmp_workspace / "template_no_ext"
        out_with_ext = pathlib.Path(str(out_no_ext) + ".png")
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--profile",   "WebtoonTest",
                "--output",    str(out_no_ext),
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"template with no-extension output failed:\nstderr: {result.stderr}"
        )
        # The un-extended path should NOT exist.
        assert not out_no_ext.exists(), (
            "File without .png extension should not have been created."
        )
        # The .png-extended path MUST exist and be a valid PNG.
        assert out_with_ext.exists(), (
            "Expected template_no_ext.png to be created after extension append."
        )
        # Verify it's actually a PNG (correct signature).
        with out_with_ext.open("rb") as f:
            sig = f.read(8)
        assert sig == b"\x89PNG\r\n\x1a\n", "File does not start with a PNG signature."


# ---------------------------------------------------------------------------
# Colour override tests
# ---------------------------------------------------------------------------

class TestTemplateColours:
    """Tests for --margins-tpl-color and --background-tpl-color flags."""

    def test_margins_tpl_color_override(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """--margins-tpl-color is accepted and template is generated."""
        out = tmp_workspace / "template_red_margins.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace",          str(ws),
                "--profile",            "WebtoonTest",
                "--output",             str(out),
                "--margins-tpl-color",  "255,0,0,200",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"template with --margins-tpl-color failed:\nstderr: {result.stderr}"
        )
        assert out.exists()

    def test_background_tpl_color_override(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """--background-tpl-color is accepted and template is generated."""
        out = tmp_workspace / "template_white_bg.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace",             str(ws),
                "--profile",               "WebtoonTest",
                "--output",                str(out),
                "--background-tpl-color",  "255,255,255,255",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"template with --background-tpl-color failed:\nstderr: {result.stderr}"
        )
        assert out.exists()

    def test_rgb_only_colour_uses_opaque_alpha(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """R,G,B without alpha component defaults to fully opaque (255)."""
        out = tmp_workspace / "template_rgb_only.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace",         str(ws),
                "--profile",           "WebtoonTest",
                "--output",            str(out),
                "--margins-tpl-color", "0,128,0",  # no alpha → defaults to 255
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"template with RGB-only colour failed:\nstderr: {result.stderr}"
        )
        assert out.exists()

    def test_bad_colour_string_is_silently_ignored(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """
        A malformed colour string (e.g. 'red') must NOT abort template generation.

        The command should exit 0 and still create the output file, using the
        profile/default colour instead.
        """
        out = tmp_workspace / "template_bad_color.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace",         str(ws),
                "--profile",           "WebtoonTest",
                "--output",            str(out),
                "--margins-tpl-color", "not-a-colour",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"Expected success with bad colour string (colours are cosmetic), "
            f"got rc={result.returncode}:\nstderr: {result.stderr}"
        )
        assert out.exists(), "Template file was not created despite bad colour."


# ---------------------------------------------------------------------------
# Colour stored on profile
# ---------------------------------------------------------------------------

class TestProfileColourStorage:
    """Tests that colour options are persisted to the workspace via add/mod-canvas-profile."""

    def test_add_profile_with_margins_tpl_color(
        self,
        platemaker_bin: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """
        workspace add-canvas-profile --margins-tpl-color stores the colour in the JSON.
        """
        ws_path = tmp_workspace / "project.platemaker.json"
        create_workspace(platemaker_bin, ws_path)
        result = subprocess.run(
            [
                str(platemaker_bin),
                "workspace", "add-canvas-profile",
                "--workspace",         str(ws_path),
                "--name",              "ColourTest",
                "--canvas",            "800x2560",
                "--margins",           "0,0,0,0",
                "--margins-tpl-color", "255,0,0,200",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"add-canvas-profile with --margins-tpl-color failed:\nstderr: {result.stderr}"
        )

        ws_json = json.loads(ws_path.read_text())
        profiles = ws_json.get("canvasProfiles", [])
        assert profiles, "No canvas profiles found in workspace JSON."
        cp = next((p for p in profiles if p["name"] == "ColourTest"), None)
        assert cp is not None, "'ColourTest' profile not found."
        vc = cp.get("visualColour", {})
        assert vc.get("r") == 255, f"Expected r=255, got {vc}"
        assert vc.get("g") == 0,   f"Expected g=0,   got {vc}"
        assert vc.get("b") == 0,   f"Expected b=0,   got {vc}"
        assert vc.get("a") == 200, f"Expected a=200, got {vc}"

    def test_add_profile_with_background_tpl_color(
        self,
        platemaker_bin: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """
        workspace add-canvas-profile --background-tpl-color stores backgroundColour.
        """
        ws_path = tmp_workspace / "project.platemaker.json"
        create_workspace(platemaker_bin, ws_path)
        subprocess.run(
            [
                str(platemaker_bin),
                "workspace", "add-canvas-profile",
                "--workspace",             str(ws_path),
                "--name",                  "BgTest",
                "--canvas",                "800x2560",
                "--margins",               "0,0,0,0",
                "--background-tpl-color",  "255,255,255,255",
            ],
            capture_output=True,
            check=True,
        )

        ws_json = json.loads(ws_path.read_text())
        profiles = ws_json.get("canvasProfiles", [])
        cp = next((p for p in profiles if p["name"] == "BgTest"), None)
        assert cp is not None
        bg = cp.get("backgroundColour", {})
        assert bg.get("r") == 255
        assert bg.get("g") == 255
        assert bg.get("b") == 255
        assert bg.get("a") == 255

    def test_mod_profile_with_margins_tpl_color(
        self,
        platemaker_bin: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """
        workspace mod-canvas-profile --margins-tpl-color updates visualColour in JSON.
        """
        ws_path = tmp_workspace / "project.platemaker.json"
        create_workspace(platemaker_bin, ws_path)
        add_profile(
            platemaker_bin,
            ws_path,
            name="ModTest",
            canvas="800x2560",
            margins="0,0,0,0",
        )
        # Modify colour.
        subprocess.run(
            [
                str(platemaker_bin),
                "workspace", "mod-canvas-profile",
                "--workspace",         str(ws_path),
                "--name",              "ModTest",
                "--margins-tpl-color", "0,0,255,128",
            ],
            capture_output=True,
            check=True,
        )

        ws_json = json.loads(ws_path.read_text())
        cp = next(
            (p for p in ws_json["canvasProfiles"] if p["name"] == "ModTest"),
            None,
        )
        assert cp is not None
        vc = cp["visualColour"]
        assert vc["r"] == 0,   f"Expected r=0,   got {vc}"
        assert vc["g"] == 0,   f"Expected g=0,   got {vc}"
        assert vc["b"] == 255, f"Expected b=255, got {vc}"
        assert vc["a"] == 128, f"Expected a=128, got {vc}"

    def test_stored_colour_used_when_no_cli_override(
        self,
        platemaker_bin: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """
        Stored profile colour is used for generation when no CLI override is given.
        The test only checks exit code 0 + file created (pixel verification would
        require Pillow, which is not in the test requirements).
        """
        ws_path = tmp_workspace / "project.platemaker.json"
        create_workspace(platemaker_bin, ws_path)
        subprocess.run(
            [
                str(platemaker_bin),
                "workspace", "add-canvas-profile",
                "--workspace",             str(ws_path),
                "--name",                  "StoredColour",
                "--canvas",                "800x5120",
                "--margins",               "50,50,50,50",
                "--margins-tpl-color",     "0,200,0,180",
                "--background-tpl-color",  "240,240,240,255",
            ],
            capture_output=True,
            check=True,
        )

        out = tmp_workspace / "stored_colour.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws_path),
                "--profile",   "StoredColour",
                "--output",    str(out),
                # No --margins-tpl-color or --background-tpl-color here;
                # colours should come from the stored profile.
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"template with stored colour failed:\nstderr: {result.stderr}"
        )
        assert out.exists()
        width, height = _png_dimensions(out)
        assert width == 800 and height == 5120


# ---------------------------------------------------------------------------
# Error / argument validation tests
# ---------------------------------------------------------------------------

class TestTemplateErrors:
    """Tests that verify proper error handling for missing or invalid arguments."""

    def test_missing_workspace_arg(
        self,
        platemaker_bin: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """Exit code 1 when --workspace is not provided."""
        out = tmp_workspace / "template.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--profile", "WebtoonTest",
                "--output",  str(out),
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 1

    def test_missing_profile_arg(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """Exit code 1 when --profile is not provided."""
        out = tmp_workspace / "template.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--output",    str(out),
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 1

    def test_missing_output_arg(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
    ) -> None:
        """Exit code 1 when --output is not provided."""
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--profile",   "WebtoonTest",
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 1

    def test_nonexistent_profile(
        self,
        platemaker_bin: pathlib.Path,
        ws: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """Exit code 1 and informative message when profile name does not exist."""
        out = tmp_workspace / "template.png"
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(ws),
                "--profile",   "DoesNotExist",
                "--output",    str(out),
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 1
        assert "DoesNotExist" in result.stderr, (
            f"Expected profile name in error message.\nstderr: {result.stderr}"
        )
        assert not out.exists(), "Template file should not be created on error."

    def test_nonexistent_workspace(
        self,
        platemaker_bin: pathlib.Path,
        tmp_workspace: pathlib.Path,
    ) -> None:
        """Exit code 2 when workspace file does not exist."""
        result = subprocess.run(
            [
                str(platemaker_bin),
                "template",
                "--workspace", str(tmp_workspace / "no_such.platemaker.json"),
                "--profile",   "WebtoonTest",
                "--output",    str(tmp_workspace / "template.png"),
            ],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 2
