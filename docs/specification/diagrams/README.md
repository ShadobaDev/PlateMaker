# Diagrams

Seven draw.io diagrams of the library as it stands after the 0.6.0 refactor.

Open them with [draw.io](https://app.diagrams.net) (desktop or web), or the *Draw.io Integration*
extension in VS Code.

The prefix names the diagram type: `activity-` (UML activity diagram), `class-` (UML class
diagram), `package-` (UML package diagram), `concept-` (not a formal UML type — visualizes rules
or dependencies that don't map onto one).

| File | Answers |
|---|---|
| `activity-render-flow.drawio` | What `ProcessingPipeline::render()` does, phase by phase, including every error and cancellation exit and which of them are fatal. |
| `activity-page-domain-render.drawio` | How one input page becomes its slot on the strip. The body of the render's per-input region — and the only path that can carry a grade. |
| `activity-page-domain-layout.drawio` | How `layoutPagesFromHeaders()` prices a whole chapter at a header read per page, and what it does with a page it cannot read. |
| `activity-page-domain-decode.drawio` | How `decodePageToRgba()` normalises one page to RGBA8888 and copies it into the caller's buffer — the only entry point that pulls pixels. |
| `class-pipeline-structure.drawio` | Which class owns which part of a render, what `RenderRequest` carries, and what stays internal. |
| `package-lib-components.drawio` | The four layers, what lives in each, and the dependency rule between them. |
| `class-workspace-project-model.drawio` | The persisted data model, and where the line between an entity and its editor falls. |
| `concept-staleness-axes.drawio` | The six reasons a project's output can be stale, and what each one catches that the others cannot. |
| `activity-preview-workflow.drawio` | How the strip editor shows a chapter before any render exists, and where each step crosses the library boundary. |

The three page-domain diagrams share one opening — `planFromHeader()` is drawn identically in each —
because that shared body is the point being made: all three entry points run the same page domain, so
none of them can drift from the render. They diverge only in what they do with the `ScaledImage`, and
that is where each diagram ends. Two of them are shorter rather than repetitive: `grade` is non-null
only on the render path, so the other two have no grade branches at all.

## Conventions

**No colour.** Shapes carry geometry and text only, so the palette is whatever the reader sets.
Recolouring one is a normal edit, not a merge conflict waiting to happen.

**Notes carry the reasoning.** A box says *what*; the note beside it says *why it is that way and
what breaks otherwise*. The notes are the part worth reading — the boxes are recoverable from the
code, the reasons are not.

**Loops are expansion regions.** A repetition over a collection is drawn as a dashed rounded region
labelled «iterative» with a pin naming what it iterates — not as a merge with a back-edge. draw.io has
no native shape for it, so it is the dashed container style with a small labelled box on the boundary.

**Cell ids must avoid JavaScript prototype names.** draw.io keys decoded cells by id in a plain JS
object, so an id like `push`, `call`, `length`, `constructor` or `toString` resolves to that inherited
*function* instead of a cell — and the codec's next call is `cell.setId()`. The file then refuses to
open with `d.setId is not a function`, which names neither the file nor the offending id. Note the
hazard is the **JavaScript** set: `append` and `count` are Python methods and are perfectly safe here.

**Names match the code exactly.** A box reading `layoutPagesFromHeaders()` is a real symbol you can
grep for. When a symbol is renamed, the diagram is wrong until it is updated.

## Publishing

GitHub does not render `.drawio` in a wiki page. To publish one, export it from draw.io as
**Editable SVG** (*File → Export as → SVG*, with *Include a copy of my diagram* ticked) and commit
the resulting `.drawio.svg` to the wiki: that single file renders inline on GitHub **and** reopens
losslessly in draw.io, so there is no separate source to keep in step.

The `.drawio` files here stay the source of truth and the thing to review.
