# SBOM snapshot for GitHub's Dependency graph

`sbom.spdx.json` is a **committed snapshot** of the library's Software Bill of Materials (SPDX 2.3),
used only to feed GitHub's **Dependency graph** (Insights → Dependency graph) and Dependabot alerts.

GitHub cannot read dependencies from CMake (`FetchContent` / `find_package` / the prebuilt libvips
zip), so the graph would otherwise be empty. The
[`.github/workflows/dependency-submission.yml`](../.github/workflows/dependency-submission.yml)
workflow submits this file through the Dependency Submission API on every push that touches `sbom/`.

## When to regenerate

Our dependencies are pinned in [`CMakeLists.txt`](../CMakeLists.txt) (libvips, nlohmann/json) and
change rarely. **Regenerate this file whenever a pinned dependency version changes** (or the lib's own
version bumps). The canonical copy is produced by the build:

```
build/mingw-release/lib/credits/sbom.spdx.json
```

Copy that over `sbom/sbom.spdx.json` and commit. (This snapshot lists the **direct** dependencies;
extending it to the full bundled DLL graph is tracked in `docs/TODO.md`.)

> Note: enabling the graph needs *Dependency graph* turned on in the repo settings — on by default
> for public repositories.
