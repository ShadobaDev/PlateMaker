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
    canvas: str,
    margins: str = "0,0,0,0",
) -> None:
    """
    Call ``platemaker workspace add-profile`` and assert success.

    :param platemaker_bin: Path to the compiled CLI binary.
    :param workspace_path: Path to an existing ``.platemaker.json`` workspace.
    :param name:           Profile name (must be unique within the workspace).
    :param canvas:         Canvas size string ``"WxH"``, e.g. ``"1600x10240"``.
    :param margins:        Margins ``"T,R,B,L"``, e.g. ``"100,100,100,100"``.
    :raises AssertionError: If the command exits with a non-zero code.
    """
    result = subprocess.run(
        [
            str(platemaker_bin),
            "workspace", "add-profile",
            "--workspace", str(workspace_path),
            "--name",      name,
            "--canvas",    canvas,
            "--margins",   margins,
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, (
        f"workspace add-profile failed (rc={result.returncode}):\n"
        f"stdout: {result.stdout}\nstderr: {result.stderr}"
    )
