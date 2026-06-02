"""
Integration tests: ``platemaker process`` subcommand.

Each test creates real PNG input files, runs the full pipeline (scale → strip
→ slice → save), and verifies the output on disk.

PNG files are generated with ``helpers.make_solid_png`` — no external imaging
library required.
"""

from __future__ import annotations

import json
import pathlib
import subprocess

import pytest

from helpers import create_workspace, make_solid_png


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _run_process(
    platemaker_bin: pathlib.Path,
    workspace:      pathlib.Path,
    output_dir:     pathlib.Path,
    extra_args:     list[str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """Run ``platemaker process`` and return the CompletedProcess result."""
    cmd = [
        str(platemaker_bin), "process",
        "--workspace", str(workspace),
        "--output",    str(output_dir),
    ]
    if extra_args:
        cmd.extend(extra_args)
    return subprocess.run(cmd, capture_output=True, text=True)


def _make_pages(
    directory:  pathlib.Path,
    count:      int,
    width:      int,
    height:     int,
    color:      tuple[int, int, int] = (255, 255, 255),
) -> list[pathlib.Path]:
    """Create *count* solid-colour PNG files in *directory*."""
    pages = []
    for i in range(count):
        p = directory / f"image_{i:03d}.png"
        make_solid_png(p, width, height, *color)
        pages.append(p)
    return pages


# ---------------------------------------------------------------------------
# Standard pipeline
# ---------------------------------------------------------------------------

def test_process_standard_pipeline_produces_slices(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    3 × 800×2560 PNG pages (total 7680 px) sliced at 1280 px each must
    produce exactly 6 output files: output_001.png … output_006.png.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws,
                     canvas="800x2560", margins="0,0,0,0",
                     target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir)])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    slices = sorted(output_dir.glob("output_*.png"))
    assert len(slices) == 6, (
        f"Expected 6 slices, got {len(slices)}: {[p.name for p in slices]}"
    )
    assert slices[0].name  == "output_001.png"
    assert slices[-1].name == "output_006.png"


def test_process_start_index(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    --start-index 5 must number the first output file output_005.png.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws,
                     canvas="800x2560", margins="0,0,0,0",
                     target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--start-index", "5"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    slices = sorted(output_dir.glob("output_*.png"))
    assert len(slices) == 6
    assert slices[0].name  == "output_005.png"
    assert slices[-1].name == "output_010.png"


def test_process_format_jpg(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    --format jpg must produce .jpg output files instead of .png.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws,
                     canvas="800x2560", margins="0,0,0,0",
                     target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--format", "jpg"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    # No PNG files should appear; 6 JPG files should.
    png_files = sorted(output_dir.glob("output_*.png"))
    jpg_files = sorted(output_dir.glob("output_*.jpg"))

    assert len(png_files) == 0, "Expected no PNG files when format=jpg"
    assert len(jpg_files) == 6, (
        f"Expected 6 JPG slices, got {len(jpg_files)}"
    )
    assert jpg_files[0].name == "output_001.jpg"


def test_process_format_webp(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    --format webp must produce .webp output files.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws,
                     canvas="800x2560", margins="0,0,0,0",
                     target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--format", "webp"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    webp_files = sorted(output_dir.glob("output_*.webp"))
    assert len(webp_files) == 6, (
        f"Expected 6 WebP slices, got {len(webp_files)}"
    )


def test_process_json_flag_outputs_valid_json(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    --json must print a machine-readable JSON summary to stdout with
    ``sliceCount`` and ``outputFiles`` fields.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws,
                     canvas="800x2560", margins="0,0,0,0",
                     target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--json"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    summary = json.loads(result.stdout)

    assert "sliceCount"  in summary
    assert "outputFiles" in summary
    assert summary["sliceCount"]  == 6
    assert len(summary["outputFiles"]) == 6
    assert summary["outputFiles"][0]   == "output_001.png"
    assert summary["cancelled"] == False  # noqa: E712


# ---------------------------------------------------------------------------
# Margin-aware pipeline
# ---------------------------------------------------------------------------

def test_process_margin_aware_pipeline(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    Margin-aware pipeline:
      * Input images are 1000×2000 (canvas including 100 px margins on each side).
      * CanvasProfile: canvas=1000x2000, margins=100,100,100,100
        → safe area = 800×1800 per image.
      * 3 images × 1800 px = 5400 px total after cropping.
      * 5400 / 1280 = 4 full slices + 1 tail → 5 output files.

    Without margin cropping (standard pipeline on the same input), the images
    would be 1000×2000 scaled to 800×1600 each (3×1600=4800 px → 3+1=4 files).
    The different file count proves the crop step executed.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    # Canvas images: 1000 px wide (800 safe + 100 left + 100 right)
    _make_pages(input_dir, 3, width=1000, height=2000, color=(200, 200, 200))

    ws = tmp_workspace / "project.platemaker.json"
    # margins: top=100, right=100, bottom=100, left=100
    # safe area: 1000-200=800 wide, 2000-200=1800 tall
    create_workspace(
        platemaker_bin, ws,
        canvas="1000x2000",
        margins="100,100,100,100",
        target_width=800,
        slice_height=1280,
    )

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir)])
    assert result.returncode == 0, (
        f"margin-aware process failed:\n{result.stderr}"
    )

    slices = sorted(output_dir.glob("output_*.png"))
    # 3 × 1800 = 5400 px → 4 full slices of 1280 + 1 tail slice of 280 = 5
    assert len(slices) == 5, (
        f"Expected 5 slices from margin-aware pipeline, got {len(slices)}: "
        f"{[p.name for p in slices]}"
    )
