# Cutting a release through GitHub Actions

How to release **libplatemaker** (this repo) via the CI [`Release`](../.github/workflows/release.yml)
workflow, instead of the old "create a Release from the GitHub form + tag on publish" flow.

**The model:** the workflow triggers on a **pushed tag** (`*.*.*`, a bare version like `0.4.0`). You push
the tag from git; CI builds + packages (MinGW + Linux), attaches a build-provenance attestation, then
**creates the GitHub Release and uploads the archives + `.sha256` sidecars** for you, and scans them on
VirusTotal. Do **not** touch the GitHub Release form — pushing the tag is the whole trigger.

A manual **"Run workflow"** (`workflow_dispatch`) builds and attests but **does not** publish a Release
(the `release` job is gated on `refs/tags/*`), so it's a safe dry run.

---

## 0. Pre-flight (30 seconds)

- Confirm the version in [`CMakeLists.txt`](../CMakeLists.txt) `project(... VERSION x.y.z)` **matches the
  tag you're about to push**. Archive names come from this, and the GUI downloads a lib asset by an exact
  name (`platemaker-dev-<VERSION>-windows-mingw-release.zip`) — a mismatch 404s the GUI later.
- Confirm `docs/CHANGELOG.md` has the entry for this version.
- Everything committed and pushed to `main`.
- One-time (already done for this repo): the `VT_API_KEY` repo secret is set, or the VirusTotal step
  self-skips.

## 1. Dry run — validate the build without releasing (recommended)

1. GitHub → **Actions** → **Release** → **Run workflow** → branch `main` → **Run workflow**.
2. Wait for the **build** job (windows-latest/MinGW + ubuntu-latest/Linux) to go green. The **release**
   job is skipped — it's not a tag. That's expected.
3. Optional: download the run's artifacts (`pkg-windows-latest` / `pkg-ubuntu-latest`) and inspect the
   package — e.g. `credits/` (SBOM + `THIRD-PARTY-NOTICES.txt`) — before publishing anything.
4. Red? Fix, `git push`, re-run. No tags to clean up (none pushed yet).

Why bother if a failed tag build won't release anyway: a dry run avoids leaving a **tag pointing at a
broken commit**, which you'd then have to delete and re-push.

## 2. Publish — push the tag

Once the dry run is green (or you're confident):

```bash
git checkout main
git pull
git tag 0.4.0                 # bare version, no "v" — matches *.*.* and the GUI's download URL
git push origin 0.4.0
```

This runs the full workflow: build (MinGW + Linux) → provenance attestation → the `release` job
**creates Release `0.4.0`**, uploads `*.zip` / `*.tar.gz` + `*.sha256`, then VirusTotal scans the assets
and appends the report links to the release body.

## 3. Verify

- **Actions** → the run is green (2× build + release).
- **Releases** → `0.4.0` exists with the assets:
  - `platemaker-dev-0.4.0-windows-mingw-release.zip` (+ `.sha256`)
  - `platemaker-cli-0.4.0-windows-mingw-release.zip` (+ `.sha256`)
  - `platemaker-{dev,cli}-0.4.0-linux-x86_64-release.tar.gz` (+ `.sha256`)
  - VirusTotal links appended to the body.
- Optional provenance check: `gh attestation verify <archive> --repo ShadobaDev/PlateMaker`.
- **Add release notes:** the workflow leaves the body empty apart from the VT links. Edit the Release on
  GitHub and paste the highlights from `docs/CHANGELOG.md`.

## 4. Then the GUI (separate, and only after this)

The GUI (`Platemaker-qt`) pins `LIBPLATEMAKER_VERSION` and its FetchContent fallback downloads
`platemaker-dev-<VERSION>-windows-mingw-release.zip` from **this repo's Releases**. So release the lib
**first** (steps above); only once its assets are up, cut the GUI release from its own repo (its own tag,
its own `VT_API_KEY`, its own Qt-via-aqt setup).

## Rollback — if you pushed a tag on a bad build

```bash
git tag -d 0.4.0                      # delete locally
git push origin :refs/tags/0.4.0      # delete on the remote
# fix, commit, push, then re-tag and push again
```

If a Release was already created for that tag, delete it on the Releases page too before re-pushing.

## Troubleshooting

- **Jobs hang on "waiting for a runner to come online" for many minutes** — this is almost always a
  GitHub-side runner-queue delay or an Actions incident, not the workflow. Check
  [githubstatus.com](https://www.githubstatus.com/); if Actions is degraded/outage, **wait** (retrying
  doesn't summon runners) and hold off on pushing the real tag until it's green. Public repos have free,
  unlimited Actions minutes, so this is not a quota issue.
- **Linux job fails but MinGW is fine** — the matrix is `fail-fast: false` and Linux is a trial; the
  MinGW release still publishes. Fix Linux separately if needed.
- **GUI configure 404s on the lib download** — the tag/`CMakeLists` version and the asset name don't
  match, or the lib Release isn't published yet (see step 0 and step 4).
