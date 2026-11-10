#!/usr/bin/env bash
#
# make_release.sh — build, package and install the Linux distributables.
#
# For each preset given (both when none is), runs `cmake --workflow --preset dist-<p>`
# (configure → build → package, dropping the dev + cli tarballs in dist/) and then installs the
# result into install/<p>, where a consumer such as the GUI expects it.
#
# The dist-* presets build without tests, in build/dist-<p>, so the development trees in
# build/<p> are left as they are.
#
#     scripts/make_release.sh                  # both
#     scripts/make_release.sh linux-release    # one
#
# Windows packages are produced by the sibling make_release.ps1.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

set -euo pipefail

# Run from the repo root (where CMakePresets.json lives), whatever the caller's cwd.
cd "$(dirname "$0")/.."

presets=("$@")
if [ ${#presets[@]} -eq 0 ]; then
    presets=(linux-release linux-debug)
fi
# A preset is named either way, linux-release or the workflow's dist-linux-release.
presets=("${presets[@]#dist-}")
for preset in "${presets[@]}"; do
    case "${preset}" in
        linux-release|linux-debug) ;;
        *) echo "Unknown preset '${preset}' (expected linux-release or linux-debug)." >&2; exit 2 ;;
    esac
done

echo "Platemaker release — Linux"
cmake --version | head -n1
echo

for preset in "${presets[@]}"; do
    echo "==> cmake --workflow --preset dist-${preset}"
    cmake --workflow --preset "dist-${preset}"
    echo "==> cmake --install build/dist-${preset}"
    cmake --install "build/dist-${preset}"
    echo
done

echo "Artifacts in dist/:"
if compgen -G "dist/*" > /dev/null; then
    ls -1 dist/
else
    echo "  (none — did the package step run?)"
fi
