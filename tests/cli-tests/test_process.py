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
import re
import shutil
import subprocess

import pytest

from helpers import create_workspace, add_profile, make_solid_png


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

# Real phone-camera JPEGs used by the docs/TODO.md Step 2a/2b regression tests
# below — see fixtures/real_photos/README.md for provenance and EXIF details.
FIXTURES_DIR    = pathlib.Path(__file__).parent / "fixtures" / "real_photos"
ORIENTATION_6_PHOTO = "20161127_144117.jpg"  # the portrait shot, stored landscape

def _run_process(
    platemaker_bin: pathlib.Path,
    workspace:      pathlib.Path,
    output_dir:     pathlib.Path,
    extra_args:     list[str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """Run ``platemaker process`` and return the CompletedProcess result.

    These tests assert on ``output_*.png`` filenames because they verify slice
    counts, numbering, and incremental behaviour — not the container format.
    The default output profile is JPEG (the Webtoon Standard preset), so the
    format is pinned to PNG here; a caller that passes its own ``--format`` in
    *extra_args* overrides it (flags are last-wins).
    """
    cmd = [
        str(platemaker_bin), "process",
        "--workspace", str(workspace),
        "--output",    str(output_dir),
        "--format",    "png",
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
# Standard pipeline (no canvas profiles)
# ---------------------------------------------------------------------------

def test_process_standard_pipeline_produces_slices(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    3 × 800×2560 PNG pages (total 7680 px) sliced at 1280 px each must
    produce exactly 6 output files: output_001.png … output_006.png.

    No canvas profiles are defined → standard pipeline (no margin cropping).
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

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
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

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
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--format", "jpg"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    png_files = sorted(output_dir.glob("output_*.png"))
    jpg_files = sorted(output_dir.glob("output_*.jpg"))

    assert len(png_files) == 0, "Expected no PNG files when format=jpg"
    assert len(jpg_files) == 6, f"Expected 6 JPG slices, got {len(jpg_files)}"
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
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--format", "webp"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    webp_files = sorted(output_dir.glob("output_*.webp"))
    assert len(webp_files) == 6, f"Expected 6 WebP slices, got {len(webp_files)}"


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
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--json"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    summary = json.loads(result.stdout)

    assert "sliceCount"  in summary
    assert "outputFiles" in summary
    assert summary["sliceCount"]        == 6
    assert len(summary["outputFiles"])  == 6
    assert summary["outputFiles"][0]    == "output_001.png"
    assert summary["cancelled"] == False  # noqa: E712


# ---------------------------------------------------------------------------
# Margin-aware pipeline
# ---------------------------------------------------------------------------

def test_process_margin_aware_pipeline(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    Margin-aware pipeline via canvas profile matching by width.

      * Canvas profile: canvas=1000x2000, margins=100,100,100,100
        → safe area = 800×1800 per image.
      * Input images are 1000×2000 (full canvas including margins).
      * 3 images × 1800 px = 5400 px total after cropping.
      * 5400 / 1280 = 4 full slices + 1 tail (280 px) → 5 output files.

    Without margin cropping (standard pipeline), the same images would be
    1000×2000 scaled to 800×1600 each (3×1600=4800 px → 3+1=4 files).
    The different file count proves the crop step executed.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    # Canvas images: 1000 px wide (800 safe + 100 left + 100 right)
    _make_pages(input_dir, 3, width=1000, height=2000, color=(200, 200, 200))

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)
    # Profile width 1000 px — process will auto-detect this profile for 1000px files.
    add_profile(platemaker_bin, ws,
                name="Canvas-1000",
                canvas="1000x2000",
                margins="100,100,100,100")

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir)])
    assert result.returncode == 0, (
        f"margin-aware process failed:\n{result.stderr}"
    )

    slices = sorted(output_dir.glob("output_*.png"))
    # 3 × 1800 = 5400 px → 4 full slices + 1 tail = 5 files
    assert len(slices) == 5, (
        f"Expected 5 slices from margin-aware pipeline, got {len(slices)}: "
        f"{[p.name for p in slices]}"
    )


def test_process_incompatible_files_are_skipped(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    When canvas profiles are defined, files whose width doesn't match any
    profile must be skipped with a warning and not cause a hard failure.
    Files that DO match must still be processed successfully.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    # 2 compatible (width=1000) + 1 incompatible (width=800)
    make_solid_png(input_dir / "image_000.png", 1000, 2000)
    make_solid_png(input_dir / "image_001.png", 1000, 2000)
    make_solid_png(input_dir / "image_002.png",  800, 2560)  # incompatible

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)
    add_profile(platemaker_bin, ws,
                name="Canvas-1000",
                canvas="1000x2000",
                margins="0,0,0,0")  # zero margins → standard scale, no crop

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir)])
    # Should succeed (2 files processed) even though 1 was incompatible.
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    # The incompatible file warning should appear on stderr.
    assert "incompatible" in result.stderr.lower() or \
           "does not match" in result.stderr.lower(), \
        "Expected an incompatibility warning on stderr"

    # Only the 2 compatible 1000-px files were processed.
    slices = sorted(output_dir.glob("output_*.png"))
    assert len(slices) >= 1, "Expected at least 1 output slice from 2 compatible files"


