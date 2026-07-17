# TODO

## CLI

---

## Tests

### `cli-tests` intermittently hangs (suspected environment, not code)

`ctest -R cli-tests` sporadically stalls until it trips its `TIMEOUT 120`
(`tests/cli-tests/CMakeLists.txt`), reported as `cli-tests (Timeout)`. A normal run
takes ~10 s. Re-running usually passes, so it is easy to mistake for a flaky test.

Evidence gathered so far (2026-07-17, mingw-release):

- **Not a code regression.** Reproduced on a pristine `HEAD` worktree built from scratch
  (1 hang / 8 runs), so it predates the canvas-fingerprint work.
- **Not a specific test.** The stall lands at a random point each time — observed after
  7 %, 24 %, 37 %, 77 % and 92 % of the suite, across different files.
- **Not first-run/cold-binary related.** A run immediately after relinking the DLL + exe
  passed in 10.5 s while a later run on the *same* binaries hung — the opposite of the
  "antivirus scans the fresh binary" theory.
- `test_template.py` alone: 19 passed, ~3.9 s, 5/5 stable. Only the full suite hangs.
- Rate measured at 1/8 (pristine HEAD) vs 4/8 (working tree) — too small a sample to
  tell those apart (p ≈ 0.28); do not read a regression into it without more runs.
- Possibly related on the same machine: `g++` cannot spawn `cc1plus` from Git Bash
  (exit 127, no diagnostics, fails even on a trivial `int main(){}`), while the same
  compiler works from PowerShell. Both smell like intermittent process-spawn
  interference (AV / EDR real-time scanning), and the suite spawns ~200 CLI processes.

Next step when picking this up: reproduce, and while it is stuck inspect whether a
`platemaker-cli.exe` child is alive (spawned but never exiting) or whether pytest itself
is stuck — that separates "the CLI hangs" from "the OS won't start the process". The
user suspects a cluttered Windows install and may reset the machine first, which would
also test the environment hypothesis.

### Stage 1 integration tests (unit tests with real pixel data)

Seven tests are currently stubbed with `GTEST_SKIP()` pending real image fixtures:

| Test | File |
|---|---|
| `PixelBufferTest.MoveConstructorTransfersOwnership` | `test_pixel_buffer.cpp` |
| `PixelBufferTest.MoveAssignmentTransfersOwnership` | `test_pixel_buffer.cpp` |
| `ScalerTest.ScaleNonExistentFileThrows` | `test_pixel_buffer.cpp` |
| `ScaledStripTest.SliceAllWithZeroSliceHeightThrows` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllCropPolicyDiscardsRemainder` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllPadWhiteProducesFullHeightTailSlice` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllKeepAsIsPreservesShortTailSlice` | `test_scaled_strip.cpp` |

Need: small test PNG fixtures in `tests/lib-unit-tests/fixtures/` and a test
helper that calls `vips_init()`/`vips_shutdown()` in a `SetUpTestSuite` /
`TearDownTestSuite` pair.

---

## Library

### Structured error system

The pipeline currently reports failures as ad-hoc strings via the
`ProcessingPipeline` log/`errorMessage` callbacks (the GUI shows them verbatim).
Replace with structured error codes/categories (load / profile-match / slice /
encode / io) carrying a stable code + message, so the GUI can localise, group and
react (e.g. offer "open output folder", "re-scan inputs"). `ProcessingOutcome`
would carry typed errors instead of a plain string.

### Dynamic thread spawning for processing

`ProcessingPipeline` runs single-threaded because the virtual strip is built
incrementally and a single slice may span more than one input. A future
pre-process could split the strip at input boundaries into segments that each
yield a whole number of slices; independent segments could then be scaled/sliced
on separate threads and the slice files numbered deterministically afterwards.

### Persist last render log

The GUI render log is in-memory only (cleared on exit). Optionally persist the
last run's log (and the slice/skip summary) next to the workspace so a user can
review what the previous render did.
