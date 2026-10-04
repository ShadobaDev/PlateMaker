# Specification

The specification lives in this repository's **wiki**, checked out here as a git submodule at
[`docs/specification/`](specification/).

**Read it at:** https://github.com/ShadobaDev/PlateMaker/wiki

```
git clone --recurse-submodules https://github.com/ShadobaDev/PlateMaker.git
# or, in an existing clone:
git submodule update --init docs/specification
```

The wiki is written for developers. The **end-user manual** for the Platemaker application is a
different wiki, in the [Platemaker-qt](https://github.com/ShadobaDev/Platemaker-qt/wiki) repository.

## What is where

| Page | Covers |
|---|---|
| [Home](https://github.com/ShadobaDev/PlateMaker/wiki/Home) | Project overview, the virtual strip model, architecture (layers, key decisions, design rules, the CLI as a first-class citizen), data models |
| [Components](https://github.com/ShadobaDev/PlateMaker/wiki/Components) | The core library and the infrastructure layer |
| [CLI](https://github.com/ShadobaDev/PlateMaker/wiki/CLI) | The `platemaker` CLI, and what the Qt GUI uses |
| [Render](https://github.com/ShadobaDev/PlateMaker/wiki/Render) | The image processing pipeline, the render output contract (lib ↔ consumer), performance |
| [Canvas Profiles](https://github.com/ShadobaDev/PlateMaker/wiki/Canvas-Profiles) | Per-image resolution, staleness detection, the profile matcher |
| [Project](https://github.com/ShadobaDev/PlateMaker/wiki/Project) | The conflict guard (the `ProjectItem` invariant) |
| [Workspace](https://github.com/ShadobaDev/PlateMaker/wiki/Workspace) | File and directory conventions |
| [Platform](https://github.com/ShadobaDev/PlateMaker/wiki/Platform) | Cross-platform targets, development environment, third-party dependencies, distribution |
| [Roadmap](https://github.com/ShadobaDev/PlateMaker/wiki/Roadmap) | Planned features, the future web application, lessons from the Clip2l prototype |