# ---------------------------------------------------------------------------
# --no-profile flag
# ---------------------------------------------------------------------------

def test_process_no_profile_bypasses_canvas_profiles(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    --no-profile must cause all input files to be processed with the standard
    pipeline regardless of canvas profiles defined in the workspace.

    The workspace has a canvas profile for 1000×2000 images.  All 3 test
    images are 800×2560 — they would normally be skipped as incompatible.
    With --no-profile the profile check is bypassed and all 3 files are
    scaled and sliced normally.

    3 × 800×2560 → total 7680 px → 6 slices at 1280 px.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    # Images that do NOT match the canvas profile below.
    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)
    # Profile that would reject the 800×2560 images (different size).
    add_profile(platemaker_bin, ws,
                name="Canvas-1000",
                canvas="1000x2000",
                margins="100,100,100,100")

    # Without --no-profile all files would be skipped → exit code 3.
    # With --no-profile they must all be processed.
    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--no-profile"])
    assert result.returncode == 0, (
        f"process with --no-profile failed:\n{result.stderr}"
    )

    slices = sorted(output_dir.glob("output_*.png"))
    assert len(slices) == 6, (
        f"Expected 6 slices with --no-profile, got {len(slices)}: "
        f"{[p.name for p in slices]}"
    )


# ---------------------------------------------------------------------------
# Incremental processing (SHA-256 cache)
# ---------------------------------------------------------------------------

