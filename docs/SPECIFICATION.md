# Specification — moved to the wiki

The specification now lives in this repository's **wiki**, and is checked out here as a git submodule
at [`docs/specification/`](specification/).

**Read it at:** https://github.com/ShadobaDev/PlateMaker/wiki

```
git clone --recurse-submodules https://github.com/ShadobaDev/PlateMaker.git
# or, in an existing clone:
git submodule update --init docs/specification
```

## Why it moved

It was one 1225-line document, of which three sections were 71%. It had no internal links, two
headings both numbered 7.5.3, and no heading at all for one of the four things it was about. Splitting
it made the pages short enough to maintain; putting them in the wiki gives them a rendered home with
navigation, and keeps one copy rather than a repository version and a published version drifting apart.

The wiki's audience is the developer. The **end-user manual** for the Platemaker application is a
different wiki, in the [Platemaker-qt](https://github.com/ShadobaDev/Platemaker-qt/wiki) repository.

## Where each section went

Section numbers were dropped when the document was split — they stopped meaning anything across
separate pages, and the duplicate 7.5.3 went with them. This table is kept because `docs/CHANGELOG.md`
and `docs/TODO.md` cite sections by number, and changelog entries are a historical record that is not
rewritten.

| Was | Section | Now |
|---|---|---|
| §1 | Project Overview | [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) |
| §2 | Core Use Case — The Virtual Strip Model | [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) |
| §3 | New Features (Planned) | [Roadmap](https://github.com/ShadobaDev/PlateMaker/wiki/Roadmap) |
| §3.1 | Canvas Template Generator | [Roadmap](https://github.com/ShadobaDev/PlateMaker/wiki/Roadmap) |
| §3.2 | Margin-Aware Import Pipeline | [Roadmap](https://github.com/ShadobaDev/PlateMaker/wiki/Roadmap) |
| §4 | Architecture | [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) |
| §4.1 | Layer Diagram | [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) |
| §4.2 | Key Architectural Decisions | [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) |
| §4.3 | Critical Design Rules | [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) |
| §4.4 | CLI as First-Class Citizen | [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) |
| §5 | Component Descriptions | [Components](https://github.com/ShadobaDev/PlateMaker/wiki/Components) |
| §5.1 | Core Library — libplatemaker | [Components](https://github.com/ShadobaDev/PlateMaker/wiki/Components) |
| §5.2 | Infrastructure | [Components](https://github.com/ShadobaDev/PlateMaker/wiki/Components) |
| §5.3 | CLI — platemaker | [CLI](https://github.com/ShadobaDev/PlateMaker/wiki/CLI) |
| §5.4 | Qt GUI — platemaker-gui | [CLI](https://github.com/ShadobaDev/PlateMaker/wiki/CLI) |
| §6 | Data Models | [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) |
| §7 | Image Processing Pipeline Details | [Render](https://github.com/ShadobaDev/PlateMaker/wiki/Render) |
| §7.0 | Render output contract (lib ↔ consumer) | [Render](https://github.com/ShadobaDev/PlateMaker/wiki/Render) |
| §7.5.1 | Per-image resolution | [Canvas Profiles](https://github.com/ShadobaDev/PlateMaker/wiki/Canvas-Profiles) |
| §7.5.2 | Conflict guard (ProjectItem invariant) | [Project](https://github.com/ShadobaDev/PlateMaker/wiki/Project) |
| §7.5.3 | Staleness detection — per-input dimensions and precise re-match | [Canvas Profiles](https://github.com/ShadobaDev/PlateMaker/wiki/Canvas-Profiles) |
| §7.5.3 | Library API (planned — Core::CanvasProfileMatcher) | [Canvas Profiles](https://github.com/ShadobaDev/PlateMaker/wiki/Canvas-Profiles) |
| §8 | Performance Strategy | [Render](https://github.com/ShadobaDev/PlateMaker/wiki/Render) |
| §9 | Cross-Platform Targets | [Platform](https://github.com/ShadobaDev/PlateMaker/wiki/Platform) |
| §10 | Future: Web Application | [Roadmap](https://github.com/ShadobaDev/PlateMaker/wiki/Roadmap) |
| §11 | Development Roadmap | [Roadmap](https://github.com/ShadobaDev/PlateMaker/wiki/Roadmap) |
| §12 | Development Environment | [Platform](https://github.com/ShadobaDev/PlateMaker/wiki/Platform) |
| §13 | File & Directory Conventions | [Workspace](https://github.com/ShadobaDev/PlateMaker/wiki/Workspace) |
| §14 | Third-Party Dependencies | [Platform](https://github.com/ShadobaDev/PlateMaker/wiki/Platform) |
| §15 | Distribution | [Platform](https://github.com/ShadobaDev/PlateMaker/wiki/Platform) |
| §16 | Lessons from the Clip2l Prototype | [Roadmap](https://github.com/ShadobaDev/PlateMaker/wiki/Roadmap) |

## Diagrams

Nine draw.io diagrams with SVG exports live in the wiki under `diagrams/`, described on the
[Diagrams](https://github.com/ShadobaDev/PlateMaker/wiki/Diagrams) page.
