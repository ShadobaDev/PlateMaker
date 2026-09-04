# overlays — strip-overlay compositing fixture (bubbles & caption)

Three hand-written SVG bubble shapes and their rasterised RGBA PNGs. They give
`tests/cli-tests/test_overlays.py` a *real* overlay layer to composite — soft
anti-aliased edges, a fully transparent background, and partial alpha along
every stroke — where the C++ suite
(`tests/lib-unit-tests/test_overlay_anchoring.cpp`) uses flat opaque squares
because it is asserting *placement*, not blending.

| file                 | size   | shape                                     |
|----------------------|--------|-------------------------------------------|
| `bubble-speech.png`  | 240×160 | rounded balloon with a tail, down-left    |
| `bubble-shout.png`   | 240×160 | 24-point jagged burst (lots of diagonals) |
| `caption-box.png`    | 240×96  | plain narration box                       |

All three are 4-band sRGB, alpha 0 outside the shape and 255 in the fill.

Nothing here is traced or derived from third-party art: the SVGs are a handful
of `<path>` / `<rect>` primitives written for this repo, so they are free to
redistribute.

## Regenerating the PNGs

The SVG is the editable source; the PNG is what the library composites (the
library is deliberately format-agnostic and takes a pre-rendered RGBA bitmap —
it never grows a text or SVG engine). libvips rasterises them through librsvg,
so this needs no tool the project does not already depend on:

```sh
vips copy bubble-speech.svg bubble-speech.png
```

Re-rasterising changes the bytes, which changes each overlay's SHA-256 — the
library treats that as a content change and re-renders. Regenerate only when a
shape actually changes.
