# Contributing

Thank you for your interest in contributing to Platemaker!

## Contributor License Agreement

By opening a pull request you agree to the [Contributor License Agreement](CLA.md).
This allows the project to be offered under additional license models in the future
while keeping your contributions permanently credited.

## Submitting a pull request

1. Fork the repository and create a branch from `main`.
2. Make your changes; the build and the tests must pass (`cmake --preset`, `cmake --build --preset`,
   `ctest --preset` with one preset, e.g. `msvc-release`; see [docs/CHEATSHEET.md](docs/CHEATSHEET.md)).
3. Open a pull request saying what changed and why, and how you verified it.

Everything in the repository — code, comments, documentation, commit messages — is in English.
Code follows [docs/CODING_STYLE.md](docs/CODING_STYLE.md); the design rules are in the wiki's
[Home](docs/specification/Home.md#critical-design-rules).

## What a change includes

A change is complete when, in the same pull request:
- its Doxygen comments and the CLI's `--help` text match the new behaviour;
- the specification ([docs/specification/](docs/specification/), the wiki) describes it;
- [docs/CHANGELOG.md](docs/CHANGELOG.md) has its entry, if a user of a release would notice it;
- its tests cover it: every line of the library is covered (see *Tests and Coverage* in
  [docs/CODING_STYLE.md](docs/CODING_STYLE.md)). The library is what the tests target; the CLI is
  exercised end to end.

## Commit messages

- Subject: `type(scope): summary`, the scope always given — `feat(cli): …`, `fix(lib): …`, `ci(release): …`.
- The body says what changed in behaviour or architecture, and why. A comment brought in line with the
  code is not a change, and tests are part of the change they cover: mention them only when the
  commit is about tests.

## The CHANGELOG

It lists what changed for a user between two **published** releases.

- One bullet per change. The title is often enough; add one or two sentences only when it is not.
- A fix may take up to three sentences — how it was, what changed, how it is now — when it needs them.
- Never list a fix for a bug that no published release had.
- One feature serving several scenarios is one bullet.
- No rationale and no options considered: those belong in the wiki or in `docs/`.
- No file names, function names, tests, CI or refactors unless a user sees or must act on them.
- Breaking changes come first, each with what the user must change.

## Documentation

The specification describes the current state. History lives only in the CHANGELOG, and only relative
to published releases: something that never shipped has no "used to".

## AI-assisted contributions

Contributions written with an AI assistant are welcome; the author answers for every line. Commits
made with one say so in a `Co-Authored-By` trailer. [CLAUDE.md](CLAUDE.md) gives an agent the map
of this repository.
