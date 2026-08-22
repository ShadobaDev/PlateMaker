#Requires -Version 5.1
<#
    make_release.ps1 — build and package every Windows distributable in sequence.

    Runs `cmake --workflow --preset <p>` for each dist preset below, which does
    configure -> build -> package and drops the dev + cli ZIPs in dist\.

    Toolchains come from the shell you launch this in, exactly as when you run the
    workflows by hand:
      - dist-mingw-* uses Ninja + gcc, so MSYS2 mingw64\bin must be on PATH.
      - dist-msvc-release uses the Visual Studio 2022 generator, which drives MSBuild
        and sets up the MSVC environment itself — no Developer PowerShell / vcvars needed.

    Linux packages are produced by the sibling make_release.sh.

    SPDX-License-Identifier: LGPL-3.0-or-later
#>

# Stop on PowerShell cmdlet errors. Native-exe exit codes are NOT covered by this, so each
# cmake call is checked explicitly against $LASTEXITCODE below.
$ErrorActionPreference = 'Stop'

# Run from the repo root (where CMakePresets.json lives), whatever the caller's cwd.
Set-Location (Join-Path $PSScriptRoot '..')

# The dist workflow presets to run, in order. Trim or extend this list as needed.
$presets = @(
    'dist-mingw-release'
    'dist-mingw-debug'
    'dist-msvc-release'
    'dist-msvc-debug'
)

Write-Host "Platemaker release - Windows"
cmake --version | Select-Object -First 1
Write-Host ""

foreach ($preset in $presets) {
    Write-Host "==> cmake --workflow --preset $preset"
    cmake --workflow --preset $preset
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Workflow '$preset' failed (exit $LASTEXITCODE)."
        exit $LASTEXITCODE
    }
    Write-Host ""
}

Write-Host "Artifacts in dist\:"
if (Test-Path dist) {
    Get-ChildItem dist | Select-Object -ExpandProperty Name
} else {
    Write-Host "  (none - did the package step run?)"
}
