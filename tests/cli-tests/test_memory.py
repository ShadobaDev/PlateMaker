"""
Integration tests: decoded-pixel residency during a render.

These pin the strip's **streaming contract** — "only the sources overlapping the current slice stay
decoded" (see ScaledStrip's class docs) — which is the reason a 200-page chapter renders in the same
memory as a 3-page one. Nothing else in the suite covers it, and it is not observable from ordinary
output: libvips decodes lazily, inside its own operations, whenever a consumer first pulls a pixel, so
a strip that quietly held every page would still produce byte-identical slices. It would just need
gigabytes to do it.

The `Memory` trace channel (``--trace=0x4000``, ``Log::Memory``) reads residency back out of the
libvips allocator, which is what makes the contract assertable at all. ``vipsMemKiB`` is what libvips
holds *right now*, sampled after each slice is written.

Pages are generated at render time by ``helpers.make_solid_png`` (stdlib PNG writer, no imaging
dependency and nothing committed to fixtures/): the contract is about **geometry and residency**, not
image content, so solid colours are exactly as good as real artwork and cost ~1.5 MB on disk.
Dimensions are the production shape divided by four — a 400×2560 page scaled to 200 wide is 1280 tall,
i.e. exactly one slice, the same page/slice alignment a real webtoon chapter has.
"""

from __future__ import annotations

import pathlib
import re
import shutil
import subprocess

import pytest

from helpers import create_workspace, make_solid_png, png_size


# ---------------------------------------------------------------------------
# Geometry (production shape / 4)
# ---------------------------------------------------------------------------

PAGE_W, PAGE_H     = 400, 2560   # source page → scales to 200×1280 = exactly one slice
TARGET_W, SLICE_H  = 200, 1280
INSERT_W, INSERT_H = 400, 100    # → 200×50 scaled: pushes every slice below it off the page joins

# A resident source page costs its *source* resolution (VIPS_ACCESS_RANDOM caches the decode), and
# make_solid_png writes 3-band RGB.
PAGE_KIB = PAGE_W * PAGE_H * 3 // 1024

# Enough pages that residency must plateau well before the halfway mark for the run to be bounded at
# all: libvips reclaims a released page on its own cache schedule (~13 pages at this size), so a
# shorter chapter would finish before the ceiling binds and prove nothing.
PAGE_COUNT = 40


# ---------------------------------------------------------------------------
# Trace parsing
# ---------------------------------------------------------------------------

def _residency(stderr: str, phase: str) -> list[int]:
    """Every ``vipsMemKiB`` sample whose phase tag contains *phase*, in emission order."""
    return [
        int(kib)
        for tag, kib in re.findall(r"\[Memory\] (.+?)\s+vipsMemKiB=(\d+)", stderr)
        if phase in tag
    ]


def _render(
    platemaker_bin: pathlib.Path,
    workspace:      pathlib.Path,
    input_dir:      pathlib.Path,
    output_dir:     pathlib.Path,
    trace:          str,
) -> subprocess.CompletedProcess[str]:
    """Run a traced ``platemaker process`` over *input_dir*."""
    return subprocess.run(
        [
            str(platemaker_bin), "process",
            "--workspace", str(workspace),
            "--input",     str(input_dir),
            "--output",    str(output_dir),
            "--format",    "png",
            f"--trace={trace}",
        ],
        capture_output=True,
        text=True,
    )


