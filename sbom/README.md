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

Copy that over `sbom/sbom.spdx.json` and commit. As of 0.4.0 this snapshot lists the **full bundled
DLL graph** (libplatemaker + nlohmann/json + the ~29 libvips runtime / compiler-runtime components),
generated from the web-build's `versions.json` + `lib/cmake/third_party.json` — so regenerate it after a
`PLATEMAKER_VIPS_WEB_VERSION` bump too, not only on a direct-dep change. The
[`Third-party coverage`](../.github/workflows/thirdparty-coverage.yml) workflow guards against a new
bundled DLL being missed.

> Note: enabling the graph needs *Dependency graph* turned on in the repo settings — on by default
> for public repositories.
