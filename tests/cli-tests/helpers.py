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
    sig  = b"\x89PNG\r\n\x1a\n"
    ihdr = _png_chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0),
    )
    row  = bytes([0]) + bytes([r, g, b] * width)
    raw  = row * height
    idat = _png_chunk(b"IDAT", zlib.compress(raw, 1))
    iend = _png_chunk(b"IEND", b"")

    pathlib.Path(path).write_bytes(sig + ihdr + idat + iend)


def make_solid_rgba_png(
    path: str | pathlib.Path,
    width: int,
    height: int,
    r: int = 255,
    g: int = 255,
    b: int = 255,
    a: int = 255,
) -> None:
    """
    Write a minimal solid-colour **RGBA** PNG (colour type 6) — a 4-band source.

    Same stdlib-only approach as :func:`make_solid_png`, but with an alpha
    channel, so libvips decodes it as a 4-band image.  Used to build folders
    that mix 3-band (RGB) and 4-band (RGBA) pages.

    :param a: Alpha channel value (0–255), default 255 (opaque).
    """
    sig  = b"\x89PNG\r\n\x1a\n"
    ihdr = _png_chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0),  # colour type 6 = RGBA
    )
    row  = bytes([0]) + bytes([r, g, b, a] * width)
    raw  = row * height
    idat = _png_chunk(b"IDAT", zlib.compress(raw, 1))
    iend = _png_chunk(b"IEND", b"")

    pathlib.Path(path).write_bytes(sig + ihdr + idat + iend)


def png_color_type(path: str | pathlib.Path) -> int:
    """
    Return the PNG colour-type byte from a file's IHDR (stdlib only).

    2 = RGB (3-band), 6 = RGBA (4-band).  The byte lives at a fixed offset:
    8-byte signature + 8-byte chunk header + width(4) + height(4) + bit-depth(1).
    """
    data = pathlib.Path(path).read_bytes()
    return data[25]


# ---------------------------------------------------------------------------
# Workspace helpers
# ---------------------------------------------------------------------------

def create_workspace(
    platemaker_bin: pathlib.Path,
    workspace_path: pathlib.Path,
    *,
    target_width: int = 800,
    slice_height: int = 1280,
) -> None:
    """
    Call ``platemaker workspace create`` and assert success.

    Creates an *empty* workspace (no canvas profiles).  Canvas profiles must
    be added separately with :func:`add_profile`.

    :param platemaker_bin: Path to the compiled CLI binary.
    :param workspace_path: Destination ``.platemaker.json`` file.
    :param target_width:   Default output target width in pixels.
    :param slice_height:   Default output slice height in pixels.
    :raises AssertionError: If the command exits with a non-zero code.
    """
    result = subprocess.run(
        [
            str(platemaker_bin),
            "workspace", "create",
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


def add_profile(
    platemaker_bin: pathlib.Path,
    workspace_path: pathlib.Path,
    *,
    name: str,
    canvas: str = "",
    canvas_safe_area: str = "",
    margins: str = "0,0,0,0",
) -> None:
    """
    Call ``platemaker workspace add-canvas-profile`` and assert success.

    Exactly one of *canvas* or *canvas_safe_area* must be provided (passing
    neither raises :class:`ValueError`; passing both is also an error because
    the CLI rejects it).

    When *canvas_safe_area* is given, ``--canvas-safe-area`` is passed to the
    CLI instead of ``--canvas``.  The tool then computes the absolute canvas
    size as ``canvas = safe-area + margins``.

    :param platemaker_bin:  Path to the compiled CLI binary.
    :param workspace_path:  Path to an existing ``.platemaker.json`` workspace.
    :param name:            Profile name (must be unique within the workspace).
    :param canvas:          Absolute canvas size ``"WxH"``, e.g. ``"1600x10240"``.
                            Mutually exclusive with *canvas_safe_area*.
    :param canvas_safe_area: Drawable area ``"WxH"``.  The tool adds *margins*
                             to produce the stored absolute canvas size.
                             Mutually exclusive with *canvas*.
    :param margins:         Margins ``"T,R,B,L"``, e.g. ``"100,100,100,100"``.
    :raises ValueError:     If both or neither of *canvas* / *canvas_safe_area*
                            are provided.
    :raises AssertionError: If the CLI command exits with a non-zero code.
    """
    if bool(canvas) == bool(canvas_safe_area):
        raise ValueError(
            "Exactly one of 'canvas' or 'canvas_safe_area' must be provided, "
            f"got canvas={canvas!r}, canvas_safe_area={canvas_safe_area!r}"
        )

    canvas_flag = "--canvas" if canvas else "--canvas-safe-area"
    canvas_val  = canvas    if canvas else canvas_safe_area

    result = subprocess.run(
        [
            str(platemaker_bin),
            "workspace", "add-canvas-profile",
            "--workspace", str(workspace_path),
            "--name",      name,
            canvas_flag,   canvas_val,
            "--margins",   margins,
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, (
        f"workspace add-canvas-profile failed (rc={result.returncode}):\n"
        f"stdout: {result.stdout}\nstderr: {result.stderr}"
    )