def _assert_streams(stderr: str, page_count: int) -> list[int]:
    """
    Assert the residency invariants shared by every layout, and return the per-slice curve.

    1. Loading, cropping and scaling decode **nothing** — the whole append phase is lazy graph
       building, so a chapter's length costs nothing until slicing starts.
    2. Band normalisation decodes nothing either. It runs over every entry while all of them are
       still live, so a materialising op there would decode the entire chapter at once.
    3. Residency stops growing. This is what ``releaseConsumedEntries()`` buys: without it every
       page stays referenced and the curve climbs monotonically to the end of the run.
    4. Peak residency stays far below the whole strip.
    """
    appends = _residency(stderr, "append")
    assert appends == [0] * page_count, (
        f"load → crop → scale must stay lazy, but pages were decoded during append: {appends}"
    )

    normalize = _residency(stderr, "normalize")
    assert normalize == [0, 0], (
        f"normalizeBandCounts() must not materialise any entry, got {normalize}"
    )

    curve = _residency(stderr, "post-write")
    assert curve, "no per-slice residency samples in the trace"

    half = len(curve) // 2
    assert max(curve[half:]) == max(curve[:half]), (
        "residency must stop growing once the strip starts releasing consumed pages; "
        f"it kept climbing: {curve}"
    )

    all_held = page_count * PAGE_KIB
    assert max(curve) < all_held // 2, (
        f"peak residency {max(curve)} KiB is more than half of the {all_held} KiB the whole strip "
        f"would cost — the strip is accumulating pages, not streaming them: {curve}"
    )
    return curve


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="module")
def aligned_pages(tmp_path_factory: pytest.TempPathFactory) -> pathlib.Path:
    """A chapter of identical pages whose scaled height is exactly one slice."""
    directory = tmp_path_factory.mktemp("aligned_pages")
    for i in range(PAGE_COUNT):
        make_solid_png(directory / f"page_{i:03d}.png", PAGE_W, PAGE_H, (i * 7) % 256, 60, 200)
    return directory


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_strip_streams_pages_instead_of_holding_the_chapter(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
    aligned_pages:  pathlib.Path,
) -> None:
    """
    A render's decoded-pixel residency is bounded, and bounded well below the chapter.

    The aligned case: each page is exactly one slice, so no slice ever spans two sources and the
    strip should never need more than a page at a time.
    """
    output_dir = tmp_workspace / "output"
    output_dir.mkdir()
    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=TARGET_W, slice_height=SLICE_H)

    result = _render(platemaker_bin, ws, aligned_pages, output_dir, "0x4000")
    assert result.returncode == 0, f"render failed:\n{result.stderr}"

    slices = sorted(output_dir.glob("output_*.png"))
    assert len(slices) == PAGE_COUNT, f"expected one slice per page, got {len(slices)}"

    _assert_streams(result.stderr, PAGE_COUNT)

    # Every page but the last is fully consumed before the run ends, so every one but the last is
    # released — by name, in strip order.
    released = re.findall(r"\[Memory\] release .*?([^\\/]+\.png) \(rows", result.stderr)
    assert released == [f"page_{i:03d}.png" for i in range(PAGE_COUNT - 1)], released


def test_strip_streams_when_every_slice_straddles_two_pages(
    platemaker_bin: pathlib.Path,
    tmp_workspace:  pathlib.Path,
    aligned_pages:  pathlib.Path,
) -> None:
    """
    The same contract holds when slices no longer land on page joins.

    A short page early in the chapter (400×100 → 200×50 scaled) pushes every slice below it off the
    page boundaries, so almost every slice is now assembled from **two** sources via ``vips_join``.
    That path — two pages resident at once, and a release cadence that tracks strip geometry rather
    than page index — is the one a well-behaved production chapter never exercises, because its page
    heights happen to be exact multiples of the slice height.
    """
    input_dir  = tmp_workspace / "input"
    output_dir = tmp_workspace / "output"
    shutil.copytree(aligned_pages, input_dir)
    output_dir.mkdir()
    # Sorts second, right after page_000.png.
    make_solid_png(input_dir / "page_000b_insert.png", INSERT_W, INSERT_H, 255, 0, 255)

    ws = tmp_workspace / "project.platemaker.json"
    create_workspace(platemaker_bin, ws, target_width=TARGET_W, slice_height=SLICE_H)

    # 0x4004 = ScaledStrip | Memory: the strip channel reports each slice's part count.
    result = _render(platemaker_bin, ws, input_dir, output_dir, "0x4004")
    assert result.returncode == 0, f"render failed:\n{result.stderr}"

    # The insert must actually have shifted things: most slices now draw from two sources. Without
    # this the test would still pass on an aligned strip and guard nothing.
    parts = [int(p) for p in re.findall(r"buildSlice \d+ .*? parts=(\d+)", result.stderr)]
    assert parts.count(2) > len(parts) // 2, (
        f"expected the insert to make most slices multi-source, got part counts {parts}"
    )

    _assert_streams(result.stderr, PAGE_COUNT + 1)

    # The shift also means the strip no longer divides evenly: one 50 px tail slice is left over.
    slices = sorted(output_dir.glob("output_*.png"))
    assert len(slices) == PAGE_COUNT + 1
    assert png_size(slices[0])  == (TARGET_W, SLICE_H)
    assert png_size(slices[-1]) == (TARGET_W, INSERT_H * TARGET_W // INSERT_W)
