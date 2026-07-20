#!/usr/bin/env bash
#
# make_release.sh — build and package every Linux distributable in sequence.
#
# Runs `cmake --workflow --preset <p>` for each dist-linux-* preset, which does
# configure → build → package and drops the dev + cli tarballs in dist/.
#
# Windows (MinGW) packages are produced by the sibling make_release.ps1.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

set -euo pipefail

# Run from the repo root (where CMakePresets.json lives), whatever the caller's cwd.
cd "$(dirname "$0")/.."

# The dist workflow presets to run, in order. Trim or extend this list as needed.
presets=(
    dist-linux-release
    dist-linux-debug
)

echo "Platemaker release — Linux"
cmake --version | head -n1
echo

for preset in "${presets[@]}"; do
    echo "==> cmake --workflow --preset ${preset}"
    cmake --workflow --preset "${preset}"
    echo
done

echo "Artifacts in dist/:"
if compgen -G "dist/*" > /dev/null; then
    ls -1 dist/
else
    echo "  (none — did the package step run?)"
fi
