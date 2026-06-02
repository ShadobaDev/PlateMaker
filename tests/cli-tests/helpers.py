"""
helpers.py — test utilities for Platemaker CLI integration tests.

Provides a minimal PNG generator that uses only Python standard library
(struct + zlib).  No Pillow or other imaging dependencies required.
"""

from __future__ import annotations

import pathlib
import struct
import subprocess
import zlib


# ---------------------------------------------------------------------------
# Minimal PNG writer (stdlib only)
# ---------------------------------------------------------------------------

def _png_chunk(tag: bytes, data: bytes) -> bytes:
    """Build a single PNG chunk: [length][tag][data][crc32]."""
    crc = zlib.crc32(tag + data) & 0xffffffff
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def make_solid_png(
    path: str | pathlib.Path,
    width: int,
    height: int,
    r: int = 255,
    g: int = 255,
    b: int = 255,
) -> None:
    """
    Write a minimal solid-colour RGB PNG file to *path*.

    Uses only Python's stdlib (struct + zlib).  The resulting file is a valid
    PNG that libvips can open.

    :param path:   Destination file path.
    :param width:  Image width in pixels.
    :param height: Image height in pixels.
    :param r:      Red channel value (0–255), default 255.
    :param g:      Green channel value (0–255), default 255.
    :param b:      Blue channel value (0–255), default 255.
    """
    # PNG 8-byte signature
    sig = b"\x89PNG\r\n\x1a\n"

    # IHDR: width(4) height(4) bit-depth(1) colour-type=2/RGB(1)
    #       compression(1) filter(1) interlace(1)  → 13 bytes total
    ihdr = _png_chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0),
    )

    # Each scanline: filter byte 0x00 (None) + width × RGB pixels
    row  = bytes([0]) + bytes([r, g, b] * width)
    raw  = row * height
    idat = _png_chunk(b"IDAT", zlib.compress(raw, 1))  # level 1 = fastest

    iend = _png_chunk(b"IEND", b"")

    pathlib.Path(path).write_bytes(sig + ihdr + idat + iend)


# ---------------------------------------------------------------------------
# Workspace creation helper
# ---------------------------------------------------------------------------

def create_workspace(
    platemaker_bin: pathlib.Path,
    workspace_path: pathlib.Path,
    *,
    canvas: str = "800x2560",
    margins: str = "0,0,0,0",
    target_width: int = 800,
    slice_height: int = 1280,
    name: str = "Default",
) -> None:
    """
    Call ``platemaker workspace create`` and assert success.

    :param platemaker_bin: Path to the compiled CLI binary.
    :param workspace_path: Destination ``.platemaker.json`` file.
    :param canvas:         Canvas size string, e.g. ``"800x2560"``.
    :param margins:        Margins string ``"T,R,B,L"``, e.g. ``"0,0,0,0"``.
    :param target_width:   Target image width passed to ``--target-width``.
    :param slice_height:   Slice height passed to ``--slice-height``.
    :param name:           Profile name.
    :raises AssertionError: If the command exits with a non-zero code.
    """
    result = subprocess.run(
        [
            str(platemaker_bin),
            "workspace", "create",
            "--canvas",       canvas,
            "--margins",      margins,
            "--name",         name,
            "--target-width", str(target_width),
            "--slice-height", str(slice_height),
            "--output",       str(workspace_path),
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, (
        f"workspace create failed (rc={result.returncode}):\n"
        f"stdout: {result.stdout}\nstderr: {result.stderr}"
    )
