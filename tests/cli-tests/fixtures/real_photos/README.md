# real_photos — geometry/orientation regression fixtures

Three real phone-camera JPEGs, moved here from an ad-hoc manual-verification
folder (`Platemaker-qt/Platemaker/temp/win10/`) used while diagnosing the two
defects logged in `docs/TODO.md` ("(A) vips_arrayjoin black-band padding" and
"(B) EXIF orientation is ignored"). Checked in so the regression checks that
were previously manual (`--trace=0x7` + eyeballing) run automatically in
`tests/cli-tests/test_process.py`.

| file                 | stored dimensions | EXIF Orientation |
|----------------------|--------------------|-------------------|
| `20161127_144048.jpg`| 3264×2448          | 3 (180°)          |
| `20161127_144051.jpg`| 3264×2448          | 3 (180°)          |
| `20161127_144117.jpg`| 3264×2448          | 6 (90° CW, portrait shot) |

The mixed orientations plus non-multiple-of-slice-height combined pixel
heights are exactly what forces `ScaledStrip::buildSlice()` into a
multi-source slice and exposes both bugs — synthetic solid-colour PNGs
(`helpers.make_solid_png`) can't reproduce this because their EXIF is empty
and their generated pixel heights were always chosen to be clean multiples.

No GPS EXIF tag is present in any of the three files. Other EXIF (camera
make/model, capture date) is left intact — the checked-in tests only read
`Orientation` and pixel dimensions.
