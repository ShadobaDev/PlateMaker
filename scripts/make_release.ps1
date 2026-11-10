#Requires -Version 5.1
<#
    make_release.ps1 — build, package and install the Windows distributables.

    For each preset given (all four when none is), runs `cmake --workflow --preset dist-<p>`
    (configure -> build -> package, dropping the dev + cli ZIPs in dist\) and then installs the
    result into install\<p>, where a consumer such as the GUI expects it.

    The dist-* presets build without tests, in build\dist-<p>, so the development trees in
    build\<p> are left as they are.

        .\scripts\make_release.ps1                       # all four
        .\scripts\make_release.ps1 -Preset msvc-release  # one
        .\scripts\make_release.ps1 msvc-release, msvc-debug
        .\scripts\make_release.ps1 dist-msvc-release      # the workflow's own name works too

    Toolchains come from the shell you launch this in, exactly as when you run the
    workflows by hand:
      - mingw-* uses Ninja + gcc, so MSYS2 mingw64\bin must be on PATH.
      - msvc-* uses the Visual Studio 2022 generator, which drives MSBuild and sets up the MSVC
        environment itself — no Developer PowerShell / vcvars needed.

    Linux packages are produced by the sibling make_release.sh.

    SPDX-License-Identifier: LGPL-3.0-or-later
#>

param(
    [string[]]$Preset = @('mingw-release', 'mingw-debug', 'msvc-release', 'msvc-debug')
)

# Stop on PowerShell cmdlet errors. Native-exe exit codes are NOT covered by this, so each
# cmake call is checked explicitly against $LASTEXITCODE below.
$ErrorActionPreference = 'Stop'

# Run from the repo root (where CMakePresets.json lives), whatever the caller's cwd.
Set-Location (Join-Path $PSScriptRoot '..')

# A preset is named either way, msvc-release or the workflow's dist-msvc-release.
$known = @('mingw-release', 'mingw-debug', 'msvc-release', 'msvc-debug')
$Preset = @($Preset | ForEach-Object { $_ -replace '^dist-', '' })
foreach ($p in $Preset) {
    if ($known -notcontains $p) {
        Write-Error "Unknown preset '$p' (expected one of: $($known -join ', '))."
        exit 2
    }
}

function Invoke-CMake([string]$What) {
    # $args is the cmake command line; a failure ends the script with cmake's exit code.
    & cmake @args
    if ($LASTEXITCODE -ne 0) {
        Write-Error "$What failed (exit $LASTEXITCODE)."
        exit $LASTEXITCODE
    }
}

Write-Host "Platemaker release - Windows"
cmake --version | Select-Object -First 1
Write-Host ""

foreach ($p in $Preset) {
    Write-Host "==> cmake --workflow --preset dist-$p"
    Invoke-CMake "Workflow 'dist-$p'" --workflow --preset "dist-$p"

    # The MSVC tree is multi-config, so the install names its configuration; Ninja ignores it.
    $config = if ($p -like '*-debug') { 'Debug' } else { 'Release' }
    Write-Host "==> cmake --install build\dist-$p --config $config"
    Invoke-CMake "Install of 'dist-$p'" --install "build\dist-$p" --config $config
    Write-Host ""
}

Write-Host "Artifacts in dist\:"
if (Test-Path dist) {
    Get-ChildItem dist | Select-Object -ExpandProperty Name
} else {
    Write-Host "  (none - did the package step run?)"
}