def test_process_incremental_skips_unchanged_files(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    A second ``process`` invocation on unchanged input files must be skipped.

    1. First run → processes 6 slices, saves processedFiles to workspace.
    2. Second run (same workspace, same --input, same files) →
       - exits 0
       - stderr contains "Nothing to do"
       - output directory is NOT cleared (slices from run 1 still present)
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    # --- First run ---
    r1 = _run_process(platemaker_bin, ws, output_dir,
                      ["--input", str(input_dir)])
    assert r1.returncode == 0, f"First run failed:\n{r1.stderr}"
    slices_after_run1 = sorted(output_dir.glob("output_*.png"))
    assert len(slices_after_run1) == 6

    # --- Second run (files unchanged) ---
    r2 = _run_process(platemaker_bin, ws, output_dir,
                      ["--input", str(input_dir)])
    assert r2.returncode == 0, f"Second run failed:\n{r2.stderr}"

    assert "nothing to do" in r2.stderr.lower() or \
           "unchanged" in r2.stderr.lower(), (
        f"Expected 'nothing to do' / 'unchanged' in stderr:\n{r2.stderr}"
    )

    # Output files are still the same (not removed/regenerated).
    slices_after_run2 = sorted(output_dir.glob("output_*.png"))
    assert len(slices_after_run2) == 6


def test_process_incremental_reprocesses_changed_file(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    After modifying an input file (different pixel content), the second run
    must reprocess and produce fresh output slices.

    1. First run with red solid pages → 6 slices.
    2. Overwrite one input file with a green solid page (same size, new SHA-256).
    3. Second run → must NOT be skipped, must succeed and produce 6 slices.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    # Red pages for the first run.
    pages = _make_pages(input_dir, 3, width=800, height=2560, color=(255, 0, 0))

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    # --- First run ---
    r1 = _run_process(platemaker_bin, ws, output_dir,
                      ["--input", str(input_dir)])
    assert r1.returncode == 0, f"First run failed:\n{r1.stderr}"
    assert len(sorted(output_dir.glob("output_*.png"))) == 6

    # --- Modify one input file (green — different SHA-256) ---
    make_solid_png(pages[0], 800, 2560, 0, 255, 0)

    # --- Second run — must reprocess ---
    r2 = _run_process(platemaker_bin, ws, output_dir,
                      ["--input", str(input_dir)])
    assert r2.returncode == 0, f"Second run failed:\n{r2.stderr}"

    assert "nothing to do" not in r2.stderr.lower(), (
        "Expected reprocessing, but 'nothing to do' appeared in stderr"
    )
    assert len(sorted(output_dir.glob("output_*.png"))) == 6


# ---------------------------------------------------------------------------
# Diagnostic tracing (--trace shadow argument)
# ---------------------------------------------------------------------------

def test_process_trace_enables_component_logs(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    The --trace=<bitmask> shadow argument turns on the library's per-component
    diagnostic logging without changing the outcome. Bits (see log.hpp):
    0x1 ProcessingPipeline · 0x2 Scaler · 0x4 ScaledStrip.

    Runs a render with all three enabled (0x7) and asserts the run still succeeds
    and that each component's tag appears on stderr (the default logger sink).
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--trace=0x7"])
    assert result.returncode == 0, f"traced process failed:\n{result.stderr}"

    # Tracing does not disturb the produced output.
    assert len(sorted(output_dir.glob("output_*.png"))) == 6

    # Each traced component logs at least once, tagged by name, to stderr.
    assert "[Scaler]" in result.stderr, result.stderr
    assert "[ScaledStrip]" in result.stderr, result.stderr
    assert "[ProcessingPipeline]" in result.stderr, result.stderr


def test_process_without_trace_is_quiet(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """A normal render emits no component-trace lines (the logger defaults to all-off)."""
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    _make_pages(input_dir, 3, width=800, height=2560)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir)])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    for tag in ("[Scaler]", "[ScaledStrip]", "[ProcessingPipeline]"):
        assert tag not in result.stderr, f"unexpected trace {tag} in a non-traced run:\n{result.stderr}"


# ---------------------------------------------------------------------------
# Real-photo regression (docs/TODO.md Step 2a / Step 2b)
# ---------------------------------------------------------------------------
#
# Synthetic solid-colour PNGs can't reproduce either bug: their EXIF is empty
# and their heights are always chosen as clean multiples of slice_height.
# These three real phone-camera JPEGs (mixed EXIF Orientation, non-multiple
# pixel heights) are what originally exposed both defects — see
# fixtures/real_photos/README.md.

def test_process_real_photos_no_black_band(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    Step 2a regression: multi-source slices must come out tight, never padded
    to the tallest contributing part (the old vips_arrayjoin black band).

    All three 3264x2448 photos scale to 800x600 at target-width=800 (EXIF
    orientation is not yet applied — that's Step 2b, covered separately
    below). Total scaled height 1800px / slice-height 1280 → one full slice
    (600+600+80 from the three sources) plus a 520px tail — the exact
    breakdown recorded in docs/TODO.md's Step 2a verification, reproduced
    here from --trace=0x7 instead of by hand.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    for photo in sorted(FIXTURES_DIR.glob("*.jpg")):
        shutil.copy(photo, input_dir / photo.name)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--trace=0x7"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    slices = sorted(output_dir.glob("output_*.png"))
    assert len(slices) == 2, (
        f"Expected 2 slices (1280 + 520 tail), got {len(slices)}: "
        f"{[p.name for p in slices]}"
    )

    # Every buildSlice trace line: built height must equal the requested
    # window, and never exceed it (the black-band signature was built >
    # requested, e.g. 800x1800 for a req=[0,1280) window).
    build_lines = re.findall(
        r"buildSlice (\d+) req=\[(\d+),(\d+)\) built=(\d+)x(\d+) parts=(\d+)",
        result.stderr,
    )
    assert len(build_lines) == 2, f"Expected 2 buildSlice trace lines:\n{result.stderr}"

    for idx, start, end, _w, h, _parts in build_lines:
        requested = int(end) - int(start)
        assert int(h) == requested, (
            f"slice {idx}: built height {h} != requested {requested} "
            f"(a join padded the slice)\n{result.stderr}"
        )

    # Pin the exact breakdown from docs/TODO.md's manual verification.
    _, _, _, w0, h0, parts0 = build_lines[0]
    assert (w0, h0, parts0) == ("800", "1280", "3")
    _, _, _, w1, h1, parts1 = build_lines[1]
    assert (w1, h1, parts1) == ("800", "520", "1")


@pytest.mark.xfail(
    strict=True,
    reason="Step 2b (EXIF autorotate) not implemented yet — see docs/TODO.md. "
           "Remove this xfail when Scaler applies vips_autorot.",
)
def test_process_real_photos_orientation_applied(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
) -> None:
    """
    Step 2b (not yet implemented): the Orientation=6 photo is a portrait
    shot stored as landscape 3264x2448. Once Scaler autorotates on load, it
    must scale to a *portrait* 800xN (N > 800), not today's landscape 800x600.

    strict=True: once Step 2b lands this test starts passing, which XPASSes
    and fails the suite — a deliberate nudge to delete the xfail marker
    instead of letting it silently rot.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    input_dir.mkdir()
    output_dir.mkdir()

    for photo in sorted(FIXTURES_DIR.glob("*.jpg")):
        shutil.copy(photo, input_dir / photo.name)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=800, slice_height=1280)

    result = _run_process(platemaker_bin, ws, output_dir,
                          ["--input", str(input_dir), "--trace=0x2"])
    assert result.returncode == 0, f"process failed:\n{result.stderr}"

    pattern = re.compile(
        r"scale\(file\) .*" + re.escape(ORIENTATION_6_PHOTO)
        + r": \d+x\d+ -> (\d+)x(\d+)"
    )
    match = pattern.search(result.stderr)
    assert match, f"no Scaler trace line for {ORIENTATION_6_PHOTO}:\n{result.stderr}"

    out_w, out_h = int(match.group(1)), int(match.group(2))
    assert out_h > out_w, (
        f"{ORIENTATION_6_PHOTO} scaled to {out_w}x{out_h} (landscape) — "
        "expected portrait (height > width) once autorotate is applied"
    )
