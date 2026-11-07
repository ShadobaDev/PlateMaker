# libplatemaker — notes for coding agents

A C++20 library that turns comic pages into fixed-height output slices, plus the `platemaker` CLI.
The Qt GUI is a separate repository that links this library.

Rules for every change — commits, the CHANGELOG, what a change must include — are in
[CONTRIBUTING.md](CONTRIBUTING.md). Read it before proposing a commit message or a CHANGELOG entry.

## Always
- The library has no Qt, keeps no state between calls (a workspace is what its file says), and never
  chooses where files live. Full list: *Critical Design Rules* in `docs/specification/Home.md`.
- Every path crosses `std::filesystem` through `utf8ToPath()` / `pathToUtf8()`, never a narrow string.
- Tests target the library; the CLI is tested end to end. CI fails on an uncovered line of the library.

## Where to look

| When you… | Read |
|---|---|
| need what a feature does, or where it is specified | `docs/specification/Home.md` (index of the wiki) |
| add or move a class, a file, a namespace | `docs/CODING_STYLE.md` |
| add a processing step | `docs/specification/Processing-Steps.md` |
| touch rendering or its contract with the consumer | `docs/specification/Render.md` |
| touch tests or coverage | `docs/CODING_STYLE.md` § Tests and Coverage, `docs/specification/Platform.md` § Tests and Coverage |
| change the CLI | `docs/specification/CLI.md` |
| plan work by its semver impact | `docs/TODO.md` |
| prepare a release | `docs/GITHUB-RELEASE.md` |
| hit a known past bug | `docs/ISSUES.md` |

`docs/specification/` is a git submodule: the GitHub wiki. Changes there are committed in that
repository, separately from this one.

## Build and test

MSVC is the primary toolchain; commands for every preset are in `docs/CHEATSHEET.md`.

```
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

A MinGW binary run from a shell without `C:\msys64\mingw64\bin` on `PATH` fails with `0xc0000139`:
a missing DLL, not a code defect.
