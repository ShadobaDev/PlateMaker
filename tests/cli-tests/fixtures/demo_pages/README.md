# demo_pages — margin-crop / canvas-profile regression fixture

Three real webtoon pages from the author's own throwaway demo chapter
("Platemaker-demo / Chap 1"), a bare sketch checked in with permission and free
to redistribute. They give `tests/cli-tests/test_process.py`
(`test_process_margin_aware_pipeline`) a realistic end-to-end margin crop —
real 4-band decode at production resolution — where the rest of the suite uses
synthetic solid-colour PNGs (`helpers.make_solid_png`).

| file      | dimensions | bands            |
|-----------|------------|------------------|
| `001.png` | 2200×5720  | 4 (RGBA), sRGB   |
| `002.png` | 2200×5720  | 4 (RGBA), sRGB   |
| `003.png` | 2200×5720  | 4 (RGBA), sRGB   |

Each page was drawn from a **1600×5120 template with a 300 px margin on every
side** (1600+300+300 = 2200 wide, 5120+300+300 = 5720 tall). The margin is
painted a distinct **pink** — it is only a visual margin marker, present nowhere
else in the art. That structure maps exactly onto a canvas profile with
`safe-area = 1600×5120, margins = 300` (absolute canvas 2200×5720), so the test
exercises canvas-profile matching by W×H, the margin crop, a tall multi-slice
strip, and RGBA preservation through save.

The pages are a flat 3-colour sketch (white / black / pink), so they are **not**
suitable for output format/quality comparison (they would compress
unrepresentatively small) — that is a separate, still-open investigation needing
real tonal art.
